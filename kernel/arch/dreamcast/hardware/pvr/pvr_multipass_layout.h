/* KallistiOS ##version##

   pvr_multipass_layout.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __PVR_MULTIPASS_LAYOUT_H
#define __PVR_MULTIPASS_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PVR_MULTIPASS_MAX_PASSES
#define PVR_MULTIPASS_MAX_PASSES 8u
#endif
#define PVR_REGION_WORDS_PER_ENTRY 6u
#define PVR_REGION_HEADER_BYTES 0x48u

typedef struct pvr_ta_pass_layout {
    uint32_t opb_size[5];
    bool presort;
} pvr_ta_pass_layout_t;

typedef struct pvr_ta_layout {
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tile_count;
    size_t pass_count;
    uint32_t pass_opb_offset[PVR_MULTIPASS_MAX_PASSES];
    uint32_t pass_opb_size[PVR_MULTIPASS_MAX_PASSES];
    uint32_t list_opb_offset[PVR_MULTIPASS_MAX_PASSES][5];
    uint32_t total_opb_size;
    size_t region_words;
} pvr_ta_layout_t;

typedef struct pvr_ta_frame_layout {
    uint32_t vertex;
    uint32_t vertex_size;
    uint32_t opb;
    uint32_t opb_size;
    uint32_t opb_end;
    uint32_t tile_matrix;
    uint32_t tile_matrix_size;
    uint32_t frame;
    uint32_t frame_size;
    uint32_t bank_end;
} pvr_ta_frame_layout_t;

int pvr_ta_layout_calculate(pvr_ta_layout_t *layout, uint32_t tile_width,
                            uint32_t tile_height,
                            const pvr_ta_pass_layout_t *passes,
                            size_t pass_count);

int pvr_ta_layout_build_regions(uint32_t *regions, size_t capacity_words,
                                uint32_t opb_base,
                                const pvr_ta_layout_t *layout,
                                const pvr_ta_pass_layout_t *passes);

int pvr_ta_frame_layout_calculate(pvr_ta_frame_layout_t *frame_layout,
                                  uint32_t bank_base, uint32_t bank_size,
                                  uint32_t vertex_size,
                                  uint32_t total_opb_size,
                                  uint32_t opb_overflow_count,
                                  size_t region_words,
                                  uint32_t frame_size);

#endif /* __PVR_MULTIPASS_LAYOUT_H */
