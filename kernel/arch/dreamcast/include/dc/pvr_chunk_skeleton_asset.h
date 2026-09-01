/* KallistiOS ##version##

   dc/pvr_chunk_skeleton_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_skeleton_asset.h
    \brief   Serialized skeleton bindings for compact assets.
    \ingroup pvr_chunk_model

    A skeleton section maps skin joint ordinals to hierarchy nodes and retains
    their inverse-bind matrices. The pointer-free section is materialized into
    caller-owned records and can build the existing position/normal palette
    from one completed hierarchy pose.
*/

#ifndef __DC_PVR_CHUNK_SKELETON_ASSET_H
#define __DC_PVR_CHUNK_SKELETON_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_deform.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PSK1` at the start of a skeleton section. */
#define PVR_CHUNK_SKELETON_SECTION_MAGIC UINT32_C(0x314b5350)

/** \brief Current serialized skeleton version. */
#define PVR_CHUNK_SKELETON_SECTION_VERSION 1u

/** \brief Fixed serialized skeleton header size. */
#define PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES 48u

/** \brief Fixed serialized joint record size. */
#define PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES 80u

/** \brief One materialized joint-to-hierarchy binding. */
typedef struct pvr_chunk_skeleton_joint {
    size_t node_index;
    matrix_t inverse_bind;
} pvr_chunk_skeleton_joint_t;

/** \brief Caller-owned skeleton used to build skin palettes. */
typedef struct pvr_chunk_skeleton {
    const pvr_chunk_skeleton_joint_t *joints;
    size_t joint_count;
    size_t node_count;
} pvr_chunk_skeleton_t;

/** \brief Checked immutable view of one serialized skeleton. */
typedef struct pvr_chunk_skeleton_section_view {
    const void *data;
    size_t size;
    const void *joints;
    size_t joint_count;
    size_t node_count;
    uint16_t version;
} pvr_chunk_skeleton_section_view_t;

/** \brief Parse and completely validate one skeleton section.

    Framing, checksums, reserved bytes, joint/node counts, unique node
    bindings, and every finite inverse-bind component are validated before
    \p view changes. The source bytes remain caller-owned and immutable.
*/
int pvr_chunk_skeleton_section_open(
    const void *data, size_t size,
    pvr_chunk_skeleton_section_view_t *view);

/** \brief Decode one admitted joint record by ordinal. */
int pvr_chunk_skeleton_section_joint_get(
    const pvr_chunk_skeleton_section_view_t *view, size_t index,
    pvr_chunk_skeleton_joint_t *joint);

/** \brief Materialize an admitted skeleton into caller-owned records.

    All capacities, alignments, and overlaps are checked before output is
    modified. The resulting skeleton borrows \p joints.
*/
int pvr_chunk_skeleton_section_materialize(
    const pvr_chunk_skeleton_section_view_t *view,
    pvr_chunk_skeleton_joint_t *joints, size_t joint_capacity,
    pvr_chunk_skeleton_t *skeleton);

/** \brief Build a skin palette from hierarchy world matrices.

    For each joint, the position matrix is `world[node] * inverse_bind`; its
    inverse-transpose 3x3 becomes the corresponding normal matrix. Inputs and
    capacities are completely preflighted before either output array changes.
    The resulting palette borrows both caller-owned output arrays.
*/
int pvr_chunk_skeleton_palette_build(
    const pvr_chunk_skeleton_t *skeleton,
    const matrix_t *world_matrices, size_t world_capacity,
    matrix_t *position_matrices, size_t position_capacity,
    pvr_normal_matrix_t *normal_matrices, size_t normal_capacity,
    pvr_skin_palette_t *palette);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_SKELETON_ASSET_H */
