/* KallistiOS ##version##

   pvr_multipass_layout.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pvr_multipass_layout.h"

#define PVR_REGION_EMPTY_POINTER UINT32_C(0x80000000)
#define PVR_REGION_LAST          (UINT32_C(1) << 31)
#define PVR_REGION_KEEP_DEPTH    (UINT32_C(1) << 30)
#define PVR_REGION_PRESORT       (UINT32_C(1) << 29)
#define PVR_REGION_KEEP_TILE     (UINT32_C(1) << 28)
#define PVR_REGION_VRAM_LIMIT    UINT32_C(0x01000000)
#define PVR_BUFFER_ALIGNMENT     UINT32_C(128)

static bool opb_size_valid(uint32_t size) {
    return size == 0 || size == 32 || size == 64 || size == 128;
}

static bool add_u32(uint32_t lhs, uint32_t rhs, uint32_t *result) {
    if(rhs > UINT32_MAX - lhs)
        return false;

    *result = lhs + rhs;
    return true;
}

static bool multiply_u32(uint32_t lhs, uint32_t rhs, uint32_t *result) {
    if(lhs && rhs > UINT32_MAX / lhs)
        return false;

    *result = lhs * rhs;
    return true;
}

static bool align_u32(uint32_t value, uint32_t alignment, uint32_t *result) {
    const uint32_t mask = alignment - 1u;

    if(value > UINT32_MAX - mask)
        return false;

    *result = (value + mask) & ~mask;
    return true;
}

int pvr_ta_layout_calculate(pvr_ta_layout_t *layout, uint32_t tile_width,
                            uint32_t tile_height,
                            const pvr_ta_pass_layout_t *passes,
                            size_t pass_count) {
    uint32_t tile_count;
    uint32_t total_opb_size = 0;
    size_t pass;

    if(!layout || !passes || !tile_width || tile_width > 40 ||
            !tile_height || tile_height > 15 || !pass_count ||
            pass_count > PVR_MULTIPASS_MAX_PASSES) {
        errno = EINVAL;
        return -1;
    }

    if(!multiply_u32(tile_width, tile_height, &tile_count)) {
        errno = EOVERFLOW;
        return -1;
    }

    *layout = (pvr_ta_layout_t) {
        .tile_width = tile_width,
        .tile_height = tile_height,
        .tile_count = tile_count,
        .pass_count = pass_count
    };

    for(pass = 0; pass < pass_count; ++pass) {
        uint32_t pass_opb_size = 0;
        bool any_list = false;
        size_t list;

        layout->pass_opb_offset[pass] = total_opb_size;

        for(list = 0; list < 5; ++list) {
            uint32_t list_opb_size;

            if(!opb_size_valid(passes[pass].opb_size[list])) {
                errno = EINVAL;
                return -1;
            }

            layout->list_opb_offset[pass][list] = pass_opb_size;
            any_list |= passes[pass].opb_size[list] != 0;

            if(!multiply_u32(passes[pass].opb_size[list], tile_count,
                             &list_opb_size) ||
                    !add_u32(pass_opb_size, list_opb_size,
                             &pass_opb_size)) {
                errno = EOVERFLOW;
                return -1;
            }
        }

        if(!any_list) {
            errno = EINVAL;
            return -1;
        }

        layout->pass_opb_size[pass] = pass_opb_size;

        if(!add_u32(total_opb_size, pass_opb_size, &total_opb_size)) {
            errno = EOVERFLOW;
            return -1;
        }
    }

    layout->total_opb_size = total_opb_size;

    if(pass_count > (SIZE_MAX / PVR_REGION_WORDS_PER_ENTRY) / tile_count) {
        errno = EOVERFLOW;
        return -1;
    }

    layout->region_words = PVR_REGION_WORDS_PER_ENTRY +
        PVR_REGION_WORDS_PER_ENTRY * pass_count * tile_count;

    return 0;
}

int pvr_ta_layout_build_regions(uint32_t *regions, size_t capacity_words,
                                uint32_t opb_base,
                                const pvr_ta_layout_t *layout,
                                const pvr_ta_pass_layout_t *passes) {
    uint32_t *cursor = regions;
    uint32_t x;

    if(!regions || !layout || !passes ||
            !layout->pass_count ||
            layout->pass_count > PVR_MULTIPASS_MAX_PASSES ||
            capacity_words < layout->region_words || (opb_base & 3u) ||
            opb_base >= PVR_REGION_VRAM_LIMIT ||
            layout->total_opb_size > PVR_REGION_VRAM_LIMIT - opb_base) {
        errno = EINVAL;
        return -1;
    }

    /* The leading empty region is retained for byte-for-byte compatibility
       with the established KOS one-pass region array. */
    cursor[0] = PVR_REGION_KEEP_TILE;
    cursor[1] = PVR_REGION_EMPTY_POINTER;
    cursor[2] = PVR_REGION_EMPTY_POINTER;
    cursor[3] = PVR_REGION_EMPTY_POINTER;
    cursor[4] = PVR_REGION_EMPTY_POINTER;
    cursor[5] = PVR_REGION_EMPTY_POINTER;
    cursor += PVR_REGION_WORDS_PER_ENTRY;

    /* KOS has historically stored columns first. Region order is independent
       of the OPB tile number, which remains row-major in each list pointer. */
    for(x = 0; x < layout->tile_width; ++x) {
        uint32_t y;

        for(y = 0; y < layout->tile_height; ++y) {
            const uint32_t tile = layout->tile_width * y + x;
            size_t pass;

            for(pass = 0; pass < layout->pass_count; ++pass) {
                uint32_t control = (y << 8) | (x << 2);
                size_t list;

                if(pass)
                    control |= PVR_REGION_KEEP_DEPTH;

                if(passes[pass].presort)
                    control |= PVR_REGION_PRESORT;

                if(pass + 1 < layout->pass_count)
                    control |= PVR_REGION_KEEP_TILE;

                if(x + 1 == layout->tile_width &&
                        y + 1 == layout->tile_height &&
                        pass + 1 == layout->pass_count)
                    control |= PVR_REGION_LAST;

                cursor[0] = control;

                for(list = 0; list < 5; ++list) {
                    if(!passes[pass].opb_size[list]) {
                        cursor[list + 1] = PVR_REGION_EMPTY_POINTER;
                    }
                    else {
                        cursor[list + 1] = opb_base +
                            layout->pass_opb_offset[pass] +
                            layout->list_opb_offset[pass][list] +
                            passes[pass].opb_size[list] * tile;
                    }
                }

                cursor += PVR_REGION_WORDS_PER_ENTRY;
            }
        }
    }

    return 0;
}

int pvr_ta_frame_layout_calculate(pvr_ta_frame_layout_t *frame_layout,
                                  uint32_t bank_base, uint32_t bank_size,
                                  uint32_t vertex_size,
                                  uint32_t total_opb_size,
                                  uint32_t opb_overflow_count,
                                  size_t region_words,
                                  uint32_t frame_size) {
    uint32_t bank_limit;
    uint32_t opb_copies;
    uint32_t opb_allocation;
    uint32_t region_bytes;
    uint32_t cursor;

    if(!frame_layout || !bank_size || !vertex_size || !total_opb_size ||
            !region_words || !frame_size ||
            region_words > UINT32_MAX / sizeof(uint32_t)) {
        errno = EINVAL;
        return -1;
    }

    if(!add_u32(bank_base, bank_size, &bank_limit) ||
            !add_u32(opb_overflow_count, 1u, &opb_copies) ||
            !multiply_u32(total_opb_size, opb_copies, &opb_allocation)) {
        errno = EOVERFLOW;
        return -1;
    }

    region_bytes = (uint32_t)(region_words * sizeof(uint32_t));
    *frame_layout = (pvr_ta_frame_layout_t) {
        .vertex = bank_base,
        .vertex_size = vertex_size,
        .opb_size = total_opb_size,
        .tile_matrix_size = region_bytes,
        .frame_size = frame_size
    };

    if(!add_u32(bank_base, vertex_size, &cursor) ||
            !align_u32(cursor, PVR_BUFFER_ALIGNMENT, &cursor))
        goto overflow;

    frame_layout->opb = cursor;

    if(!add_u32(cursor, opb_allocation, &cursor) ||
            !align_u32(cursor, PVR_BUFFER_ALIGNMENT, &cursor))
        goto overflow;

    if(!add_u32(cursor, PVR_REGION_HEADER_BYTES,
                &frame_layout->tile_matrix) ||
            !add_u32(frame_layout->tile_matrix, region_bytes, &cursor) ||
            !align_u32(cursor, PVR_BUFFER_ALIGNMENT, &cursor))
        goto overflow;

    frame_layout->opb_end = frame_layout->opb + opb_allocation;
    frame_layout->frame = cursor;

    if(!add_u32(cursor, frame_size, &cursor) ||
            !align_u32(cursor, PVR_BUFFER_ALIGNMENT, &cursor))
        goto overflow;

    if(cursor > bank_limit) {
        errno = ENOSPC;
        return -1;
    }

    frame_layout->bank_end = cursor;
    return 0;

overflow:
    errno = EOVERFLOW;
    return -1;
}
