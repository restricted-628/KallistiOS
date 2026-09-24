/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/pvr_chunk_uv.h
    \brief Caller-owned independent UVs for Compact strip references.
    \ingroup pvr_chunk_render
*/
#ifndef __DC_PVR_CHUNK_UV_H
#define __DC_PVR_CHUNK_UV_H
#include <dc/pvr_chunk_render.h>

__BEGIN_DECLS
/** \addtogroup pvr_chunk_render
    @{
*/
/** \brief One finite, independent UV pair; repeating coordinates are valid. */
typedef struct pvr_chunk_uv {
    float u, v;
} pvr_chunk_uv_t;

/** \brief Derived strip index; filled only by source_init(). */
typedef struct pvr_chunk_uv_strip {
    size_t word_offset;
    size_t first_uv;
    size_t vertex_count;
} pvr_chunk_uv_strip_t;

/** \brief Immutable UV source borrowing a model, index and coordinate array.

    Coordinates follow source strip order, then authored reference order, before
    reversed-strip winding correction. Each reference has its own pair, even
    when several references use the same vertex ID. This preserves UV seams.
    Every ordinary strip is covered. Keep all borrowed storage and this view
    immutable/alive during use. No texture ownership or wire format is implied.
*/
typedef struct pvr_chunk_uv_source {
    const pvr_chunk_model_view_t *model;
    const pvr_chunk_uv_strip_t *strips;
    const pvr_chunk_uv_t *uv;
    size_t strip_count, uv_count;
} pvr_chunk_uv_source_t;

/** \brief Query index and coordinate counts for an ordinary model.
    Revalidates the model and rejects unsupported execution/strip families.
    Both non-NULL, distinct outputs are preserved on failure. No allocation.
*/
int pvr_chunk_uv_source_query(const pvr_chunk_model_view_t *model,
                              size_t *strip_count, size_t *uv_count);

/** \brief Bind exact per-reference coordinates and build the caller's index.

    index_capacity must cover the queried strip count; uv_count must match
    exactly. All UVs must be finite. Writable index/output may not overlap each
    other, the UVs, model view or model streams. Full admission precedes writes;
    every failure preserves index/output. No allocation or automatic attachment.
*/
int pvr_chunk_uv_source_init(const pvr_chunk_model_view_t *model,
                             const pvr_chunk_uv_t *uv, size_t uv_count,
                             pvr_chunk_uv_strip_t *index, size_t index_capacity,
                             pvr_chunk_uv_source_t *output);

/** \brief Emit ordinary geometry with independent UVs before policy/clipping.

    Uses the existing clipped/filtered renderer, including SPLIT, DROP and
    ASSUME_VISIBLE. UVs are selected before the vertex callback, so a material
    layer's affine/tint policy can consume them. Geometry, winding, colors and
    deformation rules otherwise remain unchanged. Filtered strips retain their
    source identity; no emitted-vertex counter is used as an attribute key.
    Optional plan must belong to the same model. NULL plan uses stream lookup.
    Workspaces, sink storage and result must not overlap any borrowed UV data.
    Existing clipping, callback, bounds and complete-prefix contracts apply.
    UV admission/overlap failures preserve result; after admission the ordinary
    emitter initializes and updates it with complete-prefix progress.
    Replaying a recipe still requires identical geometry/depth/coverage policy.
*/
int pvr_chunk_model_emit_uv(
    const pvr_chunk_uv_source_t *uv, const pvr_chunk_model_plan_t *plan,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t policy,
    pvr_geometry_sink_t *sink, pvr_vertex_t *workspace, size_t workspace_count,
    pvr_vertex_t *clip_workspace, size_t clip_workspace_count,
    pvr_chunk_render_filter_strip_t filter_strip,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** @} */
__END_DECLS
#endif
