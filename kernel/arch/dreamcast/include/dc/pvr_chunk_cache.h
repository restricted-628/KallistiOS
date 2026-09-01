/* KallistiOS ##version##

   dc/pvr_chunk_cache.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_cache.h
    \brief   Caller-owned draw caches for admitted compact PVR models.
    \ingroup pvr_chunk_render

    A compact-model draw cache performs stream traversal, state decoding, and
    indexed vertex assembly once. Repeated draws then project and submit
    bounded PVR-native vertex runs without reparsing either compact stream.
    Storage, deformation policy, materials, textures, scenes, and lists remain
    application-owned.
*/

#ifndef __DC_PVR_CHUNK_CACHE_H
#define __DC_PVR_CHUNK_CACHE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_render.h>
#include <dc/pvr_deform.h>
#include <dc/pvr_frustum.h>

/** \addtogroup pvr_chunk_render
    @{
*/

/** \brief Required base alignment of compact-model cache storage. */
#define PVR_CHUNK_CACHE_ALIGNMENT 32u

/** \brief Current in-memory draw-cache representation version. */
#define PVR_CHUNK_CACHE_VERSION 2u

/** \brief One immutable, decoded strip in a compact-model draw cache. */
typedef struct pvr_chunk_cached_strip {
    pvr_chunk_render_state_t state; /**< Complete state at strip admission. */
    size_t first_vertex;            /**< First entry in the vertex arrays. */
    size_t vertex_count;            /**< Complete strip vertex count. */
    point_t minimum;                /**< Reference-pose AABB minimum. */
    point_t maximum;                /**< Reference-pose AABB maximum. */
    uint8_t source_type;            /**< Original pvr_chunk_strip_type_t. */
    uint8_t source_flags;           /**< Original strip flags. */
    uint16_t reserved;              /**< Must remain zero. */
} pvr_chunk_cached_strip_t;

/** \brief Exact caller-owned storage required by one draw cache. */
typedef struct pvr_chunk_cache_requirements {
    size_t alignment;
    size_t strip_count;
    size_t vertex_count;
    size_t maximum_strip_vertices;
    size_t strips_offset;
    size_t vertices_offset;
    size_t deform_vertices_offset;
    size_t source_indices_offset;
    size_t bytes;
} pvr_chunk_cache_requirements_t;

/** \brief Immutable view over a completed caller-owned draw cache.

    The structure owns no memory. Its storage and every pointer published from
    that storage must remain immutable and accessible while the cache is used.
*/
typedef struct pvr_chunk_model_cache {
    uint32_t version;
    const void *storage;
    size_t storage_bytes;
    const pvr_chunk_cached_strip_t *strips;
    size_t strip_count;
    const pvr_vertex_t *vertices;
    const pvr_deform_vertex_t *deform_vertices;
    const uint16_t *source_indices;
    size_t vertex_count;
    size_t maximum_strip_vertices;
    float center[3];
    float radius;
} pvr_chunk_model_cache_t;

/** \brief Exact storage required by one two-volume draw cache.

    `vertex_size` is 32 bytes for color packets and 64 bytes for textured
    packets. The retained packet array is tightly packed at that size.
*/
typedef struct pvr_chunk_two_volume_cache_requirements {
    size_t alignment;
    pvr_geometry_vertex_format_t format;
    size_t vertex_size;
    size_t strip_count;
    size_t vertex_count;
    size_t maximum_strip_vertices;
    size_t strips_offset;
    size_t vertices_offset;
    size_t deform_vertices_offset;
    size_t source_indices_offset;
    size_t bytes;
} pvr_chunk_two_volume_cache_requirements_t;

/** \brief Immutable view over a completed two-volume draw cache.

    The structure owns no memory. `vertices` contains tightly packed packets
    of `format`, rather than maximum-sized union entries, so color-only models
    retain their native 32-byte footprint.
*/
typedef struct pvr_chunk_two_volume_cache {
    uint32_t version;
    const void *storage;
    size_t storage_bytes;
    const pvr_chunk_cached_strip_t *strips;
    size_t strip_count;
    const void *vertices;
    const pvr_deform_vertex_t *deform_vertices;
    const uint16_t *source_indices;
    size_t vertex_count;
    size_t maximum_strip_vertices;
    pvr_geometry_vertex_format_t format;
    size_t vertex_size;
    float center[3];
    float radius;
} pvr_chunk_two_volume_cache_t;

/** \brief One retained triangle in a compact modifier-volume cache. */
typedef struct pvr_chunk_cached_modifier_triangle {
    size_t first_corner;       /**< First of three deformation/index entries. */
    size_t first_user_word;    /**< First retained triangle user word. */
    size_t user_word_count;    /**< Number of retained user words. */
    uint8_t source_type;       /**< Original pvr_chunk_volume_type_t. */
    uint8_t final_in_volume;   /**< Nonzero for the volume's last triangle. */
    uint16_t reserved;         /**< Must remain zero. */
} pvr_chunk_cached_modifier_triangle_t;

/** \brief Exact storage required by one modifier-volume draw cache. */
typedef struct pvr_chunk_modifier_cache_requirements {
    size_t alignment;
    size_t volume_count;
    size_t triangle_count;
    size_t corner_count;
    size_t user_word_count;
    size_t triangles_offset;
    size_t packets_offset;
    size_t deform_vertices_offset;
    size_t source_indices_offset;
    size_t user_words_offset;
    size_t bytes;
} pvr_chunk_modifier_cache_requirements_t;

/** \brief Immutable view over a completed modifier-volume draw cache. */
typedef struct pvr_chunk_modifier_cache {
    uint32_t version;
    const void *storage;
    size_t storage_bytes;
    const pvr_chunk_cached_modifier_triangle_t *triangles;
    const pvr_modifier_vol_t *packets;
    const pvr_deform_vertex_t *deform_vertices;
    const uint16_t *source_indices;
    const uint16_t *user_words;
    size_t volume_count;
    size_t triangle_count;
    size_t corner_count;
    size_t user_word_count;
    float center[3];
    float radius;
} pvr_chunk_modifier_cache_t;

/** \brief Progress from one cached compact-model emission. */
typedef struct pvr_chunk_cache_result {
    size_t emitted_strips;
    size_t emitted_vertices;
    size_t skipped_strips;
    size_t skipped_vertices;
} pvr_chunk_cache_result_t;

/** \brief Progress from one cached modifier-volume emission. */
typedef struct pvr_chunk_modifier_cache_result {
    size_t emitted_volumes;
    size_t emitted_triangles;
} pvr_chunk_modifier_cache_result_t;

/** \brief Prepare material state for one cached strip.

    This callback normally resolves the decoded texture identifiers and emits
    a polygon header to the same destination as the geometry sink. It runs in
    caller context immediately before the complete strip is submitted.
*/
typedef int (*pvr_chunk_cache_begin_strip_t)(
    const pvr_chunk_cached_strip_t *strip, void *data);

/** \brief Decide whether one cached strip should be emitted.

    Return a positive value to emit, zero to skip, or a negative value to
    fail. The callback runs before deformation resolution, per-frame vertex
    policy, material setup, projection, or sink publication.

    The cached bounds describe the reference pose. A resolver may move
    vertices beyond them, so dynamically deformed models must either provide
    a conservative current-pose decision, such as one built by
    pvr_deform_bounds_calculate(), or elect to emit.
*/
typedef int (*pvr_chunk_cache_filter_strip_t)(
    const pvr_chunk_cached_strip_t *strip, void *data);

/** \brief Resolve an optional deformed vertex by original model index.

    When no resolver is supplied, emission uses the cache's canonical base
    position and normal. A resolver may bind skinning, morphing, or another
    caller-owned deformation result without changing the cache.
*/
typedef int (*pvr_chunk_cache_resolve_vertex_t)(
    uint16_t source_index, pvr_deform_vertex_t *vertex, void *data);

/** \brief Apply per-frame policy to one cached vertex before projection.

    The vertex already contains cached UV and color state and the resolved
    object-space position. The deformation value supplies the corresponding
    position and normal for lighting or other per-frame policy. The command
    word is restored after the callback.
*/
typedef int (*pvr_chunk_cache_prepare_vertex_t)(
    const pvr_chunk_render_state_t *state, uint16_t source_index,
    const pvr_deform_vertex_t *deformation, pvr_vertex_t *vertex, void *data);

/** \brief Apply per-frame policy to one cached two-volume vertex.

    The member selected by \p format already contains cached UV/color state
    and the resolved object-space position. The deformation value supplies the
    corresponding normal for lighting or other policy. The command word is
    restored after the callback.
*/
typedef int (*pvr_chunk_cache_prepare_two_volume_vertex_t)(
    const pvr_chunk_render_state_t *state, uint16_t source_index,
    const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data);

/** \brief Apply per-frame policy to one cached modifier triangle.

    The three source indices and resolved deformation values follow the
    expanded triangle's winding. Retained user words are the same bounded
    words exposed by the immediate modifier callback. The packet already
    contains baked dummy fields and resolved object-space positions; its
    command is restored after the callback.
*/
typedef int (*pvr_chunk_cache_prepare_modifier_t)(
    const uint16_t source_indices[3],
    const pvr_deform_vertex_t deformations[3],
    const uint16_t *user_words, size_t user_word_count,
    pvr_modifier_vol_t *triangle, void *data);

/** \brief Revalidate one published ordinary draw-cache descriptor.

    This checks the version, complete derived layout, stored ranges, strip
    coverage, bounds, and immutable cache pointers without reading either
    original compact stream. It is useful to policy emitters layered above the
    standard cache path.
*/
int pvr_chunk_model_cache_validate(const pvr_chunk_model_cache_t *cache);

/** \brief Revalidate one published two-volume draw-cache descriptor. */
int pvr_chunk_model_two_volume_cache_validate(
    const pvr_chunk_two_volume_cache_t *cache);

/** \brief Query the exact draw-cache footprint for a prepared model.

    The complete polygon stream and every referenced prepared vertex are
    checked. The initial cache supports ordinary one-volume strips; modifier,
    two-volume, and legacy polygon-cache records report ENOTSUP. Failure
    initializes \p requirements to zero.
*/
int pvr_chunk_model_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_cache_requirements_t *requirements);

/** \brief Decode one prepared compact model into caller-owned cache storage.

    Complete structural, capacity, alignment, and overlap validation precedes
    the first storage write. \p prepare_vertex is the existing one-time compact
    vertex policy callback; it is required for intensity, non-unit-W, or bump
    data whose final PVR representation is application policy. Callback
    failure leaves \p cache unpublished and cache storage unspecified.
*/
int pvr_chunk_model_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_vertex_t prepare_vertex, void *data,
    pvr_chunk_model_cache_t *cache);

/** \brief Project and emit a completed compact-model draw cache.

    One 32-byte-aligned pvr_vertex_t workspace entry is required per vertex in
    the largest cached strip. A memory sink must have room for every cached
    vertex before emission begins. Non-memory sinks require \p begin_strip.
    Optional deformation and per-frame vertex callbacks run in caller context.
    No compact stream is read by this operation.
*/
int pvr_chunk_model_cache_emit(
    const pvr_chunk_model_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result);

/** \brief Filter, project, and emit a completed compact-model draw cache.

    This has the same validation, ownership, and callback contract as
    pvr_chunk_model_cache_emit(). A skipped strip performs no subsequent
    callback or output work. Memory sinks retain the ordinary all-strips
    capacity preflight so callback decisions cannot create partial output.
*/
int pvr_chunk_model_cache_emit_filtered(
    const pvr_chunk_model_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result);

/** \brief Query the exact two-volume cache footprint for a prepared model.

    Every strip must use one consistent two-volume output layout. Ordinary,
    modifier, bump, and legacy polygon-cache records report ENOTSUP. Failure
    initializes \p requirements to zero.
*/
int pvr_chunk_model_two_volume_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_two_volume_cache_requirements_t *requirements);

/** \brief Decode a prepared two-volume model into caller-owned storage.

    \p prepare_vertex is the existing one-time two-volume policy callback and
    remains required for intensity or non-unit-W vertex data. No storage is
    retained by KOS, and failure leaves \p cache unpublished.
*/
int pvr_chunk_model_two_volume_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex, void *data,
    pvr_chunk_two_volume_cache_t *cache);

/** \brief Project and emit a completed two-volume draw cache.

    One aligned maximum-sized workspace union is required per vertex in the
    largest strip. Output uses a format-bound vertex sink matching the cache.
    Optional deformation and per-frame policy callbacks run in caller context.
    Neither compact source stream is read.
*/
int pvr_chunk_model_two_volume_cache_emit(
    const pvr_chunk_two_volume_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result);

/** \brief Filter, project, and emit a completed two-volume draw cache.

    Filtering occurs at the same side-effect-free boundary as the ordinary
    cached path and uses the reference-pose bounds retained in each strip.
*/
int pvr_chunk_model_two_volume_cache_emit_filtered(
    const pvr_chunk_two_volume_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result);

/** \brief Classify one cached reference-pose strip bound.

    This helper performs no deformation. The frustum must contain the same
    object-to-screen transform intended for the draw.
*/
int pvr_chunk_cached_strip_classify(
    const pvr_chunk_cached_strip_t *strip, const pvr_frustum_t *frustum,
    pvr_frustum_classification_t *result);

/** \brief Query the exact modifier-volume cache footprint.

    Triangle, quad, and strip volume records are expanded during admission.
    The query validates every resulting reference and retains exact user-word
    capacity. Failure initializes \p requirements to zero.
*/
int pvr_chunk_model_modifier_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_modifier_cache_requirements_t *requirements);

/** \brief Build a caller-owned cache of expanded modifier triangles.

    \p prepare_triangle is the existing one-time modifier policy callback. It
    remains required if any referenced position has non-unit W. Failure leaves
    \p cache unpublished and cache storage unspecified.
*/
int pvr_chunk_model_modifier_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_modifier_t prepare_triangle, void *data,
    pvr_chunk_modifier_cache_t *cache);

/** \brief Revalidate one published modifier-volume draw-cache descriptor. */
int pvr_chunk_model_modifier_cache_validate(
    const pvr_chunk_modifier_cache_t *cache);

/** \brief Project and emit a completed modifier-volume draw cache.

    The existing modifier configuration determines list, culling, and final
    include/exclude mode. One aligned triangle workspace is required. Optional
    deformation resolution and per-frame triangle policy run in caller context
    without reading either compact source stream.
*/
int pvr_chunk_model_modifier_cache_emit(
    const pvr_chunk_modifier_cache_t *cache,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink, pvr_modifier_vol_t *workspace,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_cache_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_CACHE_H */
