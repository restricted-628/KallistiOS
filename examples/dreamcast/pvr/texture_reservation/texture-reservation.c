/* KallistiOS ##version##

   Contiguous caller-owned texture reservation example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_SIZE 64u
#define SURFACE_COUNT 3u

static alignas(32) uint16_t pixels[TEXTURE_SIZE * TEXTURE_SIZE];

static void build_texture(unsigned int index) {
    static const uint16_t colors[SURFACE_COUNT][2] = {
        { UINT16_C(0xf800), UINT16_C(0xffff) },
        { UINT16_C(0x07e0), UINT16_C(0xffff) },
        { UINT16_C(0x001f), UINT16_C(0xffff) }
    };
    size_t x;
    size_t y;

    for(y = 0; y < TEXTURE_SIZE; ++y) {
        for(x = 0; x < TEXTURE_SIZE; ++x) {
            unsigned int selector = ((x >> 3) ^ (y >> 3)) & 1u;

            pixels[y * TEXTURE_SIZE + x] = colors[index][selector];
        }
    }
}

static void draw_surface(const pvr_txr_surface_t *surface, float left) {
    pvr_poly_cxt_t context;
    pvr_material_t material;
    alignas(32) pvr_vertex_t vertices[4] = { 0 };
    size_t i;

    vertices[0].x = vertices[2].x = left;
    vertices[1].x = vertices[3].x = left + 128.0f;
    vertices[0].y = vertices[1].y = 176.0f;
    vertices[2].y = vertices[3].y = 304.0f;
    vertices[1].u = vertices[3].u = 1.0f;
    vertices[2].v = vertices[3].v = 1.0f;
    for(i = 0; i < 4; ++i) {
        vertices[i].flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vertices[i].z = 1.0f;
        vertices[i].argb = UINT32_C(0xffffffff);
    }

    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                     PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                     surface->width, surface->height, surface->vram,
                     PVR_FILTER_BILINEAR);
    assert(pvr_material_compile_polygon(&material, &context, 0) == 0);
    assert(pvr_material_submit(&material) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    pvr_txr_surface_t surfaces[SURFACE_COUNT];
    pvr_mem_reservation_t reservation;
    size_t offsets[SURFACE_COUNT];
    size_t total_bytes;
    pvr_pipeline_status_t status;
    unsigned int frame;
    size_t i;

    (void)argc;
    (void)argv;

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);

    for(i = 0; i < SURFACE_COUNT; ++i) {
        assert(pvr_txr_surface_init(&surfaces[i], TEXTURE_SIZE, TEXTURE_SIZE,
                                    PVR_TXR_SURFACE_RGB565,
                                    PVR_TXR_SURFACE_LINEAR, false) == 0);
    }
    assert(pvr_txr_surface_plan_reservation(
        surfaces, SURFACE_COUNT, 32, offsets, &total_bytes) == 0);
    assert(pvr_mem_reservation_alloc(&reservation, total_bytes) == 0);

    for(i = 0; i < SURFACE_COUNT; ++i) {
        assert(pvr_txr_surface_bind_reservation(
            &surfaces[i], &reservation, offsets[i]) == 0);
        assert(surfaces[i].vram ==
               (uint8_t *)reservation.base + offsets[i]);
        build_texture((unsigned int)i);
        assert(pvr_txr_surface_upload(&surfaces[i], pixels, sizeof(pixels),
                                      PVR_TXR_TRANSFER_CPU) == 0);
    }

    for(frame = 0; frame < 120u; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        draw_surface(&surfaces[0], 80.0f);
        draw_surface(&surfaces[1], 256.0f);
        draw_surface(&surfaces[2], 432.0f);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    for(i = 0; i < SURFACE_COUNT; ++i)
        pvr_txr_surface_release(&surfaces[i]);
    pvr_mem_reservation_release(&reservation);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (contiguous texture reservation)");
    puts("RESULT: PASS (contiguous texture reservation)");

    for(;;)
        thd_sleep(1000);
}
