/* KallistiOS ##version##
   Portal integration fixture, with separate coverage and depth-clear routes.
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/sh4zam.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portal-scene.h"

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

#define CHECK(test) do { \
    if(!(test)) { \
        printf("FAIL line %d: %s errno=%d\n", __LINE__, #test, errno); \
        exit(1); \
    } \
} while(0)

static portal_scene_t scene;
static alignas(32) uint8_t staging[3][4096];

static unsigned check_pixels(void) {
    static const struct { unsigned x, y; uint16_t color; } samples[] = {
        { 10, 10, 0x001f }, { 80, 200, 0xf800 }, { 192, 240, 0xf800 },
        { 194, 240, 0x07e0 }, { 320, 240, 0x07ff }, { 446, 240, 0xf81f },
        { 448, 240, 0xf800 }, { 400, 128, 0xf800 }, { 400, 352, 0xf800 }
    };
    vid_framebuffer_info_t fb;
    unsigned failures = 0;
    CHECK(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED, &fb) == 0);
    CHECK(fb.pixel_mode == PM_RGB565 && fb.width == 640 && fb.height == 480);
    for(size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        const volatile uint16_t *address = (const volatile uint16_t *)
            ((const uint8_t *)fb.address + samples[i].y * fb.stride_bytes +
             samples[i].x * 2);
        uint16_t actual = *address;
        printf("pixel %u,%u: %04x expected %04x\n", samples[i].x,
               samples[i].y, actual, samples[i].color);
        failures += actual != samples[i].color;
    }
    return failures;
}

static unsigned run_case(bool clear_depth, unsigned mode) {
    const pvr_init_params_t params = {
        .vertex_buf_size = 512 * 1024, .opb_overflow_count = 1,
        .dma_enabled = mode != 0
    };
    const pvr_pass_config_t passes[3] = {
        { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } },
        { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } },
        { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } }
    };
    const pvr_pass_depth_t depth[3] = {
        PVR_PASS_DEPTH_CLEAR,
        clear_depth ? PVR_PASS_DEPTH_CLEAR : PVR_PASS_DEPTH_PRESERVE,
        PVR_PASS_DEPTH_PRESERVE
    };
    shz_mat4x4_t before, after;
    pvr_poly_cxt_t context;
    pvr_material_t material;
    pvr_pipeline_status_t status;
    printf("portal case: %s; mode=%u (direct/DMA/hybrid)\n",
           clear_depth ? "depth-clear" : "disjoint-coverage", mode);
    shz_xmtrx_init_translation(7, 8, 9);
    shz_xmtrx_store_4x4(&before);
    CHECK(portal_scene_build(&scene, clear_depth, true) == 0);
    shz_xmtrx_store_4x4(&after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    printf("geometry/XMTRX PASS; vertices=%u/%u/%u\n",
           (unsigned)scene.count[0], (unsigned)scene.count[1],
           (unsigned)scene.count[2]);

    CHECK(pvr_init_multipass_depth(&params, passes, depth, 3) == 0);
    if(mode) {
        for(size_t pass = 0; pass < 3; ++pass) {
            /* The staging allocation is divided into two frame halves.
               Include one header and the list terminator in each half. */
            CHECK(scene.count[pass] * sizeof(pvr_vertex_t) + 64 <=
                  sizeof(staging[pass]) / 2);
            CHECK(pvr_set_pass_vertbuf_checked(pass, PVR_LIST_OP_POLY,
                staging[pass], sizeof(staging[pass]), NULL) == 0);
        }
    }
    pvr_set_bg_color(0, 0, 1);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    context.depth.write = PVR_DEPTHWRITE_ENABLE;
    CHECK(pvr_material_compile_polygon(&material, &context, 0) == 0);

    for(unsigned frame = 0; frame < 12; ++frame) {
        CHECK(pvr_wait_ready() == 0);
        pvr_scene_begin();
        for(size_t pass = 0; pass < 3; ++pass) {
            pvr_geometry_sink_t sink;
            CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
            CHECK(pvr_material_submit(&material) == 0);
            CHECK(pvr_geometry_sink_init_current(&sink) == 0);
            CHECK(pvr_geometry_sink_emit(&sink, scene.vertices[pass],
                                         scene.count[pass]) == 0);
            CHECK(sink.emitted_vertices == scene.count[pass]);
            CHECK(pvr_list_finish() == 0);
            if(pass + 1 < 3) {
                if(mode == 2)
                    CHECK(pvr_list_flush(PVR_LIST_OP_POLY) == 0);
                CHECK(pvr_scene_next_pass() == 0);
            }
        }
        CHECK(pvr_scene_finish() == 0);
    }
    CHECK(pvr_wait_ready() == 0);
    CHECK(pvr_wait_render_done() == 0);
    vid_waitvbl();
    CHECK(pvr_get_pipeline_status(&status) == 0);
    CHECK(status.faults.mask == PVR_FAULT_NONE);
    unsigned failures = check_pixels();
    printf("portal case pixel mismatches: %u/9\n", failures);
    CHECK(pvr_shutdown() == 0);
    return failures;
}

int main(void) {
    unsigned coverage_failures = 0, clear_failures = 0;
    for(unsigned mode = 0; mode < 3; ++mode)
        coverage_failures += run_case(false, mode);
    printf("disjoint-coverage result: %s (%u/27 mismatches)\n",
           coverage_failures ? "FAIL" : "PASS", coverage_failures);
    for(unsigned mode = 0; mode < 3; ++mode)
        clear_failures += run_case(true, mode);
    printf("depth-clear result: %s (%u/27 mismatches)\n",
           clear_failures ? "FAIL" : "PASS", clear_failures);
    CHECK(coverage_failures + clear_failures == 0);
    puts("RESULT: PASS (rectangular portal composition; not hardware proof)");
    return 0;
}
