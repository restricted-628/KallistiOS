/* KallistiOS ##version##

   Caller-owned PVR geometry contract example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static const pvr_vertex_t source_vertices[3] = {
    {
        .flags = PVR_CMD_VERTEX,
        .x = -5.0f, .y = -0.6f, .z = -2.0f,
        .u = 0.0f, .v = 0.0f,
        .argb = UINT32_C(0xffff4040), .oargb = 0
    },
    {
        .flags = PVR_CMD_VERTEX,
        .x = 0.0f, .y = 0.8f, .z = -2.0f,
        .u = 0.0f, .v = 0.0f,
        .argb = UINT32_C(0xff40ff40), .oargb = 0
    },
    {
        .flags = PVR_CMD_VERTEX_EOL,
        .x = 0.8f, .y = -0.6f, .z = -2.0f,
        .u = 0.0f, .v = 0.0f,
        .argb = UINT32_C(0xff4040ff), .oargb = 0
    }
};

int main(int argc, char **argv) {
    const mat_perspective_desc_t perspective = {
        320.0f, 240.0f, 1.0f, 1.0f, 100.0f
    };
    alignas(32) matrix_t projection;
    alignas(32) pvr_vertex_t clipped[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_vertex_t copied[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_frustum_clip_result_t clip_result;
    pvr_frustum_t frustum;
    pvr_geometry_sink_t memory_sink;
    pvr_material_t material;
    pvr_poly_cxt_t context;
    unsigned int frame;

    (void)argc;
    (void)argv;

    /* Leave a visible diagnostic color if a pre-PVR contract assertion ever
       stops the example before rendering begins. */
    vid_clear(96, 0, 0);

    assert(mat_perspective_build(&projection, &perspective) == 0);
    assert(pvr_frustum_init(&frustum, &projection, 0.0f, 0.0f,
                            640.0f, 480.0f, 2.0f, 101.0f) == 0);
    assert(pvr_frustum_clip_triangle(clipped,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     source_vertices, &frustum,
                                     PVR_FRUSTUM_CLIP_ALL,
                                     &clip_result) == 0);
    assert(clip_result.polygon_vertices == 4);
    assert(clip_result.output_vertices == 6);

    /* The clipped triangles can target caller memory or a PVR list. */
    assert(pvr_geometry_sink_init_memory(&memory_sink, copied,
                                         PVR_FRUSTUM_CLIP_MAX_VERTICES) == 0);
    assert(pvr_geometry_sink_emit(&memory_sink, clipped,
                                  clip_result.output_vertices) == 0);
    assert(memory_sink.emitted_vertices == clip_result.output_vertices);
    assert(memcmp(copied, clipped, clip_result.output_vertices *
                                  sizeof(*copied)) == 0);

    vid_clear(0, 96, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.05f, 0.12f, 0.30f);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.shading = PVR_SHADE_GOURAUD;
    assert(pvr_material_compile_polygon(&material, &context, 0) == 0);

    for(frame = 0; frame < 120; ++frame) {
        pvr_geometry_sink_t pvr_sink;

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_material_submit(&material) == 0);
        assert(pvr_geometry_sink_init_current(&pvr_sink) == 0);
        assert(pvr_geometry_sink_emit(&pvr_sink, copied,
                                      clip_result.output_vertices) == 0);
        assert(pvr_sink.emitted_vertices == clip_result.output_vertices);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_shutdown() == 0);
    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2,
                   vid_mode->width, 1,
                   "RESULT: PASS (material + frustum + geometry)");
    puts("RESULT: PASS (checked PVR material, frustum, and geometry contract)");

    for(;;)
        thd_sleep(1000);
}
