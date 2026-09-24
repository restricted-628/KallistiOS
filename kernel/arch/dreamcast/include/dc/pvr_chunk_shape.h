/* KallistiOS ##version##

   dc/pvr_chunk_shape.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_shape.h
    \brief   Explicit morph-target binding for admitted compact models.
    \ingroup pvr_chunk_model

    Shape targets are sparse, deterministic sidecars over a compact model.
    Binding proves every delta names a base vertex. A caller-owned source
    workspace then expands those targets once into the dense representation
    consumed by the existing animation and deformation layers.
*/

#ifndef __DC_PVR_CHUNK_SHAPE_H
#define __DC_PVR_CHUNK_SHAPE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/animation.h>
#include <dc/pvr_chunk_model.h>
#include <dc/pvr_deform.h>

/** \addtogroup pvr_chunk_model
    @{ */

/** \brief Marker for an absent entry in a caller-owned shape lookup. */
#define PVR_CHUNK_SHAPE_INDEX_NONE UINT32_MAX

/** \brief One canonical sparse morph delta keyed by model vertex index.

    Records in one target are strictly increasing by vertex_index. Position
    and normal XYZ deltas must be finite, both W components and reserved are
    zero. Zero deltas are valid, allowing deterministic converter output
    without a special omission rule.
*/
typedef struct pvr_chunk_shape_delta {
    uint16_t vertex_index;
    uint16_t reserved;
    pvr_morph_delta_t delta;
} pvr_chunk_shape_delta_t;

/** \brief One bounded sparse morph target. */
typedef struct pvr_chunk_shape_target {
    const pvr_chunk_shape_delta_t *deltas;
    size_t delta_count;
} pvr_chunk_shape_target_t;

/** \brief Bounded collection of morph targets for one model. */
typedef struct pvr_chunk_shape_set {
    const pvr_chunk_shape_target_t *targets;
    size_t target_count;
} pvr_chunk_shape_set_t;

/** \brief Storage required to bind and expand one shape set. */
typedef struct pvr_chunk_shape_requirements {
    size_t alignment;
    size_t lookup_entries;
    size_t lookup_bytes;
    size_t source_vertices;
    size_t source_targets;
    size_t source_bytes;
} pvr_chunk_shape_requirements_t;

/** \brief Immutable checked association between a model and shape targets.

    The binding owns no memory. The prepared model plan, shape target arrays,
    and dense_lookup remain caller-owned and immutable at their original
    addresses for the binding lifetime.
*/
typedef struct pvr_chunk_shape_binding {
    pvr_chunk_model_plan_t plan;
    pvr_chunk_shape_set_t shapes;
    const uint32_t *dense_lookup;
    size_t dense_lookup_count;
} pvr_chunk_shape_binding_t;

/** \brief Canonical base vertices and target-major dense morph deltas.

    Both arrays occupy one caller-owned, 32-byte-aligned workspace. Target N
    begins at `deltas + N * vertex_count`.
*/
typedef struct pvr_chunk_shape_source {
    const pvr_deform_vertex_t *vertices;
    const pvr_morph_delta_t *deltas;
    size_t vertex_count;
    size_t target_count;
} pvr_chunk_shape_source_t;

/** \brief One optional scalar weight track and finite fallback weight. */
typedef struct pvr_chunk_shape_channel {
    const anim_track_view_t *weight;
    float fallback_weight;
} pvr_chunk_shape_channel_t;

/** \brief Indexed view of one completed morphed pose. */
typedef struct pvr_chunk_shape_pose {
    const pvr_chunk_shape_binding_t *binding;
    const pvr_deform_vertex_t *vertices;
    size_t vertex_count;
} pvr_chunk_shape_pose_t;

/** \brief Query exact caller-owned storage for one model and target count. */
int pvr_chunk_shape_query(const pvr_chunk_model_plan_t *plan,
                          size_t target_count,
                          pvr_chunk_shape_requirements_t *requirements);

/** \brief Validate and bind sparse shape targets to a prepared model.

    Every target is checked completely before dense_lookup or binding is
    modified. Each sparse record must identify a vertex present in the model.
*/
int pvr_chunk_shape_bind(const pvr_chunk_model_plan_t *plan,
                         const pvr_chunk_shape_set_t *shapes,
                         uint32_t *dense_lookup,
                         size_t dense_lookup_capacity,
                         pvr_chunk_shape_binding_t *binding);

/** \brief Expand bound targets into reusable canonical source storage.

    Base vertices are decoded once in ascending model-index order. Sparse
    target records become target-major dense delta arrays with zero-filled
    entries for vertices not named by a target.
*/
int pvr_chunk_shape_source_build(
    const pvr_chunk_shape_binding_t *binding, void *workspace,
    size_t workspace_bytes, pvr_chunk_shape_source_t *source);

/** \brief Bind weight channels to a dense shape source.

    The output can be sampled directly with anim_morph_targets_sample(). No
    keyframe data is copied or owned. Complete preflight precedes publication.
*/
int pvr_chunk_shape_motion_bind(
    const pvr_chunk_shape_source_t *source,
    const pvr_chunk_shape_channel_t *channels, size_t channel_count,
    anim_morph_target_tracks_t *tracks, size_t track_capacity);

/** \brief Apply sampled targets produced for this dense shape source.

    Each sampled target must retain the exact dense-delta pointer and stride
    published by pvr_chunk_shape_motion_bind(). This detects accidental target
    reordering before pvr_morph_apply() publishes output.
*/
int pvr_chunk_shape_apply(const pvr_chunk_shape_source_t *source,
                          const pvr_morph_target_t *targets,
                          size_t target_count,
                          pvr_deform_vertex_t *output,
                          size_t output_capacity,
                          pvr_deform_result_t *result);

/** \brief Resolve one original model index in a completed morphed pose. */
int pvr_chunk_shape_pose_vertex_get(const pvr_chunk_shape_pose_t *pose,
                                    uint16_t vertex_index,
                                    pvr_deform_vertex_t *vertex);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_SHAPE_H */
