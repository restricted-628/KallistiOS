/* KallistiOS ##version##

   dc/pvr_chunk_binding.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_binding.h
    \brief   Caller-owned compact-model texture and material binding.
    \ingroup pvr_chunk_binding
*/

#ifndef __DC_PVR_CHUNK_BINDING_H
#define __DC_PVR_CHUNK_BINDING_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_cache.h>
#include <dc/pvr_chunk_render.h>
#include <dc/pvr_lighting.h>
#include <dc/pvr_material.h>

/** \defgroup pvr_chunk_binding Compact-model resource binding
    \brief                           Checked texture and material resolution
    \ingroup                         pvr_chunk_render
    @{
*/

/** \brief One caller-owned texture binding for compact-model material state.

    Bindings map an asset identifier to an existing checked PVR texture
    surface. `palette` is zero for non-paletted surfaces, selects one of four
    8-bit palette banks, or one of 64 4-bit palette banks. The surface and its
    VRAM remain application-owned.
*/
typedef struct pvr_chunk_texture_binding {
    uint16_t identifier;
    uint8_t palette;
    const pvr_txr_surface_t *surface;
} pvr_chunk_texture_binding_t;

/** \brief Bounded sorted texture-binding table description. */
typedef struct pvr_chunk_texture_table {
    const pvr_chunk_texture_binding_t *bindings;
    size_t binding_count;
} pvr_chunk_texture_table_t;

/** \brief Admitted immutable texture-binding table view.

    The binding array, surface descriptors, and their VRAM allocations must
    remain valid and immutable while this view is used.
*/
typedef struct pvr_chunk_texture_table_view {
    pvr_chunk_texture_table_t table;
} pvr_chunk_texture_table_view_t;

/** \brief Caller-owned compact-model material submission adapter.

    Initialize with pvr_chunk_material_binding_init(), then pass this object as
    callback data to pvr_chunk_material_binding_begin_strip(). The context is
    copied at initialization; the admitted texture table remains a borrowed
    immutable view. A containing application structure may place this object
    first when the vertex callback needs additional state from the same `data`
    pointer.
*/
typedef struct pvr_chunk_material_binding {
    pvr_chunk_texture_table_view_t textures;
    pvr_poly_cxt_t context;
    pvr_geometry_sink_kind_t destination;
} pvr_chunk_material_binding_t;

/** \brief Composable view-space environment-map callback adapter.

    The adapter owns no model, matrix, material, or render storage. It copies
    the normal matrix at initialization, forwards strip setup to the required
    callback, generates UV coordinates only for strips carrying
    PVR_CHUNK_STRIP_ENVIRONMENT, and then invokes the optional vertex callback.

    The nested vertex callback runs after coordinate generation and may replace
    the generated UVs. This permits application-specific orientation, animated
    reflection policy, or lighting to compose with the standard mapping.
*/
typedef struct pvr_chunk_environment_map_binding {
    pvr_normal_matrix_t normal_matrix;
    pvr_chunk_render_begin_strip_t begin_strip;
    pvr_chunk_render_prepare_vertex_t prepare_vertex;
    void *begin_strip_data;
    void *prepare_vertex_data;
} pvr_chunk_environment_map_binding_t;

/** \brief Standard shading presets for compact-model vertices.

    Presets select reusable vertex policy, not model-stream syntax or scene
    ownership. UNLIT retains decoded colors while consuming optional vertex
    intensity. DIFFUSE evaluates ambient and signed Lambert light. The final
    preset additionally produces offset-color specular highlights.
*/
typedef enum pvr_chunk_render_policy {
    PVR_CHUNK_RENDER_POLICY_UNLIT = 0,
    PVR_CHUNK_RENDER_POLICY_DIFFUSE,
    PVR_CHUNK_RENDER_POLICY_DIFFUSE_SPECULAR
} pvr_chunk_render_policy_t;

/** \brief Optional features composed with a compact render policy. */
typedef enum pvr_chunk_render_policy_feature {
    /** Generate view-space sphere-map UVs for environment-marked strips. */
    PVR_CHUNK_RENDER_POLICY_ENVIRONMENT_MAP = 1u << 0,
    /** Multiply lit diffuse alpha by the lighting distance-cue factor. */
    PVR_CHUNK_RENDER_POLICY_DEPTH_CUE_ALPHA = 1u << 1
} pvr_chunk_render_policy_feature_t;

/** \brief Caller-owned description used to build a render-policy binding.

    Lit policies borrow an immutable light array through \a lighting and
    require \a object_to_world. Environment mapping requires
    \a object_to_view. The matrices and lighting context are copied, while the
    light array and chained callback data remain borrowed for the binding's
    lifetime. The selected policy and feature mask derive the copied lighting
    flags; flags supplied in \a lighting are deliberately ignored.
*/
typedef struct pvr_chunk_render_policy_config {
    pvr_chunk_render_policy_t policy;
    uint32_t features;
    const matrix_t *object_to_world;
    const matrix_t *object_to_view;
    const pvr_lighting_extended_context_t *lighting;
    pvr_chunk_render_begin_strip_t begin_strip;
    pvr_chunk_render_prepare_vertex_t prepare_vertex;
    void *begin_strip_data;
    void *prepare_vertex_data;
} pvr_chunk_render_policy_config_t;

/** \brief Admitted allocation-free compact render-policy callback adapter.

    Treat fields as private after initialization. The object is passed as the
    shared callback data to pvr_chunk_render_policy_binding_begin_strip() and
    pvr_chunk_render_policy_binding_prepare_vertex().
*/
typedef struct pvr_chunk_render_policy_binding {
    pvr_chunk_render_policy_t policy;
    uint32_t features;
    matrix_t object_to_world;
    pvr_normal_matrix_t world_normal_matrix;
    pvr_normal_matrix_t view_normal_matrix;
    pvr_lighting_extended_context_t lighting;
    pvr_chunk_render_begin_strip_t begin_strip;
    pvr_chunk_render_prepare_vertex_t prepare_vertex;
    void *begin_strip_data;
    void *prepare_vertex_data;
} pvr_chunk_render_policy_binding_t;

/** \brief Resolve one model identifier's global palette-bank selector.

    The callback runs only during model preparation, before PVR list emission.
    Return zero after writing the selector, or negative with errno set. A null
    callback selects palette zero for every texture. It must not mutate or
    reenter the binding or its residency cache.
*/
typedef int (*pvr_chunk_residency_palette_resolver_t)(
    uint16_t identifier, uint8_t *palette, void *data);

/** \brief Render-lifetime compact-model binding over texture residency.

    The caller provides parallel texture-binding and residency-handle arrays.
    Model preparation pins every distinct referenced texture before geometry
    emission begins. The pins remain held until the caller releases them after
    the corresponding PVR render has completed.

    Every residency slot has one surface layout. An optional preparation-time
    callback supplies per-identifier global palette-bank selectors when that
    layout is paletted. Embedded VQ codebooks do not use this selector.

    Do not copy or modify a configured object or its arrays. Operations require
    external serialization in ordinary thread context.
*/
typedef struct pvr_chunk_residency_binding {
    pvr_txr_residency_t *residency;       /**< Borrowed residency cache. */
    pvr_chunk_texture_binding_t *textures; /**< Caller-provided sorted set. */
    pvr_txr_residency_handle_t *handles;  /**< Parallel acquired handles. */
    size_t capacity;                      /**< Elements in both arrays. */
    size_t count;                         /**< Distinct pinned identifiers. */
    pvr_poly_cxt_t context;               /**< Copied base material policy. */
    pvr_geometry_sink_kind_t destination; /**< Material submission sink. */
    pvr_chunk_residency_palette_resolver_t palette_resolver; /**< Optional. */
    void *palette_data;                   /**< Opaque palette callback data. */
} pvr_chunk_residency_binding_t;

/** \brief Validate and admit a sorted caller-owned texture-binding table.

    Identifiers must be strictly increasing, unique, and within the compact
    model's 13-bit range. Every surface binding, palette selector, metadata
    range, and VRAM allocation is checked before the view changes. An empty
    table is valid.
*/
int pvr_chunk_texture_table_open(
    const pvr_chunk_texture_table_t *table,
    pvr_chunk_texture_table_view_t *view);

/** \brief Find one texture binding with a bounded binary search.

    \retval 0  The binding was found.
    \retval -1 Invalid input or absent identifier, with errno set.
*/
int pvr_chunk_texture_table_find(
    const pvr_chunk_texture_table_view_t *view, uint16_t identifier,
    const pvr_chunk_texture_binding_t **binding);

/** \brief Resolve compact draw state into one checked KOS material.

    The caller's base context supplies list, depth, fog, clipping, texture
    environment, and other policy not encoded by the model. Compact state
    supplies blend, texture, filtering, mip bias, UV, supersampling, alpha,
    flat-shading, double-sided, and specular-enable changes. Environment-map
    coordinates can use pvr_chunk_environment_map_binding_prepare_vertex() or
    another caller-selected vertex policy. One- and two-volume strips select
    the corresponding checked material compiler.

    Missing identifiers report ENOENT. Invalid model state, surface state, or
    an incompatible context leaves `material` unchanged.
*/
int pvr_chunk_material_resolve(
    pvr_material_t *material, const pvr_poly_cxt_t *base_context,
    const pvr_chunk_texture_table_view_t *textures,
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip);

/** \brief Initialize a stateless material submission adapter.

    `destination` must be a current-list or explicit buffered-list sink kind.
    The base context's encoded polygon list is used for buffered submission.
    The initializer validates the context without retaining a material.
*/
int pvr_chunk_material_binding_init(
    pvr_chunk_material_binding_t *binding,
    const pvr_poly_cxt_t *base_context,
    const pvr_chunk_texture_table_view_t *textures,
    pvr_geometry_sink_kind_t destination);

/** \brief Select strips routed to this material binding's PVR list.

    This function has the exact pvr_chunk_render_filter_strip_t signature and
    composes with the filtered raw emitters. Opaque, punch-through, and
    translucent passes can therefore reuse one admitted model without
    reparsing or splitting its authored stream.
*/
int pvr_chunk_material_binding_filter_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Cached-strip form of pvr_chunk_material_binding_filter_strip(). */
int pvr_chunk_material_binding_filter_cached_strip(
    const pvr_chunk_cached_strip_t *strip, void *data);

/** \brief Resolve, compile, and submit a material for one compact strip.

    This function has the exact pvr_chunk_render_begin_strip_t signature. It
    submits only one header through the established current or buffered-list
    path and never begins, changes, or finishes a scene or list.
*/
int pvr_chunk_material_binding_begin_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Initialize the standard compact-model environment-map adapter.

    \p object_to_view supplies the complete object-to-view transform used to
    build an inverse-transpose normal matrix. The required \p begin_strip
    callback preserves material submission while both compact callbacks share
    this adapter as their opaque data. \p prepare_vertex may be null when no
    additional vertex policy is needed.

    Failure leaves \p binding unchanged.
*/
int pvr_chunk_environment_map_binding_init(
    pvr_chunk_environment_map_binding_t *binding,
    const matrix_t *object_to_view,
    pvr_chunk_render_begin_strip_t begin_strip, void *begin_strip_data,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *prepare_vertex_data);

/** \brief Forward one strip to an environment adapter's material callback. */
int pvr_chunk_environment_map_binding_begin_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Generate environment UVs and run the chained vertex callback.

    A per-reference strip normal takes precedence over the indexed vertex
    normal, preserving hard-normal discontinuities. An environment strip with
    no normal reports ENOTSUP. Without a chained callback, intensity fields,
    non-unit position W, and an active bump basis likewise report ENOTSUP
    instead of being silently accepted by the adapter.
*/
int pvr_chunk_environment_map_binding_prepare_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_vertex_t *vertex, void *data);

/** \brief Initialize one composable compact render-policy adapter.

    Initialization validates every selected matrix, light, attenuation,
    specular, and depth-cue field before changing \p binding. The policy owns
    no render resources and performs no work until its callbacks are invoked.

    A lit policy uses per-reference normals before indexed normals, transforms
    position and normal into world space, consumes vertex intensity, and lets
    compact ambient color and exponent refine the copied lighting context.
    The encoded exponent range maps to powers 1 through 17. Environment UVs
    use the independent object-to-view normal transform.
*/
int pvr_chunk_render_policy_binding_init(
    pvr_chunk_render_policy_binding_t *binding,
    const pvr_chunk_render_policy_config_t *config);

/** \brief Forward strip setup to a render policy's material callback. */
int pvr_chunk_render_policy_binding_begin_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Apply the selected compact vertex policy and chained callback.

    Homogeneous model positions are canonicalized before lighting and before
    the emitter's later projection. Missing normals required by lighting or an
    environment-marked strip report ENOTSUP. The chained vertex callback runs
    last and may override any generated non-command field.
*/
int pvr_chunk_render_policy_binding_prepare_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_vertex_t *vertex, void *data);

/** \brief Configure a compact-model adapter over one residency cache.

    \a textures and \a handles are parallel caller-owned arrays of \a capacity
    elements and must not overlap each other, \a binding, or \a residency. No
    texture is acquired during initialization. `destination` must be a current
    or explicit buffered-list sink.

    The configured adapter creates no allocation, worker, request, transfer,
    or model state. Release every acquired pin before reinitializing it.

    \return 0 on success, or -1 with errno set.
*/
int pvr_chunk_residency_binding_init(
    pvr_chunk_residency_binding_t *binding,
    pvr_txr_residency_t *residency,
    pvr_chunk_texture_binding_t *textures,
    pvr_txr_residency_handle_t *handles, size_t capacity,
    pvr_chunk_residency_palette_resolver_t palette_resolver,
    void *palette_data,
    const pvr_poly_cxt_t *base_context,
    pvr_geometry_sink_kind_t destination);

/** \brief Pin every distinct texture referenced by one admitted model.

    Existing pins in \a binding are retained, allowing several models in one
    render to build a shared set. A missing or still-loading resident texture
    reports ENOENT or EAGAIN before geometry emission. Insufficient caller
    array capacity reports ENOSPC. Successfully acquired pins remain recorded
    if a later reference fails and must still be released by the caller.

    Call this before beginning the PVR list which will receive the model.

    \return 0 when every referenced texture is pinned, or -1 with errno set.
*/
int pvr_chunk_residency_binding_prepare_model(
    pvr_chunk_residency_binding_t *binding,
    const pvr_chunk_model_view_t *view);

/** \brief Pin one stable texture identifier in a residency adapter.

    Repeated identifiers are no-ops. Successful acquisition preserves sorted
    table order and uses the configured palette resolver. This small primitive
    lets checked external manifests share the same acquisition path as model
    scanning without duplicating residency policy.
*/
int pvr_chunk_residency_binding_prepare_identifier(
    pvr_chunk_residency_binding_t *binding, uint16_t identifier);

/** \brief Select strips routed to this residency binding's PVR list. */
int pvr_chunk_residency_binding_filter_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Cached-strip form of the residency pass filter. */
int pvr_chunk_residency_binding_filter_cached_strip(
    const pvr_chunk_cached_strip_t *strip, void *data);

/** \brief Resolve and submit one strip using pre-acquired resident textures.

    This function has the exact pvr_chunk_render_begin_strip_t signature and
    performs no residency acquisition. It therefore cannot expose partially
    emitted geometry merely because a later strip references an unavailable
    texture when prepare_model() was completed first.
*/
int pvr_chunk_residency_binding_begin_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Release all resident texture pins held by an adapter.

    The caller must wait until every render which can sample the submitted
    model geometry has completed. Successful releases are removed even if a
    corrupted or stale handle makes another release fail; failed handles remain
    recorded for diagnosis.

    \return 0 when every pin was released, or -1 with the first errno retained.
*/
int pvr_chunk_residency_binding_release(
    pvr_chunk_residency_binding_t *binding);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_BINDING_H */
