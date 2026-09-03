/* KallistiOS ##version##

   Host-side canonical scene representation for compact assets.
   Copyright (C) 2026 Joseph Black
*/

#ifndef PVR_SCENE_IR_H
#define PVR_SCENE_IR_H

#include <stddef.h>
#include <stdint.h>

#include <dc/animation.h>
#include <dc/pvr_chunk_animation_catalog.h>
#include <dc/pvr_chunk_morph_animation_asset.h>
#include <dc/pvr_chunk_model_table.h>
#include <dc/pvr_chunk_skeleton_asset.h>
#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_chunk_shape.h>

typedef struct pvr_scene_ir_node {
    uint32_t parent_index;
    uint32_t model_ordinal;
    uint32_t flags;
    float local_transform[16];
} pvr_scene_ir_node_t;

typedef struct pvr_scene_ir {
    pvr_scene_ir_node_t *nodes;
    size_t node_count;
    size_t node_capacity;
} pvr_scene_ir_t;

typedef struct pvr_scene_ir_animation_clip {
    const char *name;
    uint32_t transform_ordinal;
    uint32_t morph_ordinal;
    float start_time;
    float end_time;
} pvr_scene_ir_animation_clip_t;

/* Import-time polygon execution operations. The canonical target contains
   only ordinary model draws; these values are never serialized. */
typedef enum pvr_scene_ir_draw_operation {
    PVR_SCENE_IR_DRAW_MODEL = 0,
    PVR_SCENE_IR_CAPTURE_MODEL,
    PVR_SCENE_IR_DRAW_CAPTURED_MODEL
} pvr_scene_ir_draw_operation_t;

/** Sentinel used for a command field which does not select a capture slot. */
#define PVR_SCENE_IR_CAPTURE_NONE UINT32_MAX

/* One resolved import-time execution command. DRAW_MODEL and CAPTURE_MODEL
   require node_index/model_ordinal. DRAW_CAPTURED_MODEL requires both to be
   UINT32_MAX and selects a previously populated capture_slot. */
typedef struct pvr_scene_ir_draw_command {
    pvr_scene_ir_draw_operation_t operation;
    uint32_t node_index;
    uint32_t model_ordinal;
    uint32_t capture_slot;
} pvr_scene_ir_draw_command_t;

void pvr_scene_ir_free(pvr_scene_ir_t *scene);

int pvr_scene_ir_add_node(pvr_scene_ir_t *scene, uint32_t parent_index,
                          uint32_t model_ordinal,
                          const float local_transform[16]);

int pvr_scene_ir_add_node_flags(pvr_scene_ir_t *scene,
                                uint32_t parent_index,
                                uint32_t model_ordinal, uint32_t flags,
                                const float local_transform[16]);

int pvr_scene_ir_add_root_model(pvr_scene_ir_t *scene,
                                uint32_t model_ordinal);

int pvr_scene_ir_validate(const pvr_scene_ir_t *scene);

/* Lower a resolved draw schedule into ordinary parent-before-child nodes.
   Source nodes become stable transform-only pose anchors. Scheduled model
   draws become identity children in exact command order, so animation remains
   bound once to the source topology. Capture operations never reach PCM2. */
int pvr_scene_ir_canonicalize_draw_schedule(
    const pvr_scene_ir_t *source,
    const pvr_scene_ir_draw_command_t *commands, size_t command_count,
    pvr_scene_ir_t *canonical);

int pvr_scene_ir_serialize_hierarchy(const pvr_scene_ir_t *scene,
                                     uint8_t **bytes_out,
                                     size_t *size_out);

int pvr_scene_ir_serialize_general_skin(
    const pvr_chunk_skin_general_t *skin,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_skeleton(
    const pvr_chunk_skeleton_t *skeleton,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_model_table(
    const pvr_chunk_model_table_record_t *records, size_t model_count,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_shapes(
    const pvr_chunk_shape_set_t *shapes,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_animation(
    const anim_clip_view_t *clip,
    uint8_t **bytes_out, size_t *size_out);

/* Serialize one source-node clip for a canonicalized draw-proxy scene. The
   first source_node_count bindings are preserved. Every later identity proxy
   receives an identity transform and aliases its source parent's visibility
   channel, keeping the serialized transform count equal to the hierarchy. */
int pvr_scene_ir_serialize_animation_for_scene(
    const anim_clip_view_t *clip, const pvr_scene_ir_t *canonical_scene,
    size_t source_node_count, uint8_t **bytes_out, size_t *size_out);

/* Remap each source node/model morph binding onto every matching surviving
   draw proxy. Channel data remains borrowed while the ordinary serializer
   emits the canonical section. A schedule with no surviving matching draw
   returns success with no section. */
int pvr_scene_ir_serialize_morph_animation_for_scene(
    const pvr_chunk_morph_animation_t *animation,
    const pvr_scene_ir_t *canonical_scene, size_t source_node_count,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_morph_animation(
    const pvr_chunk_morph_animation_t *animation,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_animation_catalog(
    const pvr_scene_ir_animation_clip_t *clips, size_t clip_count,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_volumes(
    const pvr_chunk_model_view_t *model,
    uint8_t **bytes_out, size_t *size_out);

#endif /* PVR_SCENE_IR_H */
