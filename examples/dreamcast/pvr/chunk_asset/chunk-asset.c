/* KallistiOS ##version##

   Compact-model asset container example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/pvr_chunk_asset_lz4.h>
#include <kos/pvr_chunk_asset_lz4_service.h>

#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char chunk_asset_model_data[];
extern const int chunk_asset_model_size;

static alignas(THD_STACK_ALIGNMENT) uint8_t decode_stack[8192];
static volatile int decode_done;

typedef struct decoded_section {
    const void *stored_data;
    void *decoded_data;
    size_t decoded_bytes;
} decoded_section_t;

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
    pvr_chunk_asset_section_t hierarchy_section;
    pvr_chunk_asset_section_t resource_section;
    pvr_chunk_asset_section_t skin_section;
    pvr_chunk_asset_section_t shape_section;
    pvr_chunk_asset_section_t animation_section;
    pvr_chunk_scene_hierarchy_view_t hierarchy_view;
    pvr_chunk_resource_section_view_t resource_view;
    pvr_chunk_resource_entry_t resource_entry;
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
    const pvr_chunk_model_view_t *models[1];
    const void *hierarchy_data = NULL;
    const void *resource_data = NULL;
    const void *skin_data = NULL;
    const void *shape_data = NULL;
    const void *animation_data = NULL;
    pvr_chunk_asset_lz4_service_t *decode_service = NULL;
    pvr_chunk_asset_lz4_job_t *job = NULL;
    pvr_chunk_asset_lz4_job_status_t status;
    fiber_service_executor_t *executor = NULL;
    decoded_section_t decoded;
    void *workspace = NULL;
    size_t allocation = 0;
    int result = 1;

    if(chunk_asset_model_size <= 0 ||
       pvr_chunk_asset_open(chunk_asset_model_data,
                            (size_t)chunk_asset_model_size, &asset) < 0 ||
       pvr_chunk_asset_workspace_query(&asset, &requirements) < 0) {
        printf("KOSPVRASSET open=0 errno=%d\n", errno);
        return 1;
    }

    if(requirements.bytes) {
        allocation = (requirements.bytes + requirements.alignment - 1u) &
                     ~(requirements.alignment - 1u);
        workspace = aligned_alloc(requirements.alignment, allocation);
        if(!workspace) {
            printf("KOSPVRASSET workspace=0 errno=%d\n", errno);
            return 1;
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
    if(pvr_chunk_asset_lz4_job_get_status(job, &status) < 0 ||
       status.state != PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE)
        goto out;

    models[0] = &model;
    if(pvr_chunk_asset_load(&asset, use_predecoded_section, &decoded,
                            workspace, allocation, &model) == 0 &&
       pvr_chunk_asset_section_get(&asset, 2, &resource_section) == 0 &&
       resource_section.type == PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE &&
       pvr_chunk_asset_section_load(&asset, 2, NULL, NULL, NULL, 0,
                                    &resource_data) == 0 &&
       pvr_chunk_resource_section_open(
           resource_data, resource_section.decoded_bytes,
           &resource_view) == 0 &&
       pvr_chunk_resource_section_validate_model(
           &resource_view, &model) == 0 &&
       pvr_chunk_resource_section_find(
           &resource_view, 7, &resource_entry) == 0 &&
       resource_entry.usage == PVR_CHUNK_RESOURCE_PRIMARY &&
       pvr_chunk_asset_section_get(&asset, 3, &hierarchy_section) == 0 &&
       hierarchy_section.type == PVR_CHUNK_ASSET_SECTION_HIERARCHY &&
       pvr_chunk_asset_section_load(&asset, 3, NULL, NULL, NULL, 0,
                                    &hierarchy_data) == 0 &&
       pvr_chunk_scene_hierarchy_open(
           hierarchy_data, hierarchy_section.decoded_bytes,
           &hierarchy_view) == 0 &&
       pvr_chunk_scene_hierarchy_bind(
           &hierarchy_view, models, 1, &hierarchy_node, 1,
           &hierarchy) == 0 &&
       pvr_chunk_asset_section_get(&asset, 4, &skin_section) == 0 &&
       skin_section.type == PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL &&
       pvr_chunk_asset_section_load(&asset, 4, NULL, NULL, NULL, 0,
                                    &skin_data) == 0 &&
       pvr_chunk_skin_general_section_open(
           skin_data, skin_section.decoded_bytes, &skin_view) == 0 &&
       pvr_chunk_skin_general_section_materialize(
           &skin_view, skin_spans, 3, skin_weights, 3, &skin) == 0 &&
       model.info.vertex_entries == 3 && model.info.triangles == 1 &&
       hierarchy.node_count == 1 && hierarchy_node.model == &model &&
       hierarchy_node.parent_index == PVR_CHUNK_NODE_NONE &&
       skin.span_count == 3 && skin.weight_count == 3 &&
       skin.joint_count == 1 &&
       pvr_chunk_asset_section_get(&asset, 5, &shape_section) == 0 &&
       shape_section.type == PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS &&
       pvr_chunk_asset_section_load(&asset, 5, NULL, NULL, NULL, 0,
                                    &shape_data) == 0 &&
       pvr_chunk_shape_section_open(
           shape_data, shape_section.decoded_bytes, &shape_view) == 0 &&
       pvr_chunk_shape_section_materialize(
           &shape_view, &shape_target, 1, &shape_delta, 1, &shapes) == 0 &&
       shapes.target_count == 1 && shape_target.delta_count == 1 &&
       shape_delta.vertex_index == 0 &&
       shape_delta.delta.position.x == 1.0f &&
       pvr_chunk_asset_section_get(&asset, 6, &animation_section) == 0 &&
       animation_section.type == PVR_CHUNK_ASSET_SECTION_ANIMATION &&
       pvr_chunk_asset_section_load(&asset, 6, NULL, NULL, NULL, 0,
                                    &animation_data) == 0 &&
       pvr_chunk_animation_section_open(
           animation_data, animation_section.decoded_bytes,
           &animation_view) == 0 &&
       pvr_chunk_animation_section_materialize(
           &animation_view, animation_keys, 2, &animation_track, 1,
           &animation_transform, 1, &animation_visibility, 1,
           &animation_clip) == 0 &&
       anim_clip_sample(&animation_clip, 0.5f, &animation_pose, 1,
                        &animation_result) == 0 &&
       animation_result.sampled_transforms == 1 &&
       animation_pose.translation.x == 1.0f &&
       animation_pose.translation.y == 2.0f &&
       animation_pose.translation.z == 3.0f) {
        printf("KOSPVRASSET loaded=1 service=1 vertices=%lu triangles=%lu "
               "resources=%lu hierarchy=%lu skin=%lu morph=%lu animation=%lu "
               "workspace=%lu "
               "decoded=%lu\n",
               (unsigned long)model.info.vertex_entries,
               (unsigned long)model.info.triangles,
               (unsigned long)resource_view.entry_count,
               (unsigned long)hierarchy.node_count,
               (unsigned long)skin.span_count,
               (unsigned long)shapes.target_count,
               (unsigned long)animation_clip.clip.transform_count,
               (unsigned long)requirements.bytes,
               (unsigned long)status.output_bytes);
        result = 0;
    }
    else {
        printf("KOSPVRASSET loaded=0 errno=%d\n", errno);
    }

out:
    if(executor)
        fiber_service_executor_destroy(executor);
    if(decode_service)
        pvr_chunk_asset_lz4_service_destroy(decode_service);
    if(job)
        pvr_chunk_asset_lz4_job_destroy(job);
    free(workspace);
    return result;
}
