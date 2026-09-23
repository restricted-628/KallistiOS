/* KallistiOS ##version##

   Explicit compact-model skinning example.
   Copyright (C) 2026 Joseph Black
*/

#ifndef CHUNK_SKIN_HOST
#include <kos.h>
#endif
#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_skin_prepared.h>

#include <assert.h>
#include <math.h>
#include <sh4zam/shz_trig.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CHUNK_SKIN_HOST
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

#define JOINT_COUNT 4u
#ifdef CHUNK_SKIN_COMPACT
typedef pvr_skin_compact_joint_t demo_joint_t;
typedef pvr_skin_compact_palette_t demo_palette_t;
#define demo_palette_prepare pvr_skin_palette_prepare_compact
#define demo_skin_apply pvr_skin_apply_compact
#define DEMO_PALETTE "compact"
#else
typedef pvr_skin_prepared_joint_t demo_joint_t;
typedef pvr_skin_prepared_palette_t demo_palette_t;
#define demo_palette_prepare pvr_skin_palette_prepare
#define demo_skin_apply pvr_skin_apply_prepared
#define DEMO_PALETTE "original"
#endif

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t model_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    UINT32_C(0x43480000), UINT32_C(0x43200000),
    UINT32_C(0x00000000), UINT32_C(0x3f19999a),
    UINT32_C(0x00000000), UINT32_C(0x3f4ccccd),
    UINT32_C(0x43dc0000), UINT32_C(0x43200000),
    UINT32_C(0x00000000), UINT32_C(0x3f19999a),
    UINT32_C(0x00000000), UINT32_C(0x3f4ccccd),
    UINT32_C(0x43a00000), UINT32_C(0x43b40000),
    UINT32_C(0x00000000), UINT32_C(0x3f19999a),
    UINT32_C(0x00000000), UINT32_C(0x3f4ccccd),
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const pvr_chunk_skin_influence_t model_influences[] = {
    { 0, { 0, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 1, { 1, 2, 0, 0 }, { 32768, 32767, 0, 0 }, 0 },
    { 2, { 3, 1, 0, 2 }, { 8192, 16384, 24576, 16383 }, 0 }
};

static const pvr_chunk_model_t model = {
    model_vertices, sizeof(model_vertices) / sizeof(model_vertices[0]),
    model_polygons, sizeof(model_polygons) / sizeof(model_polygons[0]),
    { 320.0f, 260.0f, 0.0f }, 180.0f
};

alignas(32) static const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

#ifndef CHUNK_SKIN_HOST
typedef struct render_context {
    pvr_chunk_material_binding_t material;
    pvr_chunk_render_policy_binding_t policy;
    pvr_chunk_skin_pose_t pose;
} render_context_t;
#endif

static void identity(matrix_t *matrix) {
    memcpy(matrix, &screen_identity, sizeof(*matrix));
}

/* The oracle uses analytic R*S and inverse-transpose R/S, independently of
   the palette builder and skinning kernels. Double libm is test code only. */
static void sample_pose(unsigned frame, matrix_t *positions,
                        pvr_normal_matrix_t *normals, double oracle[JOINT_COUNT][5]) {
    for(unsigned j = 0; j < JOINT_COUNT; ++j) {
        const float angle = ((float)frame - 60.0f) * 0.004f * ((float)j + 1.0f);
        const float sx = 0.85f + 0.1f * (float)j;
        const float sy = 1.1f - 0.075f * (float)j;
        const float sz = 0.7f + 0.2f * (float)j;
        const shz_sincos_t trig = shz_sincosf(angle);
        identity(positions + j);
        positions[j][0][0] = trig.cos * sx;
        positions[j][0][1] = trig.sin * sx;
        positions[j][1][0] = -trig.sin * sy;
        positions[j][1][1] = trig.cos * sy;
        positions[j][2][2] = sz;
        /* Rotate and scale around the authored triangle's center. */
        positions[j][3][0] = 320.0f - 320.0f * positions[j][0][0] - 260.0f * positions[j][1][0];
        positions[j][3][1] = 260.0f - 320.0f * positions[j][0][1] - 260.0f * positions[j][1][1];
        assert(pvr_normal_matrix_build(normals + j, positions + j) == 0);
        oracle[j][0] = sin((double)angle);
        oracle[j][1] = cos((double)angle);
        oracle[j][2] = sx;
        oracle[j][3] = sy;
        oracle[j][4] = sz;
    }
}

static void verify_pose(const pvr_chunk_skin_source_t *source,
                        const pvr_deform_vertex_t *output,
                        double oracle[JOINT_COUNT][5]) {
    for(size_t i = 0; i < source->vertex_count; ++i) {
        const pvr_deform_vertex_t *v = source->vertices + i;
        double position[3] = {0}, normal[3] = {0};
        double sum = 0;
        for(size_t k = 0; k < 4; ++k)
            sum += model_influences[i].weight[k];
        for(size_t k = 0; k < 4; ++k) {
            const double weight = model_influences[i].weight[k] / sum;
            if(weight == 0) continue;
            const double *o = oracle[model_influences[i].joint[k]];
            const double s = o[0], c = o[1], sx = o[2], sy = o[3], sz = o[4];
            const double x = v->position.x - 320.0, y = v->position.y - 260.0;
            position[0] += weight * (320.0 + c * sx * x - s * sy * y);
            position[1] += weight * (260.0 + s * sx * x + c * sy * y);
            position[2] += weight * sz * v->position.z;
            normal[0] += weight * (c * v->normal.x / sx - s * v->normal.y / sy);
            normal[1] += weight * (s * v->normal.x / sx + c * v->normal.y / sy);
            normal[2] += weight * v->normal.z / sz;
        }
        const double length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
#ifdef CHUNK_SKIN_HOST
        const double position_tolerance = 0.0002, normal_tolerance = 0.00002;
#else
        /* FSCA quantizes the angle; this screen-space bound is < 0.075 pixel. */
        const double position_tolerance = 0.075, normal_tolerance = 0.0003;
#endif
        assert(fabs(output[i].position.x - position[0]) < position_tolerance);
        assert(fabs(output[i].position.y - position[1]) < position_tolerance);
        assert(fabs(output[i].position.z - position[2]) < position_tolerance);
        assert(fabs(output[i].normal.x - normal[0] / length) < normal_tolerance);
        assert(fabs(output[i].normal.y - normal[1] / length) < normal_tolerance);
        assert(fabs(output[i].normal.z - normal[2] / length) < normal_tolerance);
        assert(output[i].position.w == 1.0f && output[i].normal.w == 0.0f);
    }
}

#ifndef CHUNK_SKIN_HOST
static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    render_context_t *context = data;

    return pvr_chunk_render_policy_binding_begin_cached_strip(
        strip, &context->policy);
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
    render_context_t *context = data;

    return pvr_chunk_render_policy_binding_prepare_cached_vertex(
        state, source_index, deformed, vertex, &context->policy);
}
#endif

int main(int argc, char **argv) {
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    const pvr_chunk_skin_t skin = {
        model_influences,
        sizeof(model_influences) / sizeof(model_influences[0]), JOINT_COUNT
    };
    pvr_chunk_skin_requirements_t skin_requirements;
    pvr_chunk_skin_binding_t binding;
    uint32_t dense_lookup[256];
    alignas(32) uint8_t source_workspace[192];
    pvr_chunk_skin_source_t source;
#ifndef CHUNK_SKIN_HOST
    pvr_chunk_cache_requirements_t cache_requirements;
    alignas(32) uint8_t cache_storage[512];
    pvr_chunk_model_cache_t cache;
    pvr_chunk_cache_draw_t draw;
#endif
    alignas(8) matrix_t position_matrices[JOINT_COUNT];
    pvr_normal_matrix_t normal_matrices[JOINT_COUNT];
    pvr_skin_palette_t palette = {
        position_matrices, normal_matrices, JOINT_COUNT
    };
    demo_joint_t imported_joints[JOINT_COUNT];
    demo_palette_t prepared_palette;
    pvr_skin_prepared_influence_t prepared_weights[3];
    pvr_skin_prepared_influences_t weight_plan;
    alignas(32) pvr_deform_vertex_t deformed[4];
    pvr_deform_vertex_t tail_guard;
    double oracle[JOINT_COUNT][5];
    pvr_deform_result_t deform_result;
#ifndef CHUNK_SKIN_HOST
    pvr_poly_cxt_t polygon_context;
    pvr_chunk_texture_table_t texture_table = { NULL, 0 };
    pvr_chunk_texture_table_view_t texture_view;
    pvr_light_t light;
    pvr_lighting_extended_context_t lighting;
    pvr_chunk_render_policy_config_t policy_config;
    render_context_t render_context;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t render_workspace[3];
    pvr_chunk_cache_result_t render_result;
    pvr_pipeline_status_t pipeline;
#endif
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
#ifndef CHUNK_SKIN_HOST
    assert(pvr_chunk_model_cache_query(&plan, &cache_requirements) == 0);
    assert(cache_requirements.bytes <= sizeof(cache_storage));
    assert(pvr_chunk_model_cache_build(&plan, cache_storage,
                                       sizeof(cache_storage), NULL, NULL,
                                       &cache) == 0);
    assert(pvr_chunk_model_cache_draw_prepare(&cache, &draw) == 0);

    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    pvr_poly_cxt_col(&polygon_context, PVR_LIST_OP_POLY);
    polygon_context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_chunk_texture_table_open(&texture_table, &texture_view) == 0);
    assert(pvr_chunk_material_binding_init(
        &render_context.material, &polygon_context, &texture_view,
        PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    memset(&light, 0, sizeof(light));
    light.kind = PVR_LIGHT_DIRECTIONAL;
    light.source.direction.z = 1.0f;
    light.color.x = 1.0f;
    light.color.y = 0.75f;
    light.color.z = 0.5f;
    light.intensity = 0.75f;
    memset(&lighting, 0, sizeof(lighting));
    lighting.ambient[0] = lighting.ambient[1] = lighting.ambient[2] = 0.25f;
    lighting.lights = &light;
    lighting.light_count = 1;
    lighting.view_position.x = 320.0f;
    lighting.view_position.y = 240.0f;
    lighting.view_position.z = 10.0f;
    lighting.specular_exponent = 8.0f;
    memset(&policy_config, 0, sizeof(policy_config));
    policy_config.policy = PVR_CHUNK_RENDER_POLICY_DIFFUSE;
    policy_config.object_to_world = &screen_identity;
    policy_config.lighting = &lighting;
    policy_config.begin_strip = pvr_chunk_material_binding_begin_strip;
    policy_config.begin_strip_data = &render_context.material;
    assert(pvr_chunk_render_policy_binding_init(
        &render_context.policy, &policy_config) == 0);
    render_context.pose.binding = &binding;
    render_context.pose.vertices = deformed;
    render_context.pose.vertex_count = 3;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
#endif

    const pvr_deform_stream_t skin_vertices = {
        source.vertices, source.vertex_count, sizeof(*source.vertices)
    };
    const pvr_skin_stream_t skin_weights = {
        source.influences, source.vertex_count, sizeof(*source.influences)
    };
    assert(pvr_skin_influences_prepare(&skin_weights, source.joint_count,
                                       prepared_weights, 3, &weight_plan) == 0);
    memset(&tail_guard, 0x5a, sizeof(tail_guard));
    deformed[3] = tail_guard;
    printf("SKIN4 palette=%s joint_bytes=%u frames=120\n", DEMO_PALETTE,
           (unsigned)sizeof(*imported_joints));

    for(frame = 0; frame < 120u; ++frame) {
        sample_pose(frame, position_matrices, normal_matrices, oracle);
        /* Prepare once after sampling the pose, then reuse for every mesh
           sharing it. This example has just one mesh. */
        assert(demo_palette_prepare(&palette, imported_joints, JOINT_COUNT,
                                    &prepared_palette) == 0);
        assert(demo_skin_apply(deformed, 3, &skin_vertices,
            &weight_plan, &prepared_palette, &deform_result) == 0);
        assert(deform_result.deformed_vertices == 3);
        verify_pose(&source, deformed, oracle);
        assert(memcmp(deformed + 3, &tail_guard, sizeof(tail_guard)) == 0);

#ifndef CHUNK_SKIN_HOST
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_chunk_model_cache_draw_emit(
            &draw, &screen_identity, &sink, render_workspace, 3,
            NULL, begin_strip, resolve_vertex, prepare_vertex,
            &render_context, &render_result) == 0);
        assert(render_result.emitted_strips == 1 &&
               render_result.emitted_vertices == 3);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
#endif
    }

#ifndef CHUNK_SKIN_HOST
    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&pipeline) == 0);
    assert(pipeline.faults.mask == PVR_FAULT_NONE);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (explicit compact skinning)");
#endif
    puts("SKIN4 position_normals=PASS output_guard=PASS");
    puts("RESULT: PASS (explicit compact skinning)");

#ifndef CHUNK_SKIN_HOST
    for(;;)
        thd_sleep(1000);
#endif
    return 0;
}
