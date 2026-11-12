/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. Host integration, not GPU execution.
*/
#include "../../examples/dreamcast/pvr/sh4der_compact/effects.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

/* Any attempted native publication makes this memory-sink test fail. */
int pvr_prim(const void *p, size_t n) {
    (void)p; (void)n; assert(!"unexpected PVR submission"); return -1;
}
int pvr_list_prim(pvr_list_t l, const void *p, size_t n) {
    (void)l; return pvr_prim(p, n);
}
void pvr_mod_compile(pvr_mod_hdr_t *p, pvr_list_t l, uint32_t m, uint32_t c) {
    (void)p; (void)l; (void)m; (void)c; assert(!"unexpected modifier");
}

int main(void) {
    const float xyz[3][3] = {{0, 0, 0.008f}, {0.2f, 0.01f, 0.008f}, {0.35f, 0, 0.008f}};
    uint32_t words[12] = {PVR_CHUNK_VERTEX_XYZ | (10u << 16), 3u << 16};
    /* Negative strip length reverses first two references; index 0 repeats. */
    const uint16_t polygons[] = {PVR_CHUNK_STRIP_INDEX, 6, 1, 0x8004, 0, 1, 2, 0, 0xff};
    pvr_chunk_model_t model = {words, 12, polygons, 9, {0, 0, 0}, 1};
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t index[256];
    alignas(32) const matrix_t identity = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    alignas(32) pvr_vertex_t work[4], out[4];
    uint8_t weights[] = {0, 7, 15}, replacement[] = {15, 0, 7};
    effect_state_t state = {.weights = weights};
    sh4der_holo_params_t hp;
    sh4der_blade_params_t bp;
    sh4der_bloom_params_t gp;
    sh4der_bloom_mesh_t mesh;
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    const unsigned expected[] = {1, 0, 2, 0};
    memcpy(words + 2, xyz, sizeof(xyz)); words[11] = 0xff;
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, index, 256, &plan) == 0);
    sh4der_holo_params_default(&hp);
    assert(sh4der_holo_prepare(&state.holo, &hp, 0.7f, 1));
    for(unsigned rebind = 0; rebind < 2; ++rebind) {
        state.weights = rebind ? replacement : weights;
        assert(pvr_geometry_sink_init_memory(&sink, out, 4) == 0);
        assert(pvr_chunk_model_emit_prepared(&plan, &identity, &sink, work, 4,
            NULL, holo_vertex, &state, &result) == 0);
        assert(result.emitted_vertices == 4);
        for(unsigned i = 0; i < 4; ++i) {
            assert(out[i].argb == sh4der_holo_argb(&state.holo, state.weights[expected[i]]));
            assert(out[i].flags == (i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
        }
    }
    /* Changed destination capacity remains checked by the existing sink. */
    memset(out, 0x5a, sizeof(out));
    pvr_vertex_t before[4]; memcpy(before, out, sizeof(out));
    assert(pvr_geometry_sink_init_memory(&sink, out, 3) == 0);
    assert(pvr_chunk_model_emit_prepared(&plan, &identity, &sink, work, 4,
        NULL, holo_vertex, &state, &result) == -1);
    assert(errno == ENOSPC && !memcmp(out, before, sizeof(out)));

    sh4der_blade_params_default(&bp);
    assert(sh4der_blade_prepare(&state.blade, &bp, 19, 1));
    pvr_vertex_t vertex = {.flags = PVR_CMD_VERTEX_EOL, .x = 0.2f,
        .y = 0.01f, .z = 0.008f, .u = 0.3f, .v = 0.7f,
        .argb = 0, .oargb = 0x12345678};
    pvr_vertex_t saved = vertex;
    assert(blade_vertex(NULL, NULL, NULL, &vertex, &state) == 0);
    assert(vertex.argb != 0);
    saved.argb = vertex.argb;
    assert(!memcmp(&saved, &vertex, sizeof(vertex)));

    sh4der_bloom_params_default(&gp);
    const float eye[] = {0.2f, 0, 1};
    assert(sh4der_bloom_blade(&mesh, &gp, &bp, &state.blade, eye));
    alignas(32) pvr_vertex_t bloom[BLOOM_VERTEX_COUNT], projected[BLOOM_VERTEX_COUNT];
    bloom_vertices(&mesh, bloom);
    pvr_geometry_stream_t stream = {bloom, BLOOM_VERTEX_COUNT, sizeof(bloom[0])};
    assert(pvr_geometry_project(projected, BLOOM_VERTEX_COUNT, &stream, &identity, NULL) == 0);
    for(unsigned i = 0; i < BLOOM_VERTEX_COUNT; ++i) {
        assert(projected[i].flags == (i % 3 == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
        assert(isfinite(projected[i].x) && isfinite(projected[i].y));
        assert(projected[i].argb == bloom[i].argb);
    }
    puts("PASS: Compact indexed effects, rebind, preserved fields, capacity, bloom projection");
}
