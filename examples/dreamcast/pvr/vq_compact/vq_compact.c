/* KallistiOS ##version##

   Compact-codebook VQ example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_SIZE 64u
#define CODEBOOK_ENTRIES 16u
#define CODEBOOK_BYTES (CODEBOOK_ENTRIES * 8u)
#define INDEX_SIZE (TEXTURE_SIZE / 2u)
#define INDEX_BYTES (INDEX_SIZE * INDEX_SIZE)
#define ENCODED_BYTES (CODEBOOK_BYTES + INDEX_BYTES)

static alignas(32) uint8_t encoded[ENCODED_BYTES];

static uint32_t spread_bits(uint32_t value) {
    value = (value | (value << 8)) & UINT32_C(0x00ff00ff);
    value = (value | (value << 4)) & UINT32_C(0x0f0f0f0f);
    value = (value | (value << 2)) & UINT32_C(0x33333333);
    value = (value | (value << 1)) & UINT32_C(0x55555555);
    return value;
}

static size_t twiddled_index(uint32_t x, uint32_t y) {
    return spread_bits(y) | (spread_bits(x) << 1);
}

static void build_texture(int index_base) {
    uint16_t *codebook = (uint16_t *)encoded;
    uint8_t *indices = encoded + CODEBOOK_BYTES;
    uint32_t entry;
    uint32_t texel;
    uint32_t x;
    uint32_t y;

    memset(encoded, 0, sizeof(encoded));
    for(entry = 0; entry < CODEBOOK_ENTRIES; ++entry) {
        uint16_t red = (uint16_t)((entry & 3u) * 10u << 11);
        uint16_t green = (uint16_t)(((entry >> 2) & 3u) * 20u << 5);
        uint16_t blue = (uint16_t)((15u - entry) * 2u);
        uint16_t color = red | green | blue;

        for(texel = 0; texel < 4u; ++texel)
            codebook[entry * 4u + texel] = color;
    }

    for(y = 0; y < INDEX_SIZE; ++y) {
        for(x = 0; x < INDEX_SIZE; ++x) {
            uint32_t pattern = ((x >> 2) + (y >> 2)) & 15u;

            indices[twiddled_index(x, y)] = (uint8_t)(index_base + pattern);
        }
    }
}

static void draw_texture(const pvr_poly_hdr_t *header) {
    alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = 160.0f, .y = 80.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX, .x = 480.0f, .y = 80.0f, .z = 1.0f,
          .u = 1.0f, .v = 0.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX, .x = 160.0f, .y = 400.0f, .z = 1.0f,
          .u = 0.0f, .v = 1.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 480.0f, .y = 400.0f, .z = 1.0f,
          .u = 1.0f, .v = 1.0f, .argb = UINT32_C(0xffffffff) }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    pvr_txr_surface_t surface;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_pipeline_status_t status;
    pvr_ptr_t texture_address;
    uint32_t format;
    int index_base;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.04f);

    assert(pvr_txr_surface_alloc_vq(&surface, TEXTURE_SIZE, TEXTURE_SIZE,
                                    PVR_TXR_SURFACE_RGB565,
                                    CODEBOOK_ENTRIES, false) == 0);
    assert(surface.codebook_size == CODEBOOK_BYTES);
    assert(surface.data_size == INDEX_BYTES);
    assert(surface.byte_size == sizeof(encoded));
    index_base = pvr_txr_surface_get_vq_index_base(&surface);
    assert(index_base == 240);
    build_texture(index_base);

    assert(pvr_txr_surface_upload(&surface, encoded, sizeof(encoded),
                                  PVR_TXR_TRANSFER_DMA) == 0);
    assert(pvr_txr_surface_get_texture_address(&surface,
                                               &texture_address) == 0);
    assert((uintptr_t)surface.vram - (uintptr_t)texture_address
           == 2048u - CODEBOOK_BYTES);

    format = pvr_txr_surface_pvr_format(&surface);
    assert(format != UINT32_MAX);
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, format,
                     surface.width, surface.height, texture_address,
                     PVR_FILTER_NEAREST);
    pvr_poly_compile(&header, &context);

    for(frame = 0; frame < 120u; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        draw_texture(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    pvr_txr_surface_release(&surface);
    assert(pvr_shutdown() == 0);
    return 0;
}
