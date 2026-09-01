/* KallistiOS ##version##

   Host-side canonical scene representation for compact assets.
   Copyright (C) 2026 Joseph Black
*/

#ifndef PVR_SCENE_IR_H
#define PVR_SCENE_IR_H

#include <stddef.h>
#include <stdint.h>

#include <dc/animation.h>
#include <dc/pvr_chunk_skeleton_asset.h>
#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_chunk_shape.h>

typedef struct pvr_scene_ir_node {
    uint32_t parent_index;
    uint32_t model_ordinal;
    float local_transform[16];
} pvr_scene_ir_node_t;

typedef struct pvr_scene_ir {
    pvr_scene_ir_node_t *nodes;
    size_t node_count;
    size_t node_capacity;
} pvr_scene_ir_t;

void pvr_scene_ir_free(pvr_scene_ir_t *scene);

int pvr_scene_ir_add_node(pvr_scene_ir_t *scene, uint32_t parent_index,
                          uint32_t model_ordinal,
                          const float local_transform[16]);

int pvr_scene_ir_add_root_model(pvr_scene_ir_t *scene,
                                uint32_t model_ordinal);

int pvr_scene_ir_validate(const pvr_scene_ir_t *scene);

int pvr_scene_ir_serialize_hierarchy(const pvr_scene_ir_t *scene,
                                     uint8_t **bytes_out,
                                     size_t *size_out);

int pvr_scene_ir_serialize_general_skin(
    const pvr_chunk_skin_general_t *skin,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_skeleton(
    const pvr_chunk_skeleton_t *skeleton,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_shapes(
    const pvr_chunk_shape_set_t *shapes,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_animation(
    const anim_clip_view_t *clip,
    uint8_t **bytes_out, size_t *size_out);

int pvr_scene_ir_serialize_volumes(
    const pvr_chunk_model_view_t *model,
    uint8_t **bytes_out, size_t *size_out);

#endif /* PVR_SCENE_IR_H */
