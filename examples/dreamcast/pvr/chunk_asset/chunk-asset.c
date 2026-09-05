/* KallistiOS ##version##

   Compact-model asset container example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/pvr_chunk_asset_lz4.h>
#include <kos/pvr_chunk_asset_lz4_service.h>

#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char chunk_asset_model_data[];
extern const int chunk_asset_model_size;

#define TEXTURE_SIZE 64u
#ifndef CHUNK_ASSET_HOLD_MS
#define CHUNK_ASSET_HOLD_MS 10000u
#endif

static alignas(THD_STACK_ALIGNMENT) uint8_t decode_stack[8192];
static volatile int decode_done;
static uint16_t texture_pixels[TEXTURE_SIZE * TEXTURE_SIZE];

static alignas(32) const matrix_t screen_root = {
    { 40.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, -40.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 320.0f, 240.0f, 1.0f, 1.0f }
};

typedef struct decoded_section {
    const void *stored_data;
    void *decoded_data;
    size_t decoded_bytes;
} decoded_section_t;

/* Sections are located by semantic type rather than directory ordinal so the
   converter may add or reorder sections without breaking this example. */
static int load_section_type(const pvr_chunk_asset_view_t *asset,
                             uint32_t type,
                             pvr_chunk_asset_section_t *section,
                             const void **decoded) {
    size_t index;

    if(pvr_chunk_asset_section_find_index(asset, type, 0, &index) < 0 ||
       pvr_chunk_asset_section_get(asset, index, section) < 0)
        return -1;
    return pvr_chunk_asset_section_load(asset, index, NULL, NULL,
                                        NULL, 0, decoded);
}

/* printf on the serial console may itself modify errno. */
static void report_stage(const char *stage) {
    int saved = errno;

    printf("KOSPVRASSET stage=%s errno=%d\n", stage, saved);
    errno = saved;
}

typedef struct asset_render_context {
    pvr_chunk_render_policy_binding_t policy;
    pvr_chunk_skin_general_pose_t pose;
} asset_render_context_t;

static void matrix_identity(matrix_t *matrix) {
    static const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(matrix, &identity, sizeof(*matrix));
}

static void normal_identity(pvr_normal_matrix_t *matrix) {
    memset(matrix, 0, sizeof(*matrix));
    matrix->column[0][0] = 1.0f;
    matrix->column[1][1] = 1.0f;
    matrix->column[2][2] = 1.0f;
}

static void build_texture(void) {
    size_t x;
    size_t y;

    for(y = 0; y < TEXTURE_SIZE; ++y) {
        for(x = 0; x < TEXTURE_SIZE; ++x) {
            texture_pixels[y * TEXTURE_SIZE + x] =
                ((x >> 3) ^ (y >> 3)) & 1u ?
                UINT16_C(0xffff) : UINT16_C(0x041f);
        }
    }
}

static _Noreturn void display_result(int result, int error) {
    char message[96];

    if(result == 0) {
        vid_clear(0, 64, 0);
        strcpy(message, "RESULT: PASS (complete compact asset)");
    }
    else {
        vid_clear(64, 0, 0);
        snprintf(message, sizeof(message),
                 "RESULT: FAIL (complete compact asset, errno=%d)", error);
    }

    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1, message);
    puts(message);

    for(;;)
        thd_sleep(1000);
}

static int render_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    asset_render_context_t *context = data;

    return pvr_chunk_render_policy_binding_begin_cached_strip(
        strip, &context->policy);
}

static int render_resolve(uint16_t source_index,
                          pvr_deform_vertex_t *vertex, void *data) {
    asset_render_context_t *context = data;

    return pvr_chunk_skin_general_pose_vertex_get(
        &context->pose, source_index, vertex);
}

static int render_prepare(
        const pvr_chunk_render_state_t *state, uint16_t source_index,
        const pvr_deform_vertex_t *deformation, pvr_vertex_t *vertex,
        void *data) {
    asset_render_context_t *context = data;

    return pvr_chunk_render_policy_binding_prepare_cached_vertex(
        state, source_index, deformation, vertex, &context->policy);
}

static int render_asset(
        const pvr_chunk_model_view_t *model,
        const pvr_chunk_model_cache_t *cache,
        const pvr_chunk_hierarchy_t *hierarchy,
        const pvr_chunk_skin_general_t *skin,
        const pvr_chunk_shape_set_t *shapes,
        const anim_clip_view_t *animation) {
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_shape_requirements_t shape_requirements;
    uint32_t shape_lookup[256];
    alignas(32) uint8_t shape_workspace[256];
    pvr_chunk_shape_binding_t shape_binding;
    pvr_chunk_shape_source_t shape_source;
    pvr_morph_target_t morph_target;
    alignas(32) pvr_deform_vertex_t morphed[3];
    pvr_chunk_skin_general_requirements_t skin_requirements;
    uint32_t skin_lookup[256];
    alignas(32) uint8_t skin_workspace[256];
    pvr_chunk_skin_general_binding_t skin_binding;
    pvr_chunk_skin_general_source_t skin_source;
    pvr_chunk_skin_general_source_t posed_skin_source;
    alignas(8) matrix_t joint_matrix;
    pvr_normal_matrix_t joint_normal;
    pvr_skin_palette_t palette;
    alignas(32) pvr_deform_vertex_t deformed[3];
    pvr_deform_result_t deform_result;
    pvr_txr_residency_t residency;
    pvr_txr_residency_slot_t residency_slot[1];
    pvr_txr_surface_t residency_surface[1];
    pvr_txr_surface_t prototype;
    pvr_txr_surface_t *surface;
    pvr_txr_residency_handle_t upload_handle;
    pvr_chunk_texture_binding_t texture_binding[1];
    pvr_txr_residency_handle_t render_handle[1];
    pvr_chunk_residency_binding_t material_binding;
    pvr_poly_cxt_t polygon_context;
    pvr_light_t light;
    pvr_lighting_extended_context_t lighting;
    pvr_chunk_render_policy_config_t policy_config;
    asset_render_context_t render_context;
    pvr_geometry_sink_t sink;
    alignas(32) pvr_vertex_t render_workspace[3];
    pvr_chunk_cache_result_t render_result;
    anim_transform_t local_pose[1];
    anim_pose_result_t animation_result;
    alignas(8) matrix_t world[1];
    pvr_deform_stream_t deform_stream;
    pvr_deform_bounds_t bounds;
    pvr_frustum_t frustum;
    pvr_frustum_classification_t classification;
    pvr_pipeline_status_t pipeline;
    unsigned int frame;
    int pvr_started = 0;
    int residency_started = 0;
    int upload_loading = 0;
    int upload_ready_pin = 0;
    int material_started = 0;
    int scene_open = 0;
    int list_open = 0;
    int saved_errno;

    if(!model || !cache || !hierarchy || !skin || !shapes || !animation ||
       model->info.vertex_entries != 3 || hierarchy->node_count != 1 ||
       shapes->target_count != 1 || skin->joint_count != 1) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_plan_query(model, &plan_requirements) < 0)
        return -1;
    if(plan_requirements.vertex_index_entries > 256) {
        errno = ENOSPC;
        return -1;
    }
    if(pvr_chunk_model_plan_build(model, plan_entries, 256, &plan) < 0 ||
       pvr_chunk_shape_query(&plan, shapes->target_count,
                             &shape_requirements) < 0 ||
       pvr_chunk_skin_general_query(&plan, skin, &skin_requirements) < 0)
        return -1;
    if(shape_requirements.lookup_entries > 256 ||
       shape_requirements.source_bytes > sizeof(shape_workspace) ||
       skin_requirements.lookup_entries > 256 ||
       skin_requirements.source_bytes > sizeof(skin_workspace)) {
        errno = ENOSPC;
        return -1;
    }
    if(pvr_chunk_shape_bind(&plan, shapes, shape_lookup, 256,
                            &shape_binding) < 0 ||
       pvr_chunk_shape_source_build(
           &shape_binding, shape_workspace, sizeof(shape_workspace),
           &shape_source) < 0 ||
       pvr_chunk_skin_general_bind(&plan, skin, skin_lookup, 256,
                                   &skin_binding) < 0 ||
       pvr_chunk_skin_general_source_build(
           &skin_binding, skin_workspace, sizeof(skin_workspace),
           &skin_source) < 0) {
        return -1;
    }

    matrix_identity(&joint_matrix);
    normal_identity(&joint_normal);
    palette.position_matrices = &joint_matrix;
    palette.normal_matrices = &joint_normal;
    palette.joint_count = 1;
    render_context.pose.binding = &skin_binding;
    render_context.pose.vertices = deformed;
    render_context.pose.vertex_count = 3;
    deform_stream.vertices = deformed;
    deform_stream.vertex_count = 3;
    deform_stream.stride = sizeof(deformed[0]);

    vid_clear(96, 0, 0);
    if(pvr_init_defaults() < 0)
        return -1;
    pvr_started = 1;
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    build_texture();
    if(pvr_txr_surface_init(&prototype, TEXTURE_SIZE, TEXTURE_SIZE,
                            PVR_TXR_SURFACE_RGB565,
                            PVR_TXR_SURFACE_LINEAR, false) < 0)
        goto render_fail;
    if(pvr_txr_residency_init(&residency, residency_slot,
                              residency_surface, 1, &prototype) < 0)
        goto render_fail;
    residency_started = 1;
    if(pvr_txr_residency_reserve(&residency, 7, &upload_handle,
                                 &surface) < 0)
        goto render_fail;
    upload_loading = 1;
    if(pvr_txr_surface_upload(surface, texture_pixels,
                              sizeof(texture_pixels),
                              PVR_TXR_TRANSFER_CPU) < 0 ||
       pvr_txr_residency_publish(&residency, upload_handle) < 0)
        goto render_fail;
    upload_loading = 0;
    upload_ready_pin = 1;
    if(pvr_txr_residency_unpin(&residency, upload_handle) < 0)
        goto render_fail;
    upload_ready_pin = 0;

    pvr_poly_cxt_col(&polygon_context, PVR_LIST_OP_POLY);
    polygon_context.gen.culling = PVR_CULLING_NONE;
    if(pvr_chunk_residency_binding_init(
           &material_binding, &residency, texture_binding, render_handle, 1,
           NULL, NULL, &polygon_context,
           PVR_GEOMETRY_SINK_CURRENT_LIST) < 0)
        goto render_fail;
    material_started = 1;
    if(pvr_chunk_residency_binding_prepare_model(
           &material_binding, model) < 0 ||
       pvr_geometry_sink_init_current(&sink) < 0)
        goto render_fail;

    memset(&light, 0, sizeof(light));
    light.kind = PVR_LIGHT_DIRECTIONAL;
    light.source.direction.z = 1.0f;
    light.color.x = 1.0f;
    light.color.y = 0.8f;
    light.color.z = 0.6f;
    light.intensity = 0.8f;
    memset(&lighting, 0, sizeof(lighting));
    lighting.ambient[0] = lighting.ambient[1] = lighting.ambient[2] = 0.2f;
    lighting.lights = &light;
    lighting.light_count = 1;
    lighting.view_position.x = 320.0f;
    lighting.view_position.y = 240.0f;
    lighting.view_position.z = 10.0f;
    lighting.specular_exponent = 8.0f;

    for(frame = 0; frame < 120u; ++frame) {
        float time = (float)frame / 119.0f;

        morph_target.deltas = shape_source.deltas;
        morph_target.stride = sizeof(shape_source.deltas[0]);
        morph_target.weight = 0.5f + 0.5f * sinf(time * F_PI * 2.0f);
        if(pvr_chunk_shape_apply(&shape_source, &morph_target, 1,
                                 morphed, 3, &deform_result) < 0)
            goto render_fail;
        posed_skin_source = skin_source;
        posed_skin_source.vertices = morphed;
        if(pvr_chunk_skin_general_apply(
               &posed_skin_source, &palette, deformed, 3,
               &deform_result) < 0 ||
           anim_clip_sample(animation, time, local_pose, 1,
                            &animation_result) < 0 ||
           pvr_chunk_hierarchy_traverse_poses(
               hierarchy, local_pose, 1, &screen_root, world, 1,
               NULL, NULL, NULL) < 0 ||
           pvr_deform_bounds_calculate(&deform_stream, &bounds) < 0 ||
           pvr_frustum_init(&frustum, &world[0], 0.0f, 0.0f,
                            640.0f, 480.0f, 0.5f, 2.0f) < 0 ||
           pvr_frustum_classify_sphere(
               &frustum, &bounds.center, bounds.radius,
               &classification) < 0)
            goto render_fail;
        if(classification == PVR_FRUSTUM_OUTSIDE) {
            errno = ERANGE;
            goto render_fail;
        }

        memset(&policy_config, 0, sizeof(policy_config));
        policy_config.policy = PVR_CHUNK_RENDER_POLICY_DIFFUSE;
        policy_config.object_to_world = &world[0];
        policy_config.lighting = &lighting;
        policy_config.begin_strip =
            pvr_chunk_residency_binding_begin_strip;
        policy_config.begin_strip_data = &material_binding;
        if(pvr_chunk_render_policy_binding_init(
               &render_context.policy, &policy_config) < 0 ||
           pvr_wait_ready() < 0)
            goto render_fail;
        pvr_scene_begin();
        scene_open = 1;
        if(pvr_list_begin(PVR_LIST_OP_POLY) < 0)
            goto render_fail;
        list_open = 1;
        if(pvr_chunk_model_cache_emit(
               cache, &world[0], &sink, render_workspace, 3,
               render_begin, render_resolve, render_prepare,
               &render_context, &render_result) < 0)
            goto render_fail;
        if(render_result.emitted_strips != 1 ||
           render_result.emitted_vertices != 3) {
            errno = EILSEQ;
            goto render_fail;
        }
        if(pvr_list_finish() < 0)
            goto render_fail;
        list_open = 0;
        if(pvr_scene_finish() < 0)
            goto render_fail;
        scene_open = 0;
    }

    if(pvr_wait_render_done() < 0 ||
       pvr_get_pipeline_status(&pipeline) < 0)
        goto render_fail;
    if(pipeline.faults.mask != PVR_FAULT_NONE) {
        errno = EIO;
        goto render_fail;
    }

    /* Keep the completed image available for inspection before shutdown
       clears VRAM. The delay adds no rendering or decode work. */
    puts("KOSPVRASSET rendered=1 inspecting=1 (holding final frame)");
    thd_sleep(CHUNK_ASSET_HOLD_MS);

    if(pvr_chunk_residency_binding_release(&material_binding) < 0)
        goto render_fail;
    material_started = 0;
    if(pvr_txr_residency_destroy(&residency) < 0)
        goto render_fail;
    residency_started = 0;
    if(pvr_shutdown() < 0)
        return -1;
    return 0;

render_fail:
    saved_errno = errno;
    if(list_open)
        pvr_list_finish();
    if(scene_open)
        pvr_scene_finish();
    if(pvr_started)
        pvr_wait_render_done();
    if(material_started)
        pvr_chunk_residency_binding_release(&material_binding);
    if(upload_loading)
        pvr_txr_residency_abort(&residency, upload_handle);
    else if(upload_ready_pin)
        pvr_txr_residency_unpin(&residency, upload_handle);
    if(residency_started)
        pvr_txr_residency_destroy(&residency);
    if(pvr_started)
        pvr_shutdown();
    errno = saved_errno;
    return -1;
}

static void decode_complete(pvr_chunk_asset_lz4_job_t *job, void *data) {
    (void)job;
    (void)data;
    decode_done = 1;
}

static int use_predecoded_section(const pvr_chunk_asset_section_t *section,
                                  void *destination,
                                  size_t destination_bytes, void *data) {
    const decoded_section_t *decoded = data;

    if(section->stored_data != decoded->stored_data ||
       destination != decoded->decoded_data ||
       destination_bytes != decoded->decoded_bytes) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int main(void) {
    pvr_chunk_asset_view_t asset;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_scene_asset_view_t scene_asset;
    pvr_chunk_scene_asset_workspace_requirements_t scene_requirements;
    pvr_chunk_asset_section_t resource_section;
    pvr_chunk_asset_section_t cooked_section;
    pvr_chunk_asset_section_t skin_section;
    pvr_chunk_asset_section_t shape_section;
    pvr_chunk_asset_section_t animation_section;
    pvr_chunk_resource_section_view_t resource_view;
    pvr_chunk_resource_entry_t resource_entry;
    pvr_chunk_cache_section_view_t cooked_view;
    pvr_chunk_cache_section_requirements_t cooked_requirements;
    pvr_chunk_model_cache_t cooked_cache;
    pvr_chunk_skin_general_section_view_t skin_view;
    pvr_chunk_skin_span_t skin_spans[3];
    pvr_chunk_skin_weight_t skin_weights[3];
    pvr_chunk_skin_general_t skin;
    pvr_chunk_shape_section_view_t shape_view;
    pvr_chunk_shape_target_t shape_target;
    pvr_chunk_shape_delta_t shape_delta;
    pvr_chunk_shape_set_t shapes;
    pvr_chunk_animation_section_view_t animation_view;
    pvr_chunk_animation_key_t animation_keys[2];
    anim_track_view_t animation_track;
    anim_transform_tracks_t animation_transform;
    anim_visibility_tracks_t animation_visibility;
    anim_clip_view_t animation_clip;
    anim_transform_t animation_pose;
    anim_pose_result_t animation_result;
    pvr_chunk_hierarchy_node_t hierarchy_node;
    pvr_chunk_hierarchy_t hierarchy;
    pvr_chunk_model_view_t model;
    const void *resource_data = NULL;
    const void *cooked_data = NULL;
    const void *skin_data = NULL;
    const void *shape_data = NULL;
    const void *animation_data = NULL;
    pvr_chunk_asset_lz4_service_t *decode_service = NULL;
    pvr_chunk_asset_lz4_job_t *job = NULL;
    pvr_chunk_asset_lz4_job_status_t status;
    fiber_service_executor_t *executor = NULL;
    decoded_section_t decoded;
    void *workspace = NULL;
    void *cooked_storage = NULL;
    size_t allocation = 0;
    int result = 1;
    int final_errno;

    if(chunk_asset_model_size <= 0) {
        errno = EILSEQ;
        goto loaded_fail;
    }
    if(pvr_chunk_asset_open(chunk_asset_model_data,
                            (size_t)chunk_asset_model_size, &asset) < 0 ||
       pvr_chunk_asset_workspace_query(&asset, &requirements) < 0 ||
       pvr_chunk_scene_asset_open(&asset, &scene_asset) < 0 ||
       pvr_chunk_scene_asset_workspace_query(
           &scene_asset, &scene_requirements) < 0) {
        report_stage("open");
        goto out;
    }
    if(scene_requirements.bytes != requirements.bytes) {
        errno = EILSEQ;
        goto loaded_fail;
    }

    if(requirements.bytes) {
        if(requirements.bytes > SIZE_MAX - (requirements.alignment - 1u)) {
            errno = ERANGE;
            goto loaded_fail;
        }
        allocation = (requirements.bytes + requirements.alignment - 1u) &
                     ~(requirements.alignment - 1u);
        workspace = aligned_alloc(requirements.alignment, allocation);
        if(!workspace) {
            report_stage("workspace");
            goto out;
        }
    }

    executor = fiber_service_executor_create();
    if(!executor)
        goto out;
    decode_service = pvr_chunk_asset_lz4_service_create(
        executor, decode_stack, sizeof(decode_stack), 2, 16);
    if(!decode_service ||
       fiber_service_executor_start(executor, NULL) < 0 ||
       pvr_chunk_asset_lz4_service_start(decode_service, 1000) < 0)
        goto out;

    decoded.stored_data = asset.vertex.stored_data;
    decoded.decoded_data = (uint8_t *)workspace + requirements.vertex_offset;
    decoded.decoded_bytes = asset.vertex.decoded_bytes;
    job = pvr_chunk_asset_lz4_job_create(
        &asset.vertex, decoded.decoded_data, decoded.decoded_bytes, NULL,
        decode_complete, NULL);
    if(!job || pvr_chunk_asset_lz4_service_submit(decode_service, job) < 0)
        goto out;
    while(!decode_done)
        thd_pass();
    if(pvr_chunk_asset_lz4_job_get_status(job, &status) < 0)
        goto out;
    if(status.state != PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE) {
        errno = status.error ? status.error : EIO;
        goto loaded_fail;
    }

    if(pvr_chunk_scene_asset_load(
           &scene_asset, use_predecoded_section, &decoded,
           workspace, allocation, &model, 1, &hierarchy_node, 1,
           &hierarchy) < 0 ||
       load_section_type(&asset, PVR_CHUNK_ASSET_SECTION_COOKED_CACHE,
                         &cooked_section, &cooked_data) < 0 ||
       pvr_chunk_cache_section_open(
           cooked_data, cooked_section.decoded_bytes, &cooked_view) < 0 ||
       pvr_chunk_cache_section_workspace_query(
           &cooked_view, &cooked_requirements) < 0)
        goto loaded_fail;
    cooked_storage = aligned_alloc(cooked_requirements.alignment,
                                   cooked_requirements.bytes);
    if(!cooked_storage || pvr_chunk_cache_section_materialize_ordinary(
           &cooked_view, cooked_storage, cooked_requirements.bytes,
           &cooked_cache) < 0)
        goto loaded_fail;

    if(load_section_type(&asset, PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE,
                         &resource_section, &resource_data) < 0 ||
       pvr_chunk_resource_section_open(
           resource_data, resource_section.decoded_bytes,
           &resource_view) < 0 ||
       pvr_chunk_resource_section_validate_model(
           &resource_view, &model) < 0 ||
       pvr_chunk_resource_section_find(
           &resource_view, 7, &resource_entry) < 0) {
        report_stage("resource");
        goto loaded_fail;
    }
    if(resource_entry.usage != PVR_CHUNK_RESOURCE_PRIMARY) {
        errno = EILSEQ;
        goto loaded_fail;
    }

    if(load_section_type(&asset, PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL,
                         &skin_section, &skin_data) < 0 ||
       pvr_chunk_skin_general_section_open(
           skin_data, skin_section.decoded_bytes, &skin_view) < 0 ||
       pvr_chunk_skin_general_section_materialize(
           &skin_view, skin_spans, 3, skin_weights, 3, &skin) < 0) {
        report_stage("skin");
        goto loaded_fail;
    }
    if(model.info.vertex_entries != 3 || model.info.triangles != 1 ||
       hierarchy.node_count != 1 || hierarchy_node.model != &model ||
       hierarchy_node.parent_index != PVR_CHUNK_NODE_NONE ||
       skin.span_count != 3 || skin.weight_count != 3 ||
       skin.joint_count != 1 ||
       cooked_cache.strip_count != model.info.strips ||
       cooked_cache.vertex_count != model.info.index_references) {
        errno = EILSEQ;
        goto loaded_fail;
    }

    if(load_section_type(&asset, PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
                         &shape_section, &shape_data) < 0 ||
       pvr_chunk_shape_section_open(
           shape_data, shape_section.decoded_bytes, &shape_view) < 0 ||
       pvr_chunk_shape_section_materialize(
           &shape_view, &shape_target, 1, &shape_delta, 1, &shapes) < 0) {
        report_stage("shape");
        goto loaded_fail;
    }
    if(shapes.target_count != 1 || shape_target.delta_count != 1 ||
       shape_delta.vertex_index != 0 ||
       shape_delta.delta.position.x != 1.0f) {
        errno = EILSEQ;
        goto loaded_fail;
    }

    if(load_section_type(&asset, PVR_CHUNK_ASSET_SECTION_ANIMATION,
                         &animation_section, &animation_data) < 0 ||
       pvr_chunk_animation_section_open(
           animation_data, animation_section.decoded_bytes,
           &animation_view) < 0 ||
       pvr_chunk_animation_section_materialize(
           &animation_view, animation_keys, 2, &animation_track, 1,
           &animation_transform, 1, &animation_visibility, 1,
           &animation_clip) < 0 ||
       anim_clip_sample(&animation_clip, 0.5f, &animation_pose, 1,
                        &animation_result) < 0) {
        report_stage("animation");
        goto loaded_fail;
    }
    if(animation_result.sampled_transforms != 1 ||
       animation_pose.translation.x != 1.0f ||
       animation_pose.translation.y != 2.0f ||
       animation_pose.translation.z != 3.0f) {
        errno = EILSEQ;
        goto loaded_fail;
    }

    if(fiber_service_executor_destroy(executor) < 0) {
        report_stage("executor-destroy");
        goto loaded_fail;
    }
    executor = NULL;
    if(pvr_chunk_asset_lz4_service_destroy(decode_service) < 0) {
        report_stage("service-destroy");
        goto loaded_fail;
    }
    decode_service = NULL;
    if(pvr_chunk_asset_lz4_job_destroy(job) < 0) {
        report_stage("job-destroy");
        goto loaded_fail;
    }
    job = NULL;
    if(render_asset(&model, &cooked_cache, &hierarchy, &skin,
                    &shapes, &animation_clip) < 0) {
        report_stage("render");
        goto loaded_fail;
    }

    printf("KOSPVRASSET loaded=1 service=1 rendered=1 "
           "vertices=%lu triangles=%lu "
           "resources=%lu cache=%lu hierarchy=%lu skin=%lu morph=%lu "
           "animation=%lu workspace=%lu decoded=%lu\n",
           (unsigned long)model.info.vertex_entries,
           (unsigned long)model.info.triangles,
           (unsigned long)resource_view.entry_count,
           (unsigned long)cooked_cache.vertex_count,
           (unsigned long)hierarchy.node_count,
           (unsigned long)skin.span_count,
           (unsigned long)shapes.target_count,
           (unsigned long)animation_clip.clip.transform_count,
           (unsigned long)requirements.bytes,
           (unsigned long)status.output_bytes);
    result = 0;
    goto out;

loaded_fail:
    report_stage("loaded");

out:
    final_errno = errno;
    /* A failed join must not release buffers still borrowed by the service.
       Hold the failure screen with that storage alive in this case. */
    if(executor && fiber_service_executor_destroy(executor) < 0)
        display_result(1, result ? final_errno : errno);
    if(decode_service &&
       pvr_chunk_asset_lz4_service_destroy(decode_service) < 0)
        display_result(1, result ? final_errno : errno);
    if(job && pvr_chunk_asset_lz4_job_destroy(job) < 0)
        display_result(1, result ? final_errno : errno);
    free(cooked_storage);
    free(workspace);
    display_result(result, final_errno);
}
