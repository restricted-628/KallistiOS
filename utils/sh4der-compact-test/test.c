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

static int base_lighting(const pvr_chunk_render_state_t *s,
    const pvr_chunk_vertex_attributes_t *a, const pvr_chunk_strip_attributes_t *t,
    pvr_vertex_t *v, void *d) {
    (void)s; (void)a; (void)t; (void)d;
    v->argb = 0x7f804020;
    return 0;
}

static int base_failure(const pvr_chunk_render_state_t *s,
    const pvr_chunk_vertex_attributes_t *a, const pvr_chunk_strip_attributes_t *t,
    pvr_vertex_t *v, void *d) {
    (void)s; (void)a; (void)t; (void)v; (void)d;
    errno = EDOM; return -1;
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
    sh4der_kos_binding_t binding;
    sh4der_kos_sidecars_t sidecars = {.holo_weights = weights, .count = 3};
    sh4der_kos_effect_t shader = {
        .binding = &binding, .holo = &state.holo, .mode = SH4DER_KOS_REPLACE
    };
    for(unsigned rebind = 0; rebind < 2; ++rebind) {
        state.weights = rebind ? replacement : weights;
        sidecars.holo_weights = state.weights;
        assert(sh4der_kos_bind(&binding, &view, &sidecars) == 0);
        assert(pvr_geometry_sink_init_memory(&sink, out, 4) == 0);
        assert(pvr_chunk_model_emit_prepared(&plan, &identity, &sink, work, 4,
            NULL, sh4der_kos_vertex, &shader, &result) == 0);
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
        NULL, sh4der_kos_vertex, &shader, &result) == -1);
    assert(errno == ENOSPC && !memcmp(out, before, sizeof(out)));

    sh4der_blade_params_default(&bp);
    assert(sh4der_blade_prepare(&state.blade, &bp, 19, 1));
    pvr_vertex_t vertex = {.flags = PVR_CMD_VERTEX_EOL, .x = 0.2f,
        .y = 0.01f, .z = 0.008f, .u = 0.3f, .v = 0.7f,
        .argb = 0, .oargb = 0x12345678};
    pvr_vertex_t saved = vertex;
    shader.holo = NULL;
    shader.blade = &state.blade;
    shader.blade_rgb[0] = 0.15f; shader.blade_rgb[1] = 0.65f; shader.blade_rgb[2] = 1;
    shader.blade_alpha = 255;
    pvr_chunk_vertex_attributes_t attributes = {.index = 0};
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb != 0);
    saved.argb = vertex.argb;
    assert(!memcmp(&saved, &vertex, sizeof(vertex)));

    /* Bind failure leaves the previous admitted binding intact. */
    sh4der_kos_binding_t old_binding = binding;
    sidecars.count = 2;
    assert(sh4der_kos_bind(&binding, &view, &sidecars) == -1);
    assert(errno == ENOSPC && !memcmp(&binding, &old_binding, sizeof(binding)));
    sidecars.count = 3;
    uint8_t ao[] = {0, 128, 255};
    sidecars.ao = ao;
    assert(sh4der_kos_bind(&binding, &view, &sidecars) == 0);
    shader.blade = NULL;
    vertex.argb = 0x7fffffff;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7f000000 && vertex.oargb == 0x12345678);
    attributes.index = 1;
    vertex.argb = 0x7fffffff;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7f808080);
    shader.base_prepare = base_lighting;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7f402010);
    shader.base_prepare = NULL;
    attributes.index = 2;
    shader.holo = &state.holo;
    shader.mode = SH4DER_KOS_ADD;
    vertex.argb = 0x7fffffff;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7fffffff);
    shader.mode = SH4DER_KOS_MODULATE;
    vertex.argb = 0x7f000000;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7f000000);

    /* Prepared posed shadow, shared space, self exclusion and matrix state. */
    sh4dow_frame_t shadow;
    sh4dow_params_t sp;
    sh4dow_params_default(&sp);
    sp.dark = 0.5f; sp.length = 10; sp.hold = 1; sp.fade_in = 0;
    sh4dow_limb_t limb = {.position = {0, 0, 0}, .radius = 1, .bone = 3};
    const float direction[3] = {0, 0, 1};
    assert(sh4dow_prepare(&shadow, &sp, &limb, 1, direction) == 1);
    uint16_t bones[3] = {3, 4, 4};
    sidecars.ao = NULL; sidecars.bones = bones;
    assert(sh4der_kos_bind(&binding, &view, &sidecars) == 0);
    shader.holo = NULL; shader.shadow = &shadow;
    shader.shadow_axes = sh4dow_axes(&shadow);
    attributes.index = 0;
    vertex.x = vertex.y = 0; vertex.z = 1; vertex.argb = 0x7fffffff;
    shz_mat4x4_t matrix_before, matrix_after;
    float sentinel[16];
    for(unsigned i = 0; i < 16; ++i) sentinel[i] = (float)i + 0.25f;
    memcpy(&matrix_before, sentinel, sizeof(matrix_before));
    shz_xmtrx_load_4x4(&matrix_before);
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7fffffff);
    attributes.index = 1;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == 0);
    assert(vertex.argb == 0x7f808080);
    shz_xmtrx_store_4x4(&matrix_after);
    assert(!memcmp(&matrix_before, &matrix_after, sizeof(matrix_before)));
    shader.base_prepare = base_failure;
    saved = vertex;
    assert(sh4der_kos_vertex(NULL, &attributes, NULL, &vertex, &shader) == -1);
    assert(errno == EDOM && !memcmp(&saved, &vertex, sizeof(vertex)));
    shader.base_prepare = NULL;

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
