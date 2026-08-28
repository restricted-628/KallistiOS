/* KallistiOS ##version##

   dc/pvr_chunk_toon.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_toon.h
    \brief   Topology-aware band shading for prepared compact models.
    \ingroup pvr_chunk_render

    Band shading is a render policy over ordinary compact-model draw caches;
    it is deliberately not an asset record. Model geometry, deformation,
    material state, textures, scene lifetime, and all work memory retain their
    existing owners.
*/

#ifndef __DC_PVR_CHUNK_TOON_H
#define __DC_PVR_CHUNK_TOON_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_cache.h>
#include <dc/pvr_toon.h>

/** \addtogroup pvr_chunk_render
    @{
*/

/** \brief One material-independent band-shading profile.

    N thresholds define N+1 bands. Each optional modulation array therefore
    contains exactly `threshold_count + 1` packed ARGB entries. Base and offset
    colors are multiplied independently, preserving per-reference color and
    alpha discontinuities already retained by the compact cache.

    The light direction is expressed in the destination space of the normal
    matrix passed to pvr_chunk_model_cache_emit_toon().
*/
typedef struct pvr_chunk_toon_profile {
    pvr_toon_light_t light;
    pvr_toon_shade_equation_t equation;
    const float *thresholds;
    const uint32_t *argb_modulation;
    const uint32_t *oargb_modulation;
    size_t threshold_count;
    float epsilon;
} pvr_chunk_toon_profile_t;

/** \brief Caller-owned scratch storage for one cached Toon draw.

    The first four arrays require at least one entry per reference in the
    largest cached strip. `toon_triangles` requires at least
    pvr_toon_triangle_capacity() entries for the largest selected profile.
    SPLIT clipping additionally requires PVR_FRUSTUM_CLIP_MAX_VERTICES entries
    in `clip_vertices`. Vertex and deformation arrays require 32-byte base
    alignment; the other arrays require their natural four-byte alignment.
*/
typedef struct pvr_chunk_toon_workspace {
    pvr_vertex_t *vertices;
    pvr_deform_vertex_t *deformations;
    vector_t *normals;
    float *shades;
    size_t strip_capacity;
    pvr_toon_triangle_t *toon_triangles;
    size_t toon_triangle_capacity;
    pvr_vertex_t *clip_vertices;
    size_t clip_vertex_capacity;
} pvr_chunk_toon_workspace_t;

/** \brief Completed prefix from one cached band-shading draw. */
typedef struct pvr_chunk_toon_result {
    size_t visited_strips;
    size_t skipped_strips;
    size_t source_triangles;
    size_t emitted_strips;
    size_t emitted_triangles;
    size_t emitted_vertices;
    size_t generated_vertices;
} pvr_chunk_toon_result_t;

/** \brief Select a profile for one cached strip.

    The supplied profile begins as a copy of the default profile. The callback
    may replace any field with storage that remains valid until the current
    strip has completed. Returning a negative value aborts the draw with its
    complete prefix valid.
*/
typedef int (*pvr_chunk_toon_resolve_profile_t)(
    const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_toon_profile_t *profile, void *data);

/** \brief Validate one band-shading profile without retaining it. */
int pvr_chunk_toon_profile_validate(const pvr_chunk_toon_profile_t *profile);

/** \brief Emit an ordinary compact draw cache through geometric shade bands.

    The emitter resolves the current position and normal for each reference,
    applies the optional per-frame vertex callback, transforms normals in one
    checked batch, and partitions every triangle at every crossed threshold.
    Generated boundaries interpolate position, normal, floating UV, base
    color, and offset color before the selected band modulation is applied.
    Thus a binary profile produces a true hard boundary even when the original
    triangle straddles its threshold.

    Flat-shaded strips evaluate one geometric face normal and never introduce
    an internal shade boundary. IGNORE_LIGHT strips bypass both banding and
    modulation. Triangle-strip winding is preserved when independent
    triangles are generated. Frustum classification, clipping, and projection
    occur after band subdivision so generated attributes remain coherent.

    No allocation or global matrix state is used. The callback and failure
    rules match pvr_chunk_model_cache_emit_filtered(); a failure after output
    begins leaves the complete prefix described by \p result and the sink.
    `begin_strip` is required for non-memory sinks and runs only if that strip
    has visible geometry. The normal matrix must transform object-space normals
    into the same space as profile light directions. The frustum supplies the
    complete object-to-screen matrix for every clipping policy.
*/
int pvr_chunk_model_cache_emit_toon(
    const pvr_chunk_model_cache_t *cache,
    const pvr_normal_matrix_t *normal_matrix,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t clip_policy,
    const pvr_chunk_toon_profile_t *default_profile,
    pvr_geometry_sink_t *sink, pvr_chunk_toon_workspace_t *workspace,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    pvr_chunk_toon_resolve_profile_t resolve_profile,
    void *data, pvr_chunk_toon_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_TOON_H */
