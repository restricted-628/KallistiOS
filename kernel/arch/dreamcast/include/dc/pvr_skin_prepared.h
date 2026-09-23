/* KallistiOS ##version##

   dc/pvr_skin_prepared.h
   Copyright (C) 2026 Joseph Black
*/

/** \file dc/pvr_skin_prepared.h
    \brief Caller-owned SH4ZAM skin palettes prepared once per pose.
    \ingroup pvr_deform
*/

#ifndef __DC_PVR_SKIN_PREPARED_H
#define __DC_PVR_SKIN_PREPARED_H

#include <dc/pvr_deform.h>
#include <dc/sh4zam.h>

__BEGIN_DECLS

/** \brief One imported joint; populated only by palette preparation. */
typedef struct pvr_skin_prepared_joint {
    shz_mat4x4_t position;
    shz_mat3x3_t normal;
} pvr_skin_prepared_joint_t;

/** \brief Immutable snapshot referencing caller-owned imported joints.

    Initialize only with pvr_skin_palette_prepare() or
    pvr_chunk_skeleton_palette_build_affine(). Neither the descriptor
    nor its joint storage may change during application. Source KOS matrices
    are copied, not borrowed, and may be changed or released after preparation.
    Reprepare after sampling a new pose; reuse the snapshot across all meshes
    sharing that palette. Do not serialize this runtime representation.
*/
typedef struct pvr_skin_prepared_palette {
    const pvr_skin_prepared_joint_t *joints;
    size_t joint_count;
    uint32_t version;
} pvr_skin_prepared_palette_t;

/** \brief Validate and import one palette into reusable SH4ZAM storage.

    Supply at least palette->joint_count naturally aligned joint records.
    Source arrays, storage, input descriptor, and output descriptor must not
    overlap any destination. All matrices are validated before any destination
    write; storage and prepared remain unchanged on failure. No allocation or
    XMTRX modification occurs. Preparation is required for every changed pose,
    not necessarily every draw.
*/
int pvr_skin_palette_prepare(const pvr_skin_palette_t *palette,
    pvr_skin_prepared_joint_t *storage, size_t joint_capacity,
    pvr_skin_prepared_palette_t *prepared);

/** \brief Skin four-influence vertices with an immutable prepared palette.

    Mirrors pvr_skin_apply(), but skips palette component scans and per-active-
    influence matrix imports. Weight/index validation, mutable source checking,
    output range/overlap checks, normalization, valid-prefix errors, and XMTRX
    preservation remain. Output must not overlap the prepared descriptor or
    joint storage. The snapshot must have been prepared successfully and remain
    intact; its version marker is not a substitute for that lifetime contract.
*/
int pvr_skin_apply_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

/** \brief Variable-span equivalent of pvr_skin_apply_prepared_palette(). */
int pvr_skin_apply_spans_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_span_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

/** \brief One normalized four-slot record; populated only by preparation.

    The active mask preserves originally positive weights even if division
    rounds them to zero. Do not edit or construct these records by hand.
*/
typedef struct pvr_skin_prepared_influence {
    uint16_t joint[4];
    uint32_t active_mask;
    float weight[4];
} pvr_skin_prepared_influence_t;

/** \brief Immutable four-weight plan in caller-owned storage.

    Original strided influence records are copied, not borrowed. The plan and
    its records must remain alive and immutable throughout application. Rebuild
    after changing weights, indices, vertex order/count or palette joint count.
    A palette may change pose without rebuilding this plan, provided its joint
    count and index meaning stay the same. This is not a serialized format.
*/
typedef struct pvr_skin_prepared_influences {
    const pvr_skin_prepared_influence_t *influences;
    size_t vertex_count;
    size_t joint_count;
    uint32_t version;
} pvr_skin_prepared_influences_t;

/** \brief Validate and normalize fixed-four weights once per mesh.

    Supply naturally aligned storage for influences->vertex_count records.
    All weights and active indices are checked before any destination write;
    storage and prepared remain unchanged on failure. Sources and their
    descriptor must not overlap either destination, and destinations must not
    overlap each other. Zero-weight slots ignore their joint index, matching
    pvr_skin_apply(). Division order matches that API. No memory is allocated.
*/
int pvr_skin_influences_prepare(const pvr_skin_stream_t *influences,
    size_t joint_count, pvr_skin_prepared_influence_t *storage,
    size_t vertex_capacity, pvr_skin_prepared_influences_t *prepared);

/** \brief Skin with both an immutable weight plan and prepared pose palette.

    Counts must match the current vertex stream and palette. No weight scans,
    weight normalization divisions, palette component scans or matrix imports
    occur during application. Changing vertex data, arithmetic results, output
    capacities and overlaps remain checked. Exact canonical in-place vertex
    processing, valid-prefix errors and XMTRX preservation match pvr_skin_apply().
    Output must not overlap either prepared descriptor or backing array. Result
    storage must be disjoint from all inputs and output, as must any descriptor
    that the caller intends to retain unchanged. Version markers do not replace
    successful preparation and immutable lifetime requirements.
*/
int pvr_skin_apply_prepared(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_influences_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

/** \brief Runtime span into a prepared plan's normalized weight records. */
typedef struct pvr_skin_prepared_span {
    size_t first_weight;
    size_t weight_count;
} pvr_skin_prepared_span_t;

/** \brief Exact array capacities for a variable-span influence plan. */
typedef struct pvr_skin_span_plan_requirements {
    size_t span_count;
    size_t weight_count;
} pvr_skin_span_plan_requirements_t;

/** \brief Immutable, copied variable-span weights for one mesh.

    Every stored weight was positive in the source, even if its normalized
    value rounded to zero. Shared/overlapping source spans are expanded into
    independent normalized runs, preserving source order and repeated joints.
    Only originally zero weights are omitted. Both arrays and this descriptor
    must remain alive and immutable throughout application. Rebuild after
    changing influences, vertex order/count, or palette joint count/meaning.
    The original arrays are not borrowed. This is not a serialized format.
*/
typedef struct pvr_skin_prepared_spans {
    const pvr_skin_prepared_span_t *spans;
    const pvr_skin_weight_t *weights;
    size_t vertex_count;
    size_t weight_count;
    size_t joint_count;
    uint32_t version;
} pvr_skin_prepared_spans_t;

/** \brief Validate spans and query exact prepared capacities.

    Counts include only positive source weights, counted separately per span.
    Requirements remain unchanged on failure and must not overlap the input
    descriptor or arrays. Unreferenced source weights are ignored, matching
    pvr_skin_apply_spans(). No allocation or XMTRX modification occurs.
*/
int pvr_skin_spans_prepare_query(const pvr_skin_span_stream_t *influences,
    size_t joint_count, pvr_skin_span_plan_requirements_t *requirements);

/** \brief Validate, copy and normalize variable-span weights once per mesh.

    Query capacities with pvr_skin_spans_prepare_query(). Supply naturally
    aligned arrays of those sizes. All sources are checked again before any
    destination write; inputs must remain stable throughout preparation.
    Destination arrays and descriptor must be mutually disjoint and must not
    overlap any input descriptor/array. All destinations remain unchanged on
    failure. No original storage is retained, and no memory is allocated.
*/
int pvr_skin_spans_prepare(const pvr_skin_span_stream_t *influences,
    size_t joint_count, pvr_skin_prepared_span_t *spans, size_t span_capacity,
    pvr_skin_weight_t *weights, size_t weight_capacity,
    pvr_skin_prepared_spans_t *prepared);

/** \brief Skin with immutable variable-span weights and a prepared palette.

    Counts must match the current vertex stream and palette. Spans, weights
    and indices are not rescanned, and weights are not renormalized. Every
    stored weight executes, including positive-source weights rounded to zero.
    Dynamic source checking, arithmetic, valid-prefix errors, canonical
    in-place processing and XMTRX preservation match pvr_skin_apply_spans().
    Output must not overlap prepared descriptors or backing arrays. Result
    storage must be disjoint from all inputs/output. Successful preparation
    and immutable lifetime requirements cannot be replaced by version markers.
*/
int pvr_skin_apply_spans_prepared(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_spans_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

/** \brief Optional affine-only palette: 48-byte position plus 36-byte normal.

    Position is SH4ZAM's column-major 3x4 layout, not three packed row vectors.
    This is a distinct runtime ABI; do not cast existing prepared joints to it.
*/
typedef struct pvr_skin_compact_joint {
    shz_mat3x4_t position;
    shz_mat3x3_t normal;
} pvr_skin_compact_joint_t;

/** \brief Immutable compact snapshot in caller-owned, naturally aligned storage.

    Produced only by pvr_skin_palette_prepare_compact() or
    pvr_chunk_skeleton_palette_build_compact(). Rebuild after any pose change;
    reuse across meshes sharing the palette. No source arrays are borrowed.
*/
typedef struct pvr_skin_compact_palette {
    const pvr_skin_compact_joint_t *joints;
    size_t joint_count;
    uint32_t version;
} pvr_skin_compact_palette_t;

/** \brief Copy an affine palette without retaining the fixed fourth row.

    Same capacity, disjoint-storage, finite-input and unchanged-on-failure
    contract as pvr_skin_palette_prepare(). In addition, every position matrix
    must have exact bottom row [0, 0, 0, 1]. Normal matrices are copied as given,
    not replaced by the position matrix's linear part. XMTRX is untouched.
*/
int pvr_skin_palette_prepare_compact(const pvr_skin_palette_t *palette,
    pvr_skin_compact_joint_t *storage, size_t joint_capacity,
    pvr_skin_compact_palette_t *prepared);

/** \brief Apply a compact palette using an existing prepared variable-span plan.

    The lifetime, counts, valid-prefix, finite-source, normal-normalization,
    canonical in-place and disjoint result/output contracts match
    pvr_skin_apply_spans_prepared(). No palette component or weight/index scans
    occur in the influence loop. Position uses three SH4ZAM four-component dot
    products; normal uses its 3x3 transform. No XMTRX load/save, per-influence
    4x4 expansion, allocation, or callbacks. Caller XMTRX is preserved.
    This optional storage-saving path is not a hardware speedup guarantee.
*/
int pvr_skin_apply_spans_compact(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_spans_t *influences,
    const pvr_skin_compact_palette_t *palette, pvr_deform_result_t *result);

/** \brief Apply a compact palette using an existing prepared fixed-four plan.

    Matches pvr_skin_apply_prepared() for input/output, lifetime, counts,
    overlap, valid-prefix errors, normalization and canonical in-place use.
    The prepared active mask controls execution: originally positive weights
    still execute if normalization rounded them to zero, while originally
    zero slots ignore their indices. No weight/index/component rescans or
    per-influence 4x4 expansion; uses the same XMTRX-preserving SH4ZAM math as
    pvr_skin_apply_spans_compact(). Result must be disjoint from all arrays and
    descriptors. No allocations or callbacks. Existing APIs remain unchanged.
*/
int pvr_skin_apply_compact(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_influences_t *influences,
    const pvr_skin_compact_palette_t *palette, pvr_deform_result_t *result);

__END_DECLS
#endif /* __DC_PVR_SKIN_PREPARED_H */
