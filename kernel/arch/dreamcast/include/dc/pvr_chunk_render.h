/* KallistiOS ##version##

   dc/pvr_chunk_render.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_render.h
    \brief   Bounded compact-model projection and geometry emission.
    \ingroup pvr_chunk_render

    This interface connects admitted compact model streams to the checked PVR
    geometry layer without owning scenes, lists, textures, or model memory.
*/

#ifndef __DC_PVR_CHUNK_RENDER_H
#define __DC_PVR_CHUNK_RENDER_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model.h>
#include <dc/pvr_frustum.h>
#include <dc/pvr_geometry.h>

/** \defgroup pvr_chunk_render Compact-model emission
    \brief                          Checked compact-model render bridge
    \ingroup                        pvr_chunk_model
    @{
*/

/** \brief Fields present in a decoded compact-model draw state. */
typedef enum pvr_chunk_render_state_field {
    PVR_CHUNK_RENDER_BLEND = 1u << 0,
    PVR_CHUNK_RENDER_MIPMAP_ADJUST = 1u << 1,
    PVR_CHUNK_RENDER_SPECULAR_EXPONENT = 1u << 2,
    PVR_CHUNK_RENDER_TEXTURE = 1u << 3,
    PVR_CHUNK_RENDER_DIFFUSE = 1u << 4,
    PVR_CHUNK_RENDER_AMBIENT = 1u << 5,
    PVR_CHUNK_RENDER_SPECULAR = 1u << 6,
    PVR_CHUNK_RENDER_BUMP_BASIS = 1u << 7
} pvr_chunk_render_state_field_t;

/** \brief One decoded texture reference from a compact polygon stream.

    The identifier is an asset-level reference, not a PVR memory address. The
    strip callback resolves it to an application-owned texture and material.
*/
typedef struct pvr_chunk_texture_state {
    uint16_t identifier;
    uint8_t filter;
    uint8_t supersample;
    uint8_t uv_flip;
    uint8_t uv_clamp;
    uint8_t mipmap_adjust;
} pvr_chunk_texture_state_t;

/** \brief Render state in effect for one compact-model strip. */
typedef struct pvr_chunk_render_state {
    uint32_t present;             /**< Fields in the outside-volume state. */
    pvr_blend_mode_t blend_source;
    pvr_blend_mode_t blend_destination;
    uint8_t mipmap_adjust;
    uint8_t specular_exponent;
    uint8_t strip_flags;
    pvr_chunk_texture_state_t texture;
    uint32_t diffuse_argb;
    uint32_t ambient_argb;
    uint32_t specular_argb;
    vector_t bump_direction;      /**< Signed-normalized bump direction. */
    vector_t bump_up;             /**< Signed-normalized bump up vector. */
    uint32_t secondary_present;   /**< Fields in the inside-volume state. */
    uint8_t secondary_specular_exponent;
    pvr_chunk_texture_state_t secondary_texture;
    uint32_t secondary_diffuse_argb;
    uint32_t secondary_ambient_argb;
    uint32_t secondary_specular_argb;
} pvr_chunk_render_state_t;

/** \brief Maximum-sized workspace entry for two-volume emission.

    Untextured strips use `color`; textured strips use `textured`. The emitter
    packs entries according to the sink format, so callers allocate one union
    per vertex in the largest strip without performing byte-size arithmetic.
*/
typedef union pvr_chunk_two_volume_vertex {
    pvr_vertex_pcm_t color;
    pvr_vertex_tpcm_t textured;
} pvr_chunk_two_volume_vertex_t;

/** \brief Progress from one compact-model emission. */
typedef struct pvr_chunk_render_result {
    size_t consumed_records;
    size_t emitted_strips;
    size_t emitted_vertices;
} pvr_chunk_render_result_t;

/** \brief Frustum policy for compact-model triangle emission. */
typedef enum pvr_chunk_clip_policy {
    /** Clip intersecting triangles and interpolate canonical attributes. */
    PVR_CHUNK_CLIP_SPLIT = 0,
    /** Emit only triangles wholly inside all six frustum planes. */
    PVR_CHUNK_CLIP_DROP,
    /** Skip classification and trust that all model geometry is visible. */
    PVR_CHUNK_CLIP_ASSUME_VISIBLE
} pvr_chunk_clip_policy_t;

/** \brief Classify an admitted model's retained bounding sphere.

    This is an allocation-free whole-model visibility test. A dynamic
    deformation path must ensure the retained center and radius conservatively
    enclose the current pose before using an OUTSIDE result to skip rendering.
    pvr_deform_bounds_calculate() produces a safe current-pose sphere for
    direct use with pvr_frustum_classify_sphere().
*/
int pvr_chunk_model_classify(
    const pvr_chunk_model_view_t *view, const pvr_frustum_t *frustum,
    pvr_frustum_classification_t *result);

/** \brief Modifier-volume header policy for compact volume records. */
typedef struct pvr_chunk_modifier_config {
    pvr_list_t list;             /**< PVR_LIST_OP_MOD or PVR_LIST_TR_MOD. */
    pvr_cull_mode_t culling;     /**< Culling encoded in every header. */
    uint32_t final_mode;         /**< Include-last or exclude-last mode. */
} pvr_chunk_modifier_config_t;

/** \brief Progress from compact modifier-volume emission. */
typedef struct pvr_chunk_modifier_result {
    size_t consumed_records;
    size_t emitted_volumes;
    size_t emitted_triangles;
} pvr_chunk_modifier_result_t;

/** \brief Resolve state and prepare the active PVR material for one strip.

    A non-memory sink requires this callback. It normally resolves texture
    identifiers, compiles or selects an ordinary or two-volume material, and
    submits its polygon header to the same destination as the geometry sink.
    For two-volume emission, the unprefixed and `secondary_*` fields describe
    the outside- and inside-volume parameter sets respectively. Return zero to
    continue or negative to fail with errno set.
*/
typedef int (*pvr_chunk_render_begin_strip_t)(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** \brief Apply application-specific vertex policy before projection.

    The supplied canonical vertex already contains position, UV, and decoded
    packed colors when those have an unambiguous mapping. The callback may use
    normals, intensities, metadata, or triangle user words to replace any
    non-command field. Return zero to continue or negative to fail with errno
    set. A callback is required when a referenced vertex contains intensity
    attributes or a position W other than one because their interpretation is
    application policy and canonical `pvr_vertex_t` carries only XYZ.
*/
typedef int (*pvr_chunk_render_prepare_vertex_t)(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_vertex_t *vertex, void *data);

/** \brief Apply application policy to one complete two-volume vertex.

    \p format selects the valid union member. Both outside- and inside-volume
    attributes have already been decoded before the callback. The command word
    is restored afterward, so callbacks may change only position, UV, color,
    offset-color, and dummy fields. Return zero to continue or negative to fail
    with errno set.
*/
typedef int (*pvr_chunk_render_prepare_two_volume_vertex_t)(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data);

/** \brief Apply application policy to one expanded modifier triangle.

    The three decoded source vertices and bounded per-triangle user words are
    supplied before projection. The callback may replace coordinates or dummy
    fields in \p triangle; its command is restored afterward.
*/
typedef int (*pvr_chunk_render_prepare_modifier_t)(
    const pvr_chunk_vertex_attributes_t vertices[3],
    const uint16_t *user_words, size_t user_word_count,
    pvr_modifier_vol_t *triangle, void *data);

/** \brief Project and emit an admitted compact model.

    The complete polygon stream and destination capacity are preflighted before
    the first callback or sink write. Ordinary one-volume strips are supported.
    Modifier volumes, two-volume records, and cached-polygon controls fail with
    ENOTSUP before any output. Bump-material records expose their decoded
    signed-normalized direction and up vectors through the render state. A
    prepare callback is required while that state is active because light
    direction and bump strength remain application policy.

    One aligned workspace entry is required per vertex in the largest strip.
    It must not overlap the model streams, matrix, or a memory sink. A memory
    sink must likewise remain separate from the model and matrix. Workspace may
    be reused immediately after return. Reversed strips swap their first two
    references so triangle winding is preserved. A failure after emission
    begins leaves a complete prefix described by \p result and by
    pvr_geometry_sink_t::emitted_vertices.

    \param view             Immutable view returned by pvr_chunk_model_open().
    \param object_to_screen Complete matrix consumed by pvr_geometry_project().
    \param sink             Prepared geometry destination.
    \param workspace        32-byte-aligned canonical vertex workspace.
    \param workspace_count  Workspace capacity in vertices; at least
                            view->info.maximum_strip_vertices.
    \param begin_strip      Material/state callback; required for PVR sinks.
    \param prepare_vertex   Optional application vertex policy callback.
    \param data             Opaque callback data.
    \param result           Optional progress destination.

    \retval 0  Every supported strip was emitted.
    \retval -1 Invalid, unsupported, insufficient, or callback failure with
               errno set.
*/
int pvr_chunk_model_emit(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Project and emit through a prepared constant-time vertex plan.

    Rendering, callback, workspace, sink, and failure semantics match
    pvr_chunk_model_emit(). The plan replaces repeated vertex-stream searches
    with direct page-indexed resolution and must remain immutable during the
    call.
*/
int pvr_chunk_model_emit_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Emit an admitted model under an explicit clipping policy.

    SPLIT and DROP first classify the model's retained sphere. An outside model
    returns successfully without walking either stream; an inside model uses
    the ordinary strip path. Intersecting models expand each source strip into
    independent triangles. SPLIT clips and interpolates UV/base/offset color;
    DROP omits every triangle not wholly inside the frustum.

    SPLIT requires a 32-byte-aligned clip workspace with at least
    PVR_FRUSTUM_CLIP_MAX_VERTICES entries. DROP and ASSUME_VISIBLE do not use
    that workspace. The ordinary strip workspace and all other callback,
    destination, and failure-prefix contracts match pvr_chunk_model_emit().
*/
int pvr_chunk_model_emit_clipped(
    const pvr_chunk_model_view_t *view, const pvr_frustum_t *frustum,
    pvr_chunk_clip_policy_t policy, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_vertex_t *clip_workspace, size_t clip_workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Emit a clipped model through a prepared vertex plan. */
int pvr_chunk_model_emit_clipped_prepared(
    const pvr_chunk_model_plan_t *plan, const pvr_frustum_t *frustum,
    pvr_chunk_clip_policy_t policy, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_vertex_t *clip_workspace, size_t clip_workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Project and emit admitted two-volume compact-model strips.

    Ordinary state records describe the outside-volume parameter set; their
    two-volume variants describe the inside-volume set exposed through the
    `secondary_*` state fields. Index-only two-volume strips require a
    `PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR` sink. Two-UV strips require a
    `PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED` sink. Mixing those layouts in one
    call is rejected during preflight.

    All records, indices, callback requirements, workspace, sink capacity, and
    memory overlap are checked before the first callback or output. Modifier
    geometry, ordinary strips, bump material, and cached-polygon controls are
    not interpreted by this path. One aligned maximum-sized workspace entry is
    required per vertex in the largest strip. The emitter allocates nothing
    and does not own scene, list, texture, material, or model lifetime.

    \retval 0  Every two-volume strip was emitted.
    \retval -1 Invalid, unsupported, insufficient, or callback failure with
               errno set.
*/
int pvr_chunk_model_emit_two_volume(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Emit two-volume strips through a prepared vertex plan. */
int pvr_chunk_model_emit_two_volume_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result);

/** \brief Project and emit compact modifier-volume geometry.

    Triangle records emit directly, quads split into two winding-preserving
    triangles, and strips expand into independent alternating-winding
    triangles. Each source volume record is terminated with \p config's include
    or exclude mode; preceding triangles use the other-polygon mode.

    The sink must use `PVR_GEOMETRY_VERTEX_MODIFIER`. A non-memory sink also
    receives one compiled modifier header before each triangle and must target
    the modifier list named by \p config. Memory sinks receive projected
    `pvr_modifier_vol_t` packets only. Complete topology, capacity, workspace,
    configuration, and overlap validation precede callbacks or output.
*/
int pvr_chunk_model_emit_modifiers(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result);

/** \brief Emit modifier volumes through a prepared vertex plan. */
int pvr_chunk_model_emit_modifiers_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result);

/** \brief Emit modifier volumes with topology-preserving near-plane warping.

    This variant uses the matrix and near plane stored in \p frustum. Vertices
    crossing the near plane are warped to it rather than clipped, so the
    source volume remains closed. Side and far clipping remain PVR policy.
    All other validation, callback, sink, and progress contracts match
    pvr_chunk_model_emit_modifiers().
*/
int pvr_chunk_model_emit_modifiers_warped(
    const pvr_chunk_model_view_t *view, const pvr_frustum_t *frustum,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result);

/** \brief Emit warped modifier volumes through a prepared vertex plan. */
int pvr_chunk_model_emit_modifiers_warped_prepared(
    const pvr_chunk_model_plan_t *plan, const pvr_frustum_t *frustum,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_RENDER_H */
