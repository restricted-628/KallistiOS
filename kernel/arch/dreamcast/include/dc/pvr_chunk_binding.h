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

#include <dc/pvr_chunk_render.h>
#include <dc/pvr_material.h>

/** \defgroup pvr_chunk_binding Compact-model resource binding
    \brief                           Checked texture and material resolution
    \ingroup                         pvr_chunk_render
    @{
*/

/** \brief Maximum asset texture identifier encoded by compact models. */
#define PVR_CHUNK_TEXTURE_IDENTIFIER_MAX UINT16_C(0x1fff)

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
    coordinates remain vertex-callback policy. One- and two-volume strips
    select the corresponding checked material compiler.

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

/** \brief Resolve, compile, and submit a material for one compact strip.

    This function has the exact pvr_chunk_render_begin_strip_t signature. It
    submits only one header through the established current or buffered-list
    path and never begins, changes, or finishes a scene or list.
*/
int pvr_chunk_material_binding_begin_strip(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip, void *data);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_BINDING_H */
