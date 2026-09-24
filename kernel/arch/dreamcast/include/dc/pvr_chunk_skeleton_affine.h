/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/

/** \file dc/pvr_chunk_skeleton_affine.h
    \brief Prepared affine skeleton composition using SH4ZAM 3x4 matrices.

    These are caller-owned runtime snapshots, not serialized asset formats.
    The general 4x4 skeleton API remains available for non-affine inputs.
*/
#ifndef __DC_PVR_CHUNK_SKELETON_AFFINE_H
#define __DC_PVR_CHUNK_SKELETON_AFFINE_H

#include <dc/pvr_chunk_skeleton_asset.h>
#include <dc/pvr_chunk_model.h>
#include <dc/pvr_skin_prepared.h>

__BEGIN_DECLS

/** \brief An admitted node binding with a compact inverse-bind transform. */
typedef struct pvr_chunk_skeleton_affine_joint {
    size_t node_index;
    shz_mat3x4_t inverse_bind;
} pvr_chunk_skeleton_affine_joint_t;

/** \brief Immutable snapshot produced only by affine skeleton preparation. */
typedef struct pvr_chunk_skeleton_affine {
    const pvr_chunk_skeleton_affine_joint_t *joints;
    size_t joint_count;
    size_t node_count;
} pvr_chunk_skeleton_affine_t;

/** \brief Immutable snapshot of one completed affine hierarchy pose. */
typedef struct pvr_chunk_skeleton_affine_pose {
    const shz_mat3x4_t *world;
    size_t node_count;
} pvr_chunk_skeleton_affine_pose_t;

/** \brief Admitted topology for one node in a complete animated pose. */
typedef struct pvr_chunk_hierarchy_affine_node {
    size_t parent_index;
    uint32_t flags;
} pvr_chunk_hierarchy_affine_node_t;

/** \brief Immutable caller-owned topology snapshot; contains no asset pointers. */
typedef struct pvr_chunk_hierarchy_affine {
    const pvr_chunk_hierarchy_affine_node_t *nodes;
    size_t node_count;
} pvr_chunk_hierarchy_affine_t;

/** \brief Admit and copy topology once for compact TRS pose evaluation.

    Requires nonempty parent-before-child topology and known flags. Hidden
    nodes still produce transforms. PRUNE_CHILDREN is rejected with ENOTSUP:
    this producer needs a complete pose, not a partially visited render tree.
    Model pointers, user data and static local matrices are not consumed; a
    complete local TRS array is required by the evaluator. Outputs remain
    unchanged on failure. Source nodes may be released after preparation.
*/
int pvr_chunk_hierarchy_affine_prepare(const pvr_chunk_hierarchy_t *hierarchy,
    pvr_chunk_hierarchy_affine_node_t *storage, size_t capacity,
    pvr_chunk_hierarchy_affine_t *prepared);

/** \brief Evaluate an admitted hierarchy directly into compact world matrices.

    Computes parent * (translation * rotation * scale), or root * local for
    each root node. NULL root selects identity. Component suppression matches
    the general TRS traversal; even suppressed source components must be valid.
    Quaternions are normalized; translation/scale w components are ignored.
    Zero scale is allowed here (the normal-palette builder rejects singularity).

    Topology is trusted and not rescanned. Dynamic TRS inputs and arithmetic
    results are checked once as each node is evaluated. Caller-owned output
    storage and descriptor must be disjoint from each other and all inputs.
    Input arrays/topology must remain immutable throughout the call.

    Boundary errors leave outputs unchanged. Once evaluation begins, the
    descriptor is cleared and storage is scratch: a later invalid TRS or
    overflow leaves it partially written and the descriptor empty. Discard
    any older pose descriptors referring to this storage before calling.
    Only success publishes a consumable pose. No allocation or callbacks;
    XMTRX is saved/restored once for the entire batch, including failures.
*/
int pvr_chunk_hierarchy_pose_build_affine(
    const pvr_chunk_hierarchy_affine_t *hierarchy,
    const anim_transform_t *local, size_t local_capacity,
    const shz_mat3x4_t *root, shz_mat3x4_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_pose_t *prepared);

/** \brief Validate and copy inverse-bind matrices once per skeleton.

    Requires finite matrices with exact bottom row [0, 0, 0, 1] and valid node
    indices. Copies into naturally aligned caller storage; source records may
    then be released. All outputs remain unchanged on failure. No allocation
    or XMTRX modification occurs. Reprepare after changing the skeleton.
*/
int pvr_chunk_skeleton_affine_prepare(const pvr_chunk_skeleton_t *skeleton,
    pvr_chunk_skeleton_affine_joint_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_t *prepared);

/** \brief Validate and pack a completed world pose once per pose change.

    Requires node_count > 0 and the same finite/affine contract as above.
    Copying drops the fixed fourth row; it does not approximate perspective
    matrices. Outputs remain unchanged on failure and XMTRX is untouched.
    Reuse this pose across every skeleton referring to the same hierarchy.
*/
int pvr_chunk_skeleton_pose_prepare_affine(const matrix_t *world,
    size_t node_count, shz_mat3x4_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_pose_t *prepared);

/** \brief Build a prepared skin palette directly from admitted 3x4 snapshots.

    Both snapshots must have been produced by the preparation functions above
    (or a successful compact hierarchy pose build)
    and remain immutable, including backing storage, during this call. Counts
    must match. No repeated input-matrix or joint-index scans are performed.
    Output storage is caller-owned and naturally aligned, with at least one
    record per joint, disjoint from all inputs and the output descriptor.

    Computes world[node] * inverse_bind using fused SH4ZAM XMTRX composition.
    The existing robust inverse-transpose calculation handles nonuniform scale
    and shear. Singular/nonfinite results leave all outputs unchanged. XMTRX
    is saved/restored once for the complete batch, including error exits.
    Does not allocate, invoke callbacks, or change FPSCR modes. This is an
    ordinary FPU-context operation, not an interrupt-safe cache of XMTRX.

    The resulting palette plugs directly into pvr_skin_apply*_prepared APIs.
    It retains their existing 4x4/3x3 joint layout; the compact representation
    is used for skeleton/pose inputs, not a changed public or on-disk ABI.
*/
int pvr_chunk_skeleton_palette_build_affine(
    const pvr_chunk_skeleton_affine_t *skeleton,
    const pvr_chunk_skeleton_affine_pose_t *pose,
    pvr_skin_prepared_joint_t *storage, size_t capacity,
    pvr_skin_prepared_palette_t *prepared);

/** \brief Build the optional compact position/normal palette directly.

    Same input, ownership, unchanged-on-failure and batch XMTRX-preservation
    contract as pvr_chunk_skeleton_palette_build_affine(). Retains the robust
    inverse-transpose normal calculation. Its output is consumed by
    pvr_skin_apply_spans_compact(), not the original prepared-palette APIs.
*/
int pvr_chunk_skeleton_palette_build_compact(
    const pvr_chunk_skeleton_affine_t *skeleton,
    const pvr_chunk_skeleton_affine_pose_t *pose,
    pvr_skin_compact_joint_t *storage, size_t capacity,
    pvr_skin_compact_palette_t *prepared);

__END_DECLS
#endif
