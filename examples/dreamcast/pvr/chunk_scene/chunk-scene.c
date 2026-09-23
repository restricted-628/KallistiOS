/* KallistiOS ##version##

   Multi-model pose integration example.
   Copyright (C) 2026 Joseph Black
*/

#ifndef CHUNK_SCENE_HOST
#include <kos.h>
#endif
#include <dc/animation.h>
#include <dc/pvr_chunk_animation_asset.h>
#include <dc/pvr_chunk_animation_catalog.h>
#include <dc/pvr_chunk_cache_asset.h>
#include <dc/pvr_chunk_morph_animation_asset.h>
#include <dc/pvr_chunk_scene.h>
#include <dc/pvr_chunk_skin_asset.h>
#include <dc/pvr_chunk_skeleton_asset.h>
#include <dc/pvr_chunk_skeleton_affine.h>
#include <dc/pvr_chunk_shape_asset.h>
#include <dc/pvr_skin_prepared.h>
#include <dc/pvr_lighting.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately bounded fixture storage, not a general scene manager. */
#define MODELS 2u
#define NODES 5u
#define JOINTS 2u
#ifdef CHUNK_SCENE_GRID
#define VERTICES 289u
#define WEIGHTS (2u * VERTICES)
#define TRIANGLES 512u
#define STRIPS 16u
#define STRIP_VERTICES 34u
#define SOURCE_BYTES (VERTICES * 128u)
#define CLIP_TRACKS 3u
#else
#define VERTICES 3u
#define WEIGHTS VERTICES
#define TRIANGLES 1u
#define STRIPS 1u
#define STRIP_VERTICES 3u
#define SOURCE_BYTES 512u
#define CLIP_TRACKS 1u
#endif
#define PACKETS (STRIPS * STRIP_VERTICES)
/* The prepared source-index table is page-granular. */
#define LOOKUP (((VERTICES + PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE - 1u) / \
                 PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE) * PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE)

typedef struct model_state {
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t entries[LOOKUP];
    pvr_chunk_skin_span_t spans[VERTICES];
    pvr_chunk_skin_weight_t weights[WEIGHTS];
    pvr_chunk_skin_general_t skin;
    uint32_t skin_lookup[LOOKUP];
    pvr_chunk_skin_general_binding_t skin_binding;
    alignas(32) uint8_t skin_storage[SOURCE_BYTES];
    pvr_chunk_skin_general_source_t skin_source;
    pvr_skin_prepared_span_t prepared_spans[VERTICES];
    pvr_skin_weight_t prepared_weights[WEIGHTS];
    pvr_skin_prepared_spans_t skin_plan;
    pvr_chunk_skeleton_affine_joint_t affine_joints[JOINTS];
    pvr_chunk_skeleton_affine_t affine_skeleton;
    pvr_skin_prepared_joint_t prepared_joints[JOINTS];
    pvr_skin_prepared_palette_t prepared_palette;
    pvr_chunk_shape_section_view_t shape_view;
    pvr_chunk_shape_target_t shape_target;
    pvr_chunk_shape_delta_t shape_delta;
    pvr_chunk_shape_set_t shapes;
    uint32_t shape_lookup[LOOKUP];
    pvr_chunk_shape_binding_t shape_binding;
    alignas(32) uint8_t shape_storage[SOURCE_BYTES];
    pvr_chunk_shape_source_t shape_source;
    anim_morph_target_tracks_t morph_tracks;
    pvr_morph_target_t morph_target;
    alignas(32) pvr_deform_vertex_t morphed[VERTICES];
    alignas(32) pvr_deform_vertex_t deformed[VERTICES];
    pvr_chunk_skin_general_pose_t pose;
    pvr_chunk_model_cache_t cache;
    pvr_chunk_cache_draw_t draw;
    void *cache_storage;
} model_state_t;

static struct {
    pvr_chunk_asset_view_t asset;
    pvr_chunk_scene_asset_view_t scene;
    pvr_chunk_model_view_t models[MODELS];
    pvr_chunk_hierarchy_node_t nodes[NODES];
    pvr_chunk_hierarchy_t hierarchy;
    model_state_t model[MODELS];
    pvr_chunk_animation_key_t keys[3u * CLIP_TRACKS];
    anim_track_view_t track[CLIP_TRACKS];
    anim_transform_tracks_t transforms[NODES];
    anim_visibility_tracks_t visibility[NODES];
    anim_clip_view_t clip;
    anim_transform_t local[NODES];
    alignas(32) matrix_t world[NODES];
    shz_mat3x4_t affine_world[NODES];
    pvr_chunk_skeleton_affine_pose_t affine_pose;
    anim_scalar_hermite_key_t morph_keys[6];
    anim_track_view_t morph_tracks[MODELS];
    pvr_chunk_shape_channel_t channels[MODELS];
    pvr_chunk_morph_animation_binding_t bindings[MODELS];
    pvr_chunk_morph_animation_t morph;
    void *decode_storage;
} app;

/* Preserve the failing operation's errno through diagnostics and cleanup. */
static int failure(const char *stage) {
    int error = errno;
    printf("KOSSCENE stage=%s errno=%d\n", stage, error);
    errno = error;
    return -1;
}

static int require(int condition) {
    if(condition)
        return 0;
    errno = EILSEQ;
    return -1;
}

static int section(uint32_t type, size_t ordinal,
                    const void **data, size_t *bytes) {
    pvr_chunk_asset_section_t descriptor;
    size_t index;

    if(pvr_chunk_asset_section_find_index(&app.asset, type, ordinal,
                                           &index) < 0 ||
       pvr_chunk_asset_section_get(&app.asset, index, &descriptor) < 0 ||
       pvr_chunk_asset_section_load(&app.asset, index, NULL, NULL,
                                     NULL, 0, data) < 0)
        return -1;
    *bytes = descriptor.decoded_bytes;
    return 0;
}

static int model_load(size_t index) {
    model_state_t *m = &app.model[index];
    pvr_chunk_model_table_record_t record;
    pvr_chunk_model_plan_requirements_t plan_req;
    pvr_chunk_skin_general_section_view_t skin_view;
    pvr_chunk_skin_general_requirements_t skin_req;
    pvr_chunk_skeleton_section_view_t skeleton_view;
    pvr_chunk_skeleton_joint_t joints[JOINTS];
    pvr_chunk_skeleton_t skeleton;
    pvr_chunk_shape_requirements_t shape_req;
    pvr_chunk_cache_section_view_t cache_view;
    pvr_chunk_cache_section_requirements_t cache_req;
    const void *data;
    size_t bytes;

    if(require(app.models[index].info.vertex_entries == VERTICES &&
               app.models[index].info.triangles == TRIANGLES) < 0 ||
       pvr_chunk_model_table_record_get(&app.scene.model_table, index,
                                          &record) < 0 ||
       pvr_chunk_model_plan_query(&app.models[index], &plan_req) < 0 ||
       require(plan_req.vertex_index_entries <= LOOKUP) < 0 ||
       pvr_chunk_model_plan_build(&app.models[index], m->entries, LOOKUP,
                                    &m->plan) < 0)
        return failure("model-plan");

    if(section(PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL,
                 record.skin_general_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_skin_general_section_open(data, bytes, &skin_view) < 0 ||
       pvr_chunk_skin_general_section_materialize(
           &skin_view, m->spans, VERTICES, m->weights, WEIGHTS,
           &m->skin) < 0 ||
       require(m->skin.joint_count == JOINTS &&
               m->skin.span_count == VERTICES) < 0 ||
       pvr_chunk_skin_general_query(&m->plan, &m->skin, &skin_req) < 0 ||
       require(skin_req.source_bytes <= sizeof(m->skin_storage) &&
               skin_req.lookup_entries <= LOOKUP) < 0 ||
       pvr_chunk_skin_general_bind(&m->plan, &m->skin, m->skin_lookup,
                                     LOOKUP, &m->skin_binding) < 0 ||
       pvr_chunk_skin_general_source_build(
           &m->skin_binding, m->skin_storage, sizeof(m->skin_storage),
           &m->skin_source) < 0)
        return failure("skin");

    const pvr_skin_span_stream_t influences = {
        m->skin_source.spans, m->skin_source.vertex_count,
        sizeof(*m->skin_source.spans), m->skin_source.weights,
        m->skin_source.weight_count
    };
    pvr_skin_span_plan_requirements_t prepared_req;
    if(pvr_skin_spans_prepare_query(&influences, JOINTS, &prepared_req) < 0 ||
       require(prepared_req.span_count <= VERTICES &&
               prepared_req.weight_count <= WEIGHTS) < 0 ||
       pvr_skin_spans_prepare(&influences, JOINTS, m->prepared_spans, VERTICES,
           m->prepared_weights, WEIGHTS, &m->skin_plan) < 0)
        return failure("skin-prepare");

    if(section(PVR_CHUNK_ASSET_SECTION_SKELETON,
                 record.skeleton_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_skeleton_section_open(data, bytes, &skeleton_view) < 0 ||
       require(skeleton_view.node_count == NODES &&
               skeleton_view.joint_count == JOINTS) < 0 ||
       pvr_chunk_skeleton_section_materialize(
           &skeleton_view, joints, JOINTS, &skeleton) < 0 ||
       pvr_chunk_skeleton_affine_prepare(&skeleton, m->affine_joints, JOINTS,
                                         &m->affine_skeleton) < 0)
        return failure("skeleton");

    if(section(PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
                 record.morph_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_shape_section_open(data, bytes, &m->shape_view) < 0 ||
       pvr_chunk_shape_section_materialize(&m->shape_view, &m->shape_target,
                                            1, &m->shape_delta, 1,
                                            &m->shapes) < 0 ||
       pvr_chunk_shape_query(&m->plan, 1, &shape_req) < 0 ||
       require(shape_req.source_bytes <= sizeof(m->shape_storage) &&
               shape_req.lookup_entries <= LOOKUP) < 0 ||
       pvr_chunk_shape_bind(&m->plan, &m->shapes, m->shape_lookup,
                             LOOKUP, &m->shape_binding) < 0 ||
       pvr_chunk_shape_source_build(
           &m->shape_binding, m->shape_storage, sizeof(m->shape_storage),
           &m->shape_source) < 0)
        return failure("morph");

    if(section(PVR_CHUNK_ASSET_SECTION_COOKED_CACHE,
                 record.cooked_cache_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_cache_section_open(data, bytes, &cache_view) < 0 ||
       pvr_chunk_cache_section_workspace_query(&cache_view, &cache_req) < 0)
        return failure("cache-query");
    m->cache_storage = aligned_alloc(cache_req.alignment, cache_req.bytes);
    if(!m->cache_storage || pvr_chunk_cache_section_materialize_ordinary(
           &cache_view, m->cache_storage, cache_req.bytes, &m->cache) < 0 ||
       pvr_chunk_model_cache_draw_prepare(&m->cache, &m->draw) < 0)
        return failure("cache");
    if(require(m->cache.vertex_count == PACKETS &&
               m->cache.strip_count == STRIPS &&
               m->cache.maximum_strip_vertices == STRIP_VERTICES) < 0)
        return failure("cache-layout");
    for(size_t strip = 0; strip < STRIPS; ++strip) {
        if(require(m->cache.strips[strip].first_vertex == strip * STRIP_VERTICES &&
                   m->cache.strips[strip].vertex_count == STRIP_VERTICES) < 0)
            return failure("strip-layout");
#ifdef CHUNK_SCENE_GRID
        /* Resolve this fixture's sole texture binding once, before drawing. */
        if(require((m->cache.strips[strip].state.present & PVR_CHUNK_RENDER_TEXTURE) &&
                   m->cache.strips[strip].state.texture.identifier == 7) < 0)
            return failure("texture-binding");
#endif
    }
    m->pose.binding = &m->skin_binding;
    m->pose.vertices = m->deformed;
    m->pose.vertex_count = VERTICES;
    return 0;
}

static int scene_load(const void *source, size_t source_bytes) {
    pvr_chunk_scene_asset_workspace_requirements_t req;
    pvr_chunk_animation_catalog_view_t catalog;
    pvr_chunk_animation_catalog_clip_t logical_clip;
    pvr_chunk_animation_section_view_t animation_view;
    pvr_chunk_morph_animation_section_view_t morph_view;
    const pvr_chunk_shape_section_view_t *shapes[MODELS] = { NULL, NULL };
    const void *data;
    size_t bytes;
    size_t i;

    if(pvr_chunk_asset_open(source, source_bytes, &app.asset) < 0 ||
       pvr_chunk_scene_asset_open(&app.asset, &app.scene) < 0 ||
       require(app.scene.model_count == MODELS &&
               app.scene.node_count == NODES) < 0 ||
       pvr_chunk_scene_asset_workspace_query(&app.scene, &req) < 0)
        return failure("scene-open");
    if(req.bytes) {
        app.decode_storage = aligned_alloc(req.alignment, req.bytes);
        if(!app.decode_storage)
            return failure("scene-storage");
    }
    if(pvr_chunk_scene_asset_load(
           &app.scene, NULL, NULL, app.decode_storage, req.bytes,
           app.models, MODELS, app.nodes, NODES, &app.hierarchy) < 0)
        return failure("scene-load");
    for(i = 0; i < MODELS; ++i) {
        pvr_chunk_model_table_record_t record;
        if(model_load(i) < 0 ||
           pvr_chunk_model_table_record_get(&app.scene.model_table, i,
                                              &record) < 0 ||
           require(record.morph_ordinal < MODELS) < 0)
            return -1;
        shapes[record.morph_ordinal] = &app.model[i].shape_view;
    }
    if(section(PVR_CHUNK_ASSET_SECTION_ANIMATION_CATALOG, 0,
                 &data, &bytes) < 0 ||
       pvr_chunk_animation_catalog_open(data, bytes, &catalog) < 0 ||
       pvr_chunk_animation_catalog_validate_asset(&catalog, &app.asset) < 0 ||
       pvr_chunk_animation_catalog_find(&catalog, "bend", 4, NULL,
                                          &logical_clip) < 0 ||
       section(PVR_CHUNK_ASSET_SECTION_ANIMATION,
                 logical_clip.transform_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_animation_section_open(data, bytes, &animation_view) < 0 ||
       require(animation_view.transform_count == NODES) < 0 ||
       pvr_chunk_animation_section_materialize(
           &animation_view, app.keys, 3u * CLIP_TRACKS, app.track, CLIP_TRACKS, app.transforms,
           NODES, app.visibility, NODES, &app.clip) < 0)
        return failure("clip");
    if(section(PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
                 logical_clip.morph_ordinal, &data, &bytes) < 0 ||
       pvr_chunk_morph_animation_section_open(data, bytes, &morph_view) < 0 ||
       pvr_chunk_morph_animation_section_validate_scene(
           &morph_view, &app.scene.hierarchy, &app.scene.model_table,
           shapes, MODELS) < 0 ||
       pvr_chunk_morph_animation_section_materialize(
           &morph_view, app.morph_keys, 6, app.morph_tracks, MODELS,
           app.channels, MODELS, app.bindings, MODELS, &app.morph) < 0 ||
       require(app.morph.binding_count == MODELS) < 0)
        return failure("morph-clip");
    for(i = 0; i < MODELS; ++i) {
        const pvr_chunk_morph_animation_binding_t *b = &app.bindings[i];
        model_state_t *m;
        if(require(b->model_ordinal < MODELS && b->node_index < NODES &&
                    b->channel_count == 1) < 0)
            return -1;
        m = &app.model[b->model_ordinal];
        if(require(app.nodes[b->node_index].model ==
                    &app.models[b->model_ordinal]) < 0 ||
           pvr_chunk_shape_motion_bind(&m->shape_source, b->channels, 1,
                                          &m->morph_tracks, 1) < 0)
            return failure("morph-bind");
    }
    return 0;
}

static int sample(float time) {
    anim_pose_result_t animation_result;
    anim_morph_result_t morph_result;
    pvr_deform_result_t deform_result;
    size_t i;

    /* Skeleton palettes already contain hierarchy world transforms. Do not
       transform their output by the mesh node again when rendering. */
    if(anim_clip_sample(&app.clip, time, app.local, NODES,
                          &animation_result) < 0 ||
       pvr_chunk_hierarchy_traverse_poses(
           &app.hierarchy, app.local, NODES, NULL, app.world, NODES,
           NULL, NULL, NULL) < 0 ||
       pvr_chunk_skeleton_pose_prepare_affine(app.world, NODES,
           app.affine_world, NODES, &app.affine_pose) < 0)
        return failure("hierarchy-pose");
    for(i = 0; i < MODELS; ++i) {
        model_state_t *m = &app.model[i];
        const pvr_deform_stream_t posed = {
            m->morphed, m->skin_source.vertex_count, sizeof(*m->morphed)
        };
        if(pvr_chunk_skeleton_palette_build_affine(
               &m->affine_skeleton, &app.affine_pose, m->prepared_joints,
               JOINTS, &m->prepared_palette) < 0 ||
           anim_morph_targets_sample(&m->morph_tracks, 1, time,
                                       &m->morph_target, 1,
                                       &morph_result) < 0 ||
           pvr_chunk_shape_apply(&m->shape_source, &m->morph_target, 1,
                                   m->morphed, VERTICES,
                                   &deform_result) < 0)
            return failure("deform-pose");
        if(pvr_skin_apply_spans_prepared(m->deformed, VERTICES, &posed,
               &m->skin_plan, &m->prepared_palette, &deform_result) < 0)
            return failure("skin-pose");
    }
    return 0;
}

/* Independent authored expectations: root X=.25, tip joint X rises 0..1..0,
   and the two morph curves oppose each other. The triangle's top vertex
   follows the tip; the grid blends the two joints by row. Resolve original
   indices instead of assuming dense-cache order. */
static int resolve(uint16_t index, pvr_deform_vertex_t *vertex, void *data) {
    model_state_t *m = data;
    return pvr_chunk_skin_general_pose_vertex_get(&m->pose, index, vertex);
}

#ifdef CHUNK_SCENE_GRID
#include "grid-goldens.h"
#ifdef CHUNK_SCENE_CLIP
#include "grid-clipping.h"
#endif
#define SCENE_SHADE grid_shade
#else
#define SCENE_SHADE NULL
#endif

static int check_pose(float time) {
    float bend = time <= 1.0f ? time : 2.0f - time;
    size_t i;
    size_t vertex;

    if(sample(time) < 0)
        return -1;
#ifdef CHUNK_SCENE_GRID
    const grid_oracle_t oracle = grid_oracle(time);
#endif
    for(i = 0; i < MODELS; ++i) {
        alignas(32) static const matrix_t identity = {
            { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
            { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
        };
        alignas(32) pvr_vertex_t workspace[STRIP_VERTICES];
        alignas(32) pvr_vertex_t output[PACKETS + 1u];
        const pvr_vertex_t guard = { .flags = 0xdeadbeef };
        pvr_geometry_sink_t sink;
        pvr_chunk_cache_result_t emitted;
        model_state_t *m = &app.model[i];
        float weight = i == 0 ? bend : 1.0f - bend;
        if(require(fabsf(app.model[i].morph_target.weight - weight) <
                    0.0001f) < 0)
            return failure("weight-golden");
        for(vertex = 0; vertex < VERTICES; ++vertex) {
            pvr_deform_vertex_t v;
#ifdef CHUNK_SCENE_GRID
            grid_expected_t expected = grid_expected(&oracle, vertex, weight);
            if(pvr_chunk_skin_general_pose_vertex_get(
                   &app.model[i].pose, (uint16_t)vertex, &v) < 0)
                return failure("grid-vertex-get");
            if(grid_check_vertex(&v, &expected) < 0) {
                printf("grid mismatch time=%g model=%u vertex=%u "
                       "position=(%g,%g,%g) expected=(%g,%g,%g) "
                       "normal=(%g,%g,%g) expected=(%g,%g,%g)\n",
                       (double)time, (unsigned)i, (unsigned)vertex,
                       (double)v.position.x, (double)v.position.y, (double)v.position.z,
                       expected.x, expected.y, expected.z,
                       (double)v.normal.x, (double)v.normal.y, (double)v.normal.z,
                       expected.nx, expected.ny, expected.nz);
                return failure("grid-transform-golden");
            }
#else
            float x = vertex == 0 ? -0.75f : 1.25f;
            float y = -1.0f;
            if(vertex == 2) {
                x = 0.25f + bend + 0.5f * weight;
                y = 1.0f;
            }
            if(pvr_chunk_skin_general_pose_vertex_get(
                   &app.model[i].pose, (uint16_t)vertex, &v) < 0 ||
               require(fabsf(v.position.x - x) < 0.0001f &&
                       fabsf(v.position.y - y) < 0.0001f &&
                       fabsf(v.position.z) < 0.0001f &&
                       fabsf(v.normal.z - 1.0f) < 0.0001f) < 0)
                return failure("vertex-golden");
#endif
        }
        /* Exercise the same prepared emitter on a host memory sink. Colors
           must survive deformation, and indices must still select the
           correct posed vertex even if strip order differs from input order. */
        output[PACKETS] = guard;
        if(pvr_geometry_sink_init_memory(&sink, output, PACKETS) < 0 ||
           pvr_chunk_model_cache_draw_emit(&m->draw, &identity, &sink,
                                         workspace, STRIP_VERTICES, NULL, NULL, resolve,
                                         SCENE_SHADE, m, &emitted) < 0 ||
           require(emitted.emitted_vertices == PACKETS &&
                   emitted.emitted_strips == STRIPS &&
                   memcmp(&output[PACKETS], &guard, sizeof(guard)) == 0) < 0)
            return failure("cache-golden");
        for(vertex = 0; vertex < PACKETS; ++vertex) {
            pvr_deform_vertex_t v;
            if(resolve(m->cache.source_indices[vertex], &v, m) < 0 ||
               require(fabsf(output[vertex].x - v.position.x) < 0.0001f &&
                       fabsf(output[vertex].y - v.position.y) < 0.0001f &&
                       fabsf(output[vertex].z - 1.0f) < 0.0001f &&
#ifndef CHUNK_SCENE_GRID
                       output[vertex].argb == m->cache.vertices[vertex].argb &&
#endif
                       output[vertex].flags == ((vertex + 1) % STRIP_VERTICES == 0 ?
                           PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX)) < 0)
                return failure("packet-golden");
#ifdef CHUNK_SCENE_GRID
            uint16_t source = m->cache.source_indices[vertex];
            grid_expected_t expected = grid_expected(&oracle, source, weight);
            if(grid_check_color(output[vertex].argb, m->cache.vertices[vertex].argb,
                                &expected) < 0)
                return failure("grid-lighting-golden");
            if(require(fabsf(output[vertex].u - (float)(source % 17u) / 16.0f) < 0.0001f &&
                       fabsf(output[vertex].v - (float)(source / 17u) / 16.0f) < 0.0001f) < 0)
                return failure("uv-golden");
#endif
        }
    }
    return 0;
}

#ifndef CHUNK_SCENE_HOST
static pvr_poly_hdr_t draw_header;

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    (void)strip;
    (void)data;
    /* A single application-owned opaque material policy serves both models. */
    return pvr_prim(&draw_header, sizeof(draw_header));
}

#ifdef CHUNK_SCENE_CLIP
static int clip_begin_strip(const pvr_chunk_render_state_t *state,
                            const pvr_chunk_strip_view_t *strip, void *data) {
    (void)state;
    (void)strip;
    return begin_strip(NULL, data);
}
#endif

#ifndef CHUNK_SCENE_WORKLOAD
static int render(void) {
    pvr_poly_cxt_t context;
    pvr_geometry_sink_t sink;
    pvr_pipeline_status_t pipeline;
#ifndef CHUNK_SCENE_CLIP
    alignas(32) pvr_vertex_t workspace[STRIP_VERTICES];
#endif
#ifdef CHUNK_SCENE_GRID
    pvr_txr_surface_t texture = {0};
    alignas(32) uint16_t pixels[64 * 64];
#endif
    unsigned frame;
    int scene_open = 0;
    int list_open = 0;
    int error;

    if(pvr_init_defaults() < 0)
        return -1;
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
#ifdef CHUNK_SCENE_GRID
    for(unsigned y = 0; y < 64; ++y)
        for(unsigned x = 0; x < 64; ++x)
            pixels[y * 64 + x] = ((x / 8u) ^ (y / 8u)) & 1u ? 0xffff : 0x4208;
    if(pvr_txr_surface_alloc(&texture, 64, 64, PVR_TXR_SURFACE_RGB565,
                            PVR_TXR_SURFACE_TWIDDLED, false) < 0 ||
       pvr_txr_load_ex_checked(pixels, texture.vram, 64, 64, PVR_TXRLOAD_16BPP) < 0)
        goto fail;
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                    pvr_txr_surface_pvr_format(&texture), 64, 64,
                    texture.vram, PVR_FILTER_BILINEAR);
    context.txr.env = PVR_TXRENV_MODULATE;
#ifdef CHUNK_SCENE_CLIP
    context.gen.specular = PVR_SPECULAR_ENABLE;
#endif
#else
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
#endif
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&draw_header, &context);
    if(pvr_geometry_sink_init_current(&sink) < 0)
        goto fail;
#ifdef CHUNK_SCENE_CLIP
    for(frame = 0; frame < GRID_CLIP_CASES * 24; ++frame) {
#else
    for(frame = 0; frame < 240; ++frame) {
#endif
        size_t i;
#ifdef CHUNK_SCENE_CLIP
        const float time = 2.0f * (frame % 24) / 23.0f;
#else
        const float time = (float)frame / 120.0f;
#endif
        if(sample(time) < 0 || pvr_wait_ready() < 0)
            goto fail;
        pvr_scene_begin();
        scene_open = 1;
        if(pvr_list_begin(PVR_LIST_OP_POLY) < 0)
            goto fail;
        list_open = 1;
        for(i = 0; i < MODELS; ++i) {
#ifdef CHUNK_SCENE_CLIP
            pvr_chunk_model_plan_t plan;
            pvr_frustum_t frustum;
            pvr_chunk_clip_policy_t policy;
            pvr_chunk_render_result_t result;
            grid_clip_context_t clip = { &app.model[i], 0 };
            if(grid_clip_plan(clip.model, &plan) < 0 ||
               grid_clip_frustum(frame / 24, i, &frustum, &policy) < 0 ||
               grid_clip_emit(&plan, &frustum, policy, &sink, 1, &clip,
                              clip_begin_strip, &result) < 0 ||
               require(result.emitted_vertices <= GRID_CLIP_CAPACITY) < 0)
                goto fail;
#else
            /* Only application display placement follows the world-space
               skin result. The +.25 root must not be applied twice. */
            alignas(32) matrix_t screen = {
                { 65, 0, 0, 0 }, { 0, -65, 0, 0 },
                { 0, 0, 1, 0 }, { i ? 430 : 150, 260, 1, 1 }
            };
            pvr_chunk_cache_result_t result;
            if(pvr_chunk_model_cache_draw_emit(
                   &app.model[i].draw, &screen, &sink, workspace, STRIP_VERTICES,
                   NULL, begin_strip, resolve, SCENE_SHADE, &app.model[i], &result) < 0 ||
               require(result.emitted_strips == STRIPS &&
                       result.emitted_vertices == PACKETS) < 0)
                goto fail;
#endif
        }
        if(pvr_list_finish() < 0)
            goto fail;
        list_open = 0;
        if(pvr_scene_finish() < 0)
            goto fail;
        scene_open = 0;
    }
    if(pvr_wait_render_done() < 0 ||
       pvr_get_pipeline_status(&pipeline) < 0 ||
       require(pipeline.faults.mask == PVR_FAULT_NONE) < 0)
        goto fail;
    puts("KOSSCENE rendered=1 inspecting=1");
#ifdef CHUNK_SCENE_CLIP
    puts("KOSSCENE clip_frames=144 cases=6 faults=0");
#elif defined(CHUNK_SCENE_GRID)
    puts("KOSSCENE grid_frames=240 triangles_per_frame=1024 packets_per_frame=1088 faults=0");
#endif
    thd_sleep(10000);
#ifdef CHUNK_SCENE_GRID
    pvr_txr_surface_release(&texture);
#endif
    return pvr_shutdown();
fail:
    error = errno;
    if(list_open)
        pvr_list_finish();
    if(scene_open)
        pvr_scene_finish();
    pvr_wait_render_done();
#ifdef CHUNK_SCENE_GRID
    pvr_txr_surface_release(&texture);
#endif
    pvr_shutdown();
    errno = error;
    return -1;
}
#endif
#endif

#if !defined(CHUNK_SCENE_GRID) && (defined(CHUNK_SCENE_HOST) || defined(CHUNK_SCENE_WORKLOAD))
#include "scene-workload.h"
#endif

int main(int argc, char **argv) {
    static const float times[] = { 0, 0.25f, 0.5f, 1, 1.5f, 2 };
    const void *data;
    size_t bytes;
    size_t i;
    int result = 1;
    int error;
#ifdef CHUNK_SCENE_HOST
    FILE *file;
    long length;
    void *storage;
    if(argc != 2 || !(file = fopen(argv[1], "rb")))
        return 1;
    if(fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
       fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 1;
    }
    bytes = (size_t)length;
    if(bytes > SIZE_MAX - 31u) {
        fclose(file);
        return 1;
    }
    storage = aligned_alloc(32, (bytes + 31u) & ~(size_t)31u);
    if(!storage || fread(storage, 1, bytes, file) != bytes) {
        free(storage);
        fclose(file);
        return 1;
    }
    fclose(file);
    data = storage;
#else
#ifdef CHUNK_SCENE_GRID
    extern const unsigned char skin_grid_asset_data[];
    extern const int skin_grid_asset_size;
    data = skin_grid_asset_data;
    bytes = (size_t)skin_grid_asset_size;
#else
    extern const unsigned char chunk_scene_asset_data[];
    extern const int chunk_scene_asset_size;
    data = chunk_scene_asset_data;
    bytes = (size_t)chunk_scene_asset_size;
#endif
    (void)argc;
    (void)argv;
#endif
    if(scene_load(data, bytes) < 0)
        goto out;
    for(i = 0; i < sizeof(times) / sizeof(times[0]); ++i)
        if(check_pose(times[i]) < 0)
            goto out;
    puts("KOSSCENE models=2 joints=2 morph_bindings=2 pose_goldens=6");
#ifdef CHUNK_SCENE_GRID
    puts("KOSSCENE grid_vertices=578 grid_triangles=1024 packet_guards=PASS uv_goldens=PASS");
    puts("KOSSCENE rotation_scale=PASS normal_goldens=PASS lighting_goldens=PASS");
#endif
#ifdef CHUNK_SCENE_CLIP
    if(grid_clip_check() < 0)
        goto out;
#endif
#if !defined(CHUNK_SCENE_GRID) && (defined(CHUNK_SCENE_HOST) || defined(CHUNK_SCENE_WORKLOAD))
    if(workload_check() < 0)
        goto out;
#endif
#ifndef CHUNK_SCENE_HOST
#ifdef CHUNK_SCENE_WORKLOAD
    if(render_workload() < 0)
#else
    if(render() < 0)
#endif
        goto out;
#endif
    result = 0;
out:
    error = errno;
    for(i = 0; i < MODELS; ++i)
        free(app.model[i].cache_storage);
    free(app.decode_storage);
#ifdef CHUNK_SCENE_HOST
    free(storage);
#else
    vid_clear(result ? 64 : 0, result ? 0 : 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT,
                   vid_mode->width, 1,
                   result ? "RESULT: FAIL (compact scene)" :
                            "RESULT: PASS (compact scene)");
#endif
    printf("KOSSCENE result=%s errno=%d\n", result ? "FAIL" : "PASS",
           result ? error : 0);
#ifndef CHUNK_SCENE_HOST
    for(;;)
        thd_sleep(1000);
#endif
    return result;
}
