/* KallistiOS ##version##

   Per-texture VQ palette example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define LOGICAL_SIZE 64u
#define HARDWARE_SIZE (LOGICAL_SIZE * 2u)
#define VQ_CODEBOOK_BYTES 2048u
#define INDEX_BYTES (LOGICAL_SIZE * LOGICAL_SIZE)

static alignas(32) uint8_t encoded[VQ_CODEBOOK_BYTES + INDEX_BYTES];
static uint16_t colors[256];

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

static void build_texture(void) {
    uint8_t *indices = encoded + VQ_CODEBOOK_BYTES;
    uint32_t x;
    uint32_t y;
    uint32_t i;

    for(i = 0; i < 256u; ++i) {
        uint16_t red = (uint16_t)((i & 31u) << 11);
        uint16_t green = (uint16_t)(((i >> 2) & 63u) << 5);
        uint16_t blue = (uint16_t)((255u - i) & 31u);

        colors[i] = red | green | blue;
    }
    assert(pvr_txr_vq_palette_build(encoded, VQ_CODEBOOK_BYTES, colors,
                                     256) == 0);

    for(y = 0; y < LOGICAL_SIZE; ++y) {
        for(x = 0; x < LOGICAL_SIZE; ++x) {
            indices[twiddled_index(x, y)]
                = (uint8_t)(((x >> 2) + (y >> 2) * 16u) & 255u);
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
    uint32_t format;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.04f);
    build_texture();

    assert(pvr_txr_surface_alloc(&surface, HARDWARE_SIZE, HARDWARE_SIZE,
                                 PVR_TXR_SURFACE_RGB565,
                                 PVR_TXR_SURFACE_VQ, false) == 0);
    assert(surface.codebook_size == VQ_CODEBOOK_BYTES);
    assert(surface.data_size == INDEX_BYTES);
    assert(surface.byte_size == sizeof(encoded));
    assert(pvr_txr_surface_upload(&surface, encoded, sizeof(encoded),
                                  PVR_TXR_TRANSFER_DMA) == 0);

    format = pvr_txr_surface_pvr_format(&surface);
    assert(format != UINT32_MAX);
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, format,
                     surface.width, surface.height, surface.vram,
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
