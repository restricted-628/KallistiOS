/* KallistiOS ##version##

   dc/pvr_chunk_skin.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_skin.h
    \brief   Explicit skinning data for admitted compact models.
    \ingroup pvr_chunk_model

    Compact skinning is order-independent. Each admitted model vertex has one
    record containing up to four joint influences. The binding stage proves
    complete coverage and builds a caller-owned constant-time index; the
    source stage decodes canonical vertices once for repeated deformation.
*/

#ifndef __DC_PVR_CHUNK_SKIN_H
#define __DC_PVR_CHUNK_SKIN_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model.h>
#include <dc/pvr_deform.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Exact sum required across four unsigned-normalized weights. */
#define PVR_CHUNK_SKIN_WEIGHT_SUM UINT32_C(65535)

/** \brief Marker for an absent entry in a caller-owned skin lookup. */
#define PVR_CHUNK_SKIN_INDEX_NONE UINT32_MAX

/** \brief One compact, canonical four-joint influence record.

    Records are sorted by vertex index. Active weights are unsigned-normalized
    values whose exact sum is PVR_CHUNK_SKIN_WEIGHT_SUM. A zero-weight slot has
    a zero joint index, and reserved is zero. These canonical rules make the
    table deterministic and byte-comparable.
*/
typedef struct pvr_chunk_skin_influence {
    uint16_t vertex_index;
    uint16_t joint[4];
    uint16_t weight[4];
    uint16_t reserved;
} pvr_chunk_skin_influence_t;

/** \brief Bounded caller-owned compact influence table. */
typedef struct pvr_chunk_skin {
    const pvr_chunk_skin_influence_t *influences;
    size_t influence_count;
    size_t joint_count;
} pvr_chunk_skin_t;

/** \brief Storage required to bind and canonicalize one compact skin. */
typedef struct pvr_chunk_skin_requirements {
    size_t alignment;
    size_t lookup_entries;
    size_t lookup_bytes;
    size_t source_vertices;
    size_t source_bytes;
} pvr_chunk_skin_requirements_t;

/** \brief Immutable checked association between a model plan and skin table.

    The binding owns no memory. The prepared plan, compact influence table,
    and dense_lookup array remain caller-owned and immutable at their original
    addresses for the binding lifetime.
*/
typedef struct pvr_chunk_skin_binding {
    pvr_chunk_model_plan_t plan;
    pvr_chunk_skin_t skin;
    const uint32_t *dense_lookup;
    size_t dense_lookup_count;
} pvr_chunk_skin_binding_t;

/** \brief Canonical base vertices and floating-point influences built once.

    Both arrays occupy one caller-owned, 32-byte-aligned workspace. This source
    can be passed to pvr_chunk_skin_apply() for every sampled joint palette;
    compact records are not decoded again per frame.
*/
typedef struct pvr_chunk_skin_source {
    const pvr_deform_vertex_t *vertices;
    const pvr_skin_influences_t *influences;
    size_t vertex_count;
    size_t joint_count;
} pvr_chunk_skin_source_t;

/** \brief Indexed view of one completed deformed pose. */
typedef struct pvr_chunk_skin_pose {
    const pvr_chunk_skin_binding_t *binding;
    const pvr_deform_vertex_t *vertices;
    size_t vertex_count;
} pvr_chunk_skin_pose_t;

/** \brief Query exact caller-owned storage for one model plan.

    lookup_bytes is separate persistent binding storage. source_bytes is a
    32-byte-aligned canonical source workspace built once and then retained.
*/
int pvr_chunk_skin_query(const pvr_chunk_model_plan_t *plan,
                         pvr_chunk_skin_requirements_t *requirements);

/** \brief Validate and bind explicit influences to every model vertex.

    The table must contain exactly one strictly increasing record for every
    admitted vertex. Joint indices and normalized weights are completely
    checked before dense_lookup or binding is modified.
*/
int pvr_chunk_skin_bind(const pvr_chunk_model_plan_t *plan,
                        const pvr_chunk_skin_t *skin,
                        uint32_t *dense_lookup,
                        size_t dense_lookup_capacity,
                        pvr_chunk_skin_binding_t *binding);

/** \brief Decode one bound skin into reusable canonical source storage.

    Vertex positions and normals are decoded once. Formats without an explicit
    normal receive the deterministic positive-Z fallback used only by the
    deformation kernel; prelit render paths may ignore it.
*/
int pvr_chunk_skin_source_build(
    const pvr_chunk_skin_binding_t *binding, void *workspace,
    size_t workspace_bytes, pvr_chunk_skin_source_t *source);

/** \brief Apply one joint palette to a reusable compact skin source. */
int pvr_chunk_skin_apply(const pvr_chunk_skin_source_t *source,
                         const pvr_skin_palette_t *palette,
                         pvr_deform_vertex_t *output,
                         size_t output_capacity,
                         pvr_deform_result_t *result);

/** \brief Resolve one original model index in a completed deformed pose. */
int pvr_chunk_skin_pose_vertex_get(const pvr_chunk_skin_pose_t *pose,
                                   uint16_t vertex_index,
                                   pvr_deform_vertex_t *vertex);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_SKIN_H */
