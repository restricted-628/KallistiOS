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
    PVR_CHUNK_RENDER_SPECULAR = 1u << 6
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
    uint32_t present;
    pvr_blend_mode_t blend_source;
    pvr_blend_mode_t blend_destination;
    uint8_t mipmap_adjust;
    uint8_t specular_exponent;
    uint8_t strip_flags;
    pvr_chunk_texture_state_t texture;
    uint32_t diffuse_argb;
    uint32_t ambient_argb;
    uint32_t specular_argb;
} pvr_chunk_render_state_t;

/** \brief Progress from one compact-model emission. */
typedef struct pvr_chunk_render_result {
    size_t consumed_records;
    size_t emitted_strips;
    size_t emitted_vertices;
} pvr_chunk_render_result_t;

/** \brief Resolve state and prepare the active PVR material for one strip.

    A non-memory sink requires this callback. It normally resolves
    pvr_chunk_texture_state_t::identifier, compiles or selects a material, and
    submits its polygon header to the same destination as the geometry sink.
    Return zero to continue or negative to fail with errno set.
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

/** \brief Project and emit an admitted compact model.

    The complete polygon stream and destination capacity are preflighted before
    the first callback or sink write. Ordinary one-volume strips are supported.
    Modifier volumes, two-volume records, cached-polygon controls, and bump
    materials fail with ENOTSUP before any output.

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
    \param workspace_count  Workspace capacity in vertices.
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

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_RENDER_H */
