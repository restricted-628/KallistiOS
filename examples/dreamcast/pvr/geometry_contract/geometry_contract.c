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
        .x = -0.8f, .y = -0.6f, .z = -2.0f,
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
    const pvr_geometry_stream_t stream = {
        source_vertices, 3, sizeof(pvr_vertex_t)
    };
    alignas(32) matrix_t projection;
    alignas(32) pvr_vertex_t projected[3];
    alignas(32) pvr_vertex_t copied[3];
    pvr_geometry_result_t result;
    pvr_geometry_sink_t memory_sink;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    (void)argc;
    (void)argv;

    assert(mat_perspective_build(&projection, &perspective) == 0);
    assert(pvr_geometry_project(projected, 3, &stream, &projection,
                                &result) == 0);
    assert(result.consumed_vertices == 3 && result.produced_vertices == 3);

    /* The same prepared stream can target caller memory or a PVR list. */
    assert(pvr_geometry_sink_init_memory(&memory_sink, copied, 3) == 0);
    assert(pvr_geometry_sink_emit(&memory_sink, projected, 3) == 0);
    assert(memory_sink.emitted_vertices == 3);
    assert(memcmp(copied, projected, sizeof(copied)) == 0);

    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.04f);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.shading = PVR_SHADE_GOURAUD;
    pvr_poly_compile(&header, &context);

    puts("RESULT: PASS (caller-owned PVR geometry contract)");

    for(;;) {
        pvr_geometry_sink_t pvr_sink;

        pvr_wait_ready();
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_prim(&header, sizeof(header)) == 0);
        assert(pvr_geometry_sink_init_current(&pvr_sink) == 0);
        assert(pvr_geometry_sink_emit(&pvr_sink, copied, 3) == 0);
        assert(pvr_sink.emitted_vertices == 3);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }
}
