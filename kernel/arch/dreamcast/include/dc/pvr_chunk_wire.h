/* KallistiOS ##version##

   dc/pvr_chunk_wire.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_wire.h
    \brief   Wireframe policies for prepared compact models.
    \ingroup pvr_chunk_render

    Wireframe drawing is an optional render policy over ordinary compact-model
    caches. It adds no model record, retained renderer, scene ownership, or
    allocation.
*/

#ifndef __DC_PVR_CHUNK_WIRE_H
#define __DC_PVR_CHUNK_WIRE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_cache.h>

/** \addtogroup pvr_chunk_render
    @{
*/

/** \brief Edge set selected from one triangle strip. */
typedef enum pvr_chunk_wire_topology {
    /** Every unique edge in strip-reference topology, including diagonals. */
    PVR_CHUNK_WIRE_MESH = 0,
    /** Only the outside boundary of the strip-reference topology. */
    PVR_CHUNK_WIRE_BOUNDARY,
    /** Only consecutive references, forming the strip's central path. */
    PVR_CHUNK_WIRE_PATH
} pvr_chunk_wire_topology_t;

/** \brief Source of line endpoint colors. */
typedef enum pvr_chunk_wire_color_mode {
    /** Use the profile's constant base and offset colors. */
    PVR_CHUNK_WIRE_COLOR_PROFILE = 0,
    /** Preserve the prepared or per-frame endpoint colors. */
    PVR_CHUNK_WIRE_COLOR_VERTEX
} pvr_chunk_wire_color_mode_t;

/** \brief Per-strip wireframe policy. */
typedef struct pvr_chunk_wire_profile {
    float width;                       /**< Positive screen-space pixels. */
    uint32_t argb;                     /**< Constant packed base color. */
    uint32_t oargb;                    /**< Constant packed offset color. */
    pvr_chunk_wire_topology_t topology;
    pvr_chunk_wire_color_mode_t color_mode;
} pvr_chunk_wire_profile_t;

/** \brief Caller-owned resolved-strip storage. */
typedef struct pvr_chunk_wire_workspace {
    pvr_vertex_t *vertices;
    pvr_deform_vertex_t *deformations;
    size_t strip_capacity;
} pvr_chunk_wire_workspace_t;

/** \brief Completed prefix from one prepared-cache wireframe draw. */
typedef struct pvr_chunk_wire_result {
    size_t visited_strips;
    size_t skipped_strips;
    size_t source_edges;
    size_t clipped_edges;
    size_t dropped_edges;
    size_t emitted_edges;
    size_t emitted_vertices;
} pvr_chunk_wire_result_t;

/** \brief Select a wireframe profile for one cached strip. */
typedef int (*pvr_chunk_wire_resolve_profile_t)(
    const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_wire_profile_t *profile, void *data);

/** \brief Validate one wireframe profile without retaining it. */
int pvr_chunk_wire_profile_validate(const pvr_chunk_wire_profile_t *profile);

/** \brief Query worst-case memory-sink vertices for a complete cache.

    The result covers PVR_CHUNK_WIRE_MESH for every strip. Boundary and path
    modes require no more storage. Clipping and degenerate edges can only
    reduce actual output.
*/
int pvr_chunk_model_cache_wire_capacity(
    const pvr_chunk_model_cache_t *cache, size_t *vertices);

/** \brief Emit one ordinary prepared cache as constant-width line quads.

    Current deformation and optional per-frame vertex policy are resolved once
    per strip. The selected reference-topology edges are clipped and projected,
    expanded into four-vertex screen-space quads, and emitted through the
    established geometry sink. SPLIT clips crossing segments, DROP rejects
    them, and ASSUME_VISIBLE projects without classification.

    A memory sink must retain the worst-case free capacity returned by
    pvr_chunk_model_cache_wire_capacity(), even when a narrower per-strip
    topology is selected. Non-memory sinks require \p begin_strip, which runs
    once immediately before the strip's first visible edge. The polygon header
    emitted there must disable culling. Every pointer remains caller-owned and
    no state is retained after return.
*/
int pvr_chunk_model_cache_emit_wire(
    const pvr_chunk_model_cache_t *cache, const pvr_frustum_t *frustum,
    pvr_chunk_clip_policy_t clip_policy,
    const pvr_chunk_wire_profile_t *default_profile,
    pvr_geometry_sink_t *sink, pvr_chunk_wire_workspace_t *workspace,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    pvr_chunk_wire_resolve_profile_t resolve_profile,
    void *data, pvr_chunk_wire_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_WIRE_H */
