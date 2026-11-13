/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. See README.md for dependency credits.
*/
#include <kos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include "effects.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* Keep side effects even in NDEBUG/release builds. */
#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "SH4DER example failed: %s at line %d\n", #condition, __LINE__); \
        abort(); \
    } \
} while(0)

static const float positions[4][3] = {
    {0.0f, -0.035f, 0.008f}, {0.35f, -0.035f, 0.008f},
    {0.0f, 0.035f, 0.008f}, {0.35f, 0.035f, 0.008f}
};
static const uint16_t polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0xffff, 0xffff,
    PVR_CHUNK_STRIP_INDEX, 6, 1, 4, 0, 1, 2, 3, 0xff
};
static pvr_poly_hdr_t header;

static int begin_strip(const pvr_chunk_render_state_t *state,
                       const pvr_chunk_strip_view_t *strip, void *data) {
    (void)state; (void)strip; (void)data;
    /* Same current-list route as the vertex sink, including buffered lists. */
    return pvr_prim(&header, sizeof(header));
}

int main(int argc, char **argv) {
    uint32_t words[15] = { PVR_CHUNK_VERTEX_XYZ | (13u << 16), 4u << 16 };
    pvr_chunk_model_t model = { words, 15, polygons,
        sizeof(polygons) / sizeof(polygons[0]), {0.175f, 0, 0.008f}, 0.2f };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    static pvr_chunk_vertex_index_entry_t index[256];
    uint8_t weights[4];
    effect_state_t effect = { .weights = weights };
    sh4der_blade_params_t blade;
    sh4der_bloom_params_t bloom;
    sh4der_holo_params_t holo;
    sh4der_bloom_mesh_t mesh;
    const float eye[3] = {0.2f, 0.0f, 1.0f};
    static alignas(32) pvr_vertex_t workspace[4];
    static alignas(32) pvr_vertex_t glow[BLOOM_VERTEX_COUNT];
    static alignas(32) pvr_vertex_t projected[BLOOM_VERTEX_COUNT];
    alignas(32) matrix_t projection = {
        {600, 0, 0, 0}, {0, -600, 0, 0}, {0, 0, 1, 0}, {210, 180, 0, 1}
    };
    pvr_geometry_sink_t sink;
    pvr_geometry_stream_t stream = {glow, BLOOM_VERTEX_COUNT, sizeof(glow[0])};
    pvr_poly_cxt_t context;
    (void)argc; (void)argv;

    memcpy(words + 2, positions, sizeof(positions));
    words[14] = 0xff;
    CHECK(pvr_chunk_model_open(&model, &view) == 0);
    CHECK(pvr_chunk_model_plan_build(&view, index, 256, &plan) == 0);
    sh4der_blade_params_default(&blade);
    sh4der_bloom_params_default(&bloom);
    sh4der_holo_params_default(&holo);
    holo.center[0] = 0.05f;
    holo.center[1] = 0;
    holo.center[2] = 0.008f;
    holo.inner = 0.02f;
    holo.outer = 0.25f;
    /* This fixed model's admitted references are exactly 0..3. For cooked
       assets validate sidecar coverage/order at bind, and repeat on rebind. */
    sh4der_holo_weights(&holo, &positions[0][0], sizeof(positions[0]), weights, 4);
    sh4der_kos_binding_t binding;
    const sh4der_kos_sidecars_t sidecars = {.holo_weights = weights, .count = 4};
    CHECK(sh4der_kos_bind(&binding, &view, &sidecars) == 0);
    sh4der_kos_effect_t blade_effect = {
        .binding = &binding, .blade = &effect.blade,
        .blade_rgb = {0.15f, 0.65f, 1.0f}, .blade_alpha = 255,
        .mode = SH4DER_KOS_REPLACE
    };
    sh4der_kos_effect_t holo_effect = {
        .binding = &binding, .holo = &effect.holo, .mode = SH4DER_KOS_REPLACE
    };
    CHECK(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.01f, 0.01f, 0.025f);
    pvr_poly_cxt_col(&context, PVR_LIST_TR_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    context.blend.src = PVR_BLEND_SRCALPHA;
    context.blend.dst = PVR_BLEND_ONE;
    pvr_poly_compile(&header, &context);
    CHECK(pvr_geometry_sink_init_current(&sink) == 0);
    const uint64_t start = timer_ms_gettime64();

    while(timer_ms_gettime64() - start < 10000) {
        uint32_t tick = (uint32_t)((timer_ms_gettime64() - start) * 30 / 1000);
        CHECK(sh4der_blade_prepare(&effect.blade, &blade, tick, 1.0f));
        CHECK(sh4der_holo_prepare(&effect.holo, &holo, effect.blade.pulse, 1.0f));
        CHECK(sh4der_bloom_blade(&mesh, &bloom, &blade, &effect.blade, eye));
        CHECK(bloom_vertices(&mesh, glow) == BLOOM_VERTEX_COUNT);
        projection[3][1] = 180;
        CHECK(pvr_geometry_project(projected, BLOOM_VERTEX_COUNT, &stream,
                                     &projection, NULL) == 0);
        CHECK(pvr_wait_ready() == 0);
        pvr_scene_begin();
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        CHECK(pvr_chunk_model_emit_prepared(&plan, &projection, &sink,
            workspace, 4, begin_strip, sh4der_kos_vertex, &blade_effect, NULL) == 0);
        CHECK(pvr_prim(&header, sizeof(header)) == 0);
        CHECK(pvr_geometry_sink_emit(&sink, projected, BLOOM_VERTEX_COUNT) == 0);
        projection[3][1] = 320;
        CHECK(pvr_chunk_model_emit_prepared(&plan, &projection, &sink,
            workspace, 4, begin_strip, sh4der_kos_vertex, &holo_effect, NULL) == 0);
        CHECK(pvr_list_finish() == 0);
        CHECK(pvr_scene_finish() == 0);
    }
    CHECK(pvr_wait_render_done() == 0);
    CHECK(pvr_shutdown() == 0);
    return 0;
}
