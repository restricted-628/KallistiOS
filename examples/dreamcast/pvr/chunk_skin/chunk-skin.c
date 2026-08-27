/* KallistiOS ##version##

   Explicit compact-model skinning example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t model_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    UINT32_C(0x43480000), UINT32_C(0x43200000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x43dc0000), UINT32_C(0x43200000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x43a00000), UINT32_C(0x43b40000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const pvr_chunk_skin_influence_t model_influences[] = {
    { 0, { 0, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 1, { 1, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 2, { 0, 1, 0, 0 }, { 32768, 32767, 0, 0 }, 0 }
};

static const pvr_chunk_model_t model = {
    model_vertices, sizeof(model_vertices) / sizeof(model_vertices[0]),
    model_polygons, sizeof(model_polygons) / sizeof(model_polygons[0]),
    { 320.0f, 260.0f, 0.0f }, 180.0f
};

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

typedef struct render_context {
    pvr_poly_hdr_t header;
    pvr_chunk_skin_pose_t pose;
} render_context_t;

static void identity(matrix_t *matrix) {
    memcpy(matrix, &screen_identity, sizeof(*matrix));
}

static void normal_identity(pvr_normal_matrix_t *matrix) {
    memset(matrix, 0, sizeof(*matrix));
    matrix->column[0][0] = 1.0f;
    matrix->column[1][1] = 1.0f;
    matrix->column[2][2] = 1.0f;
}

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    render_context_t *context = data;

    (void)strip;
    pvr_prim(&context->header, sizeof(context->header));
    return 0;
}

static int resolve_vertex(uint16_t source_index,
                          pvr_deform_vertex_t *deformed, void *data) {
    render_context_t *context = data;

    return pvr_chunk_skin_pose_vertex_get(&context->pose, source_index,
                                          deformed);
}

static int prepare_vertex(
    const pvr_chunk_render_state_t *state,
    uint16_t source_index, const pvr_deform_vertex_t *deformed,
    pvr_vertex_t *vertex, void *data) {
    float light;
    uint32_t intensity;

    (void)state;
    (void)source_index;
    (void)data;
    light = 0.25f + 0.75f * fmaxf(0.0f, deformed->normal.z);
    intensity = (uint32_t)(light * 255.0f);
    vertex->argb = UINT32_C(0xff000000) | (intensity << 16) |
                   ((intensity * 3u / 4u) << 8) | intensity / 2u;
    vertex->oargb = 0;
    return 0;
}

int main(int argc, char **argv) {
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    const pvr_chunk_skin_t skin = {
        model_influences,
        sizeof(model_influences) / sizeof(model_influences[0]), 2
    };
    pvr_chunk_skin_requirements_t skin_requirements;
    pvr_chunk_skin_binding_t binding;
    uint32_t dense_lookup[256];
    alignas(32) uint8_t source_workspace[192];
    pvr_chunk_skin_source_t source;
    pvr_chunk_cache_requirements_t cache_requirements;
    alignas(32) uint8_t cache_storage[512];
    pvr_chunk_model_cache_t cache;
    alignas(8) matrix_t position_matrices[2];
    pvr_normal_matrix_t normal_matrices[2];
    pvr_skin_palette_t palette = {
        position_matrices, normal_matrices, 2
    };
    alignas(32) pvr_deform_vertex_t deformed[3];
    pvr_deform_result_t deform_result;
    pvr_poly_cxt_t polygon_context;
    render_context_t render_context;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t render_workspace[3];
    pvr_chunk_cache_result_t render_result;
    pvr_pipeline_status_t pipeline;
    unsigned frame;

    (void)argc;
    (void)argv;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_skin_query(&plan, &skin_requirements) == 0);
    assert(skin_requirements.lookup_entries == 256 &&
           skin_requirements.source_vertices == 3 &&
           skin_requirements.source_bytes == sizeof(source_workspace));
    assert(pvr_chunk_skin_bind(&plan, &skin, dense_lookup, 256,
                               &binding) == 0);
    assert(pvr_chunk_skin_source_build(&binding, source_workspace,
                                       sizeof(source_workspace),
                                       &source) == 0);
    assert(pvr_chunk_model_cache_query(&plan, &cache_requirements) == 0);
    assert(cache_requirements.bytes <= sizeof(cache_storage));
    assert(pvr_chunk_model_cache_build(&plan, cache_storage,
                                       sizeof(cache_storage), NULL, NULL,
                                       &cache) == 0);

    identity(position_matrices + 0);
    identity(position_matrices + 1);
    normal_identity(normal_matrices + 0);
    normal_identity(normal_matrices + 1);

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    pvr_poly_cxt_col(&polygon_context, PVR_LIST_OP_POLY);
    polygon_context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&render_context.header, &polygon_context);
    render_context.pose.binding = &binding;
    render_context.pose.vertices = deformed;
    render_context.pose.vertex_count = 3;
    assert(pvr_geometry_sink_init_current(&sink) == 0);

    for(frame = 0; frame < 120u; ++frame) {
        position_matrices[1][3][0] = 48.0f * sinf((float)frame * 0.08f);
        assert(pvr_chunk_skin_apply(&source, &palette, deformed, 3,
                                    &deform_result) == 0);
        assert(deform_result.deformed_vertices == 3);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_cache_emit(
            &cache, &screen_identity, &sink, render_workspace, 3,
            begin_strip, resolve_vertex, prepare_vertex,
            &render_context, &render_result) == 0);
        assert(render_result.emitted_strips == 1 &&
               render_result.emitted_vertices == 3);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&pipeline) == 0);
    assert(pipeline.faults.mask == PVR_FAULT_NONE);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (explicit compact skinning)");
    puts("RESULT: PASS (explicit compact skinning)");

    for(;;)
        thd_sleep(1000);
}
