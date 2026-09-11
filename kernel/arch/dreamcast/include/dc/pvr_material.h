/* KallistiOS ##version##

   dc/pvr_material.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_material.h
    \brief   Checked compilation of caller-owned PVR material state.
    \ingroup pvr_material

    Existing PVR contexts remain the editable description of hardware state.
    This interface validates one complete context and publishes an immutable,
    submission-ready material packet without allocating memory, retaining a
    texture, or taking ownership of a scene.

    Checked texture admission includes supported filter/list combinations:
    trilinear phases require mipmaps and are rejected on the punch-through
    list. Mipmapped textures must be square and twiddled (including VQ).
    Both texture states of a two-volume material obey the same rules.
    Disabled texture fields are ignored. Raw pvr_*_compile interfaces remain
    available for callers that need direct header encoding.
*/

#ifndef __DC_PVR_MATERIAL_H
#define __DC_PVR_MATERIAL_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>

#include <dc/pvr.h>

/** \defgroup pvr_material Checked materials
    \brief                   Immutable compiled PVR material packets
    \ingroup                 pvr_geometry
    @{
*/

/** \brief Primitive family represented by a compiled material. */
typedef enum pvr_material_kind {
    PVR_MATERIAL_POLYGON = 0,   /**< Ordinary polygon header. */
    PVR_MATERIAL_SPRITE,        /**< Sprite header. */
    PVR_MATERIAL_TWO_VOLUME     /**< Modifier-affected polygon header. */
} pvr_material_kind_t;

/** \brief Caller-owned, submission-ready material packet.

    A successful compilation replaces the entire value. Failed compilation
    leaves it unchanged. The embedded header occupies exactly one TA block;
    the remaining fields are CPU-side metadata and must not be submitted as
    part of that block.
*/
typedef struct pvr_material {
    pvr_poly_hdr_t header;       /**< Compiled 32-byte TA header. */
    pvr_list_t list;             /**< Polygon list encoded by the header. */
    pvr_material_kind_t kind;    /**< Primitive family encoded by the header. */
} pvr_material_t;

/** \brief Compile and validate an ordinary polygon material.

    The source context must select an opaque, translucent, or punch-through
    polygon list and must not enable modifier-volume fields. Texture sizes,
    addresses, formats, and every packed enum are checked before output is
    changed. Compile flags use PVR_COMPILE_*.

    \retval 0  Material compiled successfully.
    \retval -1 Invalid state, with errno set to EINVAL.
*/
int pvr_material_compile_polygon(pvr_material_t *material,
                                 const pvr_poly_cxt_t *context,
                                 uint32_t compile_flags);

/** \brief Compile and validate a sprite material.

    PVR_COMPILE_SUPERSAMPLE is the only accepted compile flag.

    \retval 0  Material compiled successfully.
    \retval -1 Invalid state, with errno set to EINVAL.
*/
int pvr_material_compile_sprite(pvr_material_t *material,
                                const pvr_sprite_cxt_t *context,
                                uint32_t compile_flags);

/** \brief Compile and validate a two-volume polygon material.

    The source must enable normal modifier-volume behavior and describes both
    the outside and inside material states. Compile flags may independently
    enable texture supersampling for the two volume states.

    \retval 0  Material compiled successfully.
    \retval -1 Invalid state, with errno set to EINVAL.
*/
int pvr_material_compile_two_volume(pvr_material_t *material,
                                    const pvr_poly_cxt_t *context,
                                    uint32_t compile_flags);

/** \brief Submit one material header to the currently open PVR list.

    This function submits only the embedded 32-byte header. It does not begin,
    end, or change a scene or list.

    \retval 0  Header accepted by the established PVR submission path.
    \retval -1 Invalid material or unavailable submission state, with errno
               set appropriately.
*/
int pvr_material_submit(const pvr_material_t *material);

/** \brief Required vertex data for a compound-material step. */
typedef enum pvr_material_pass_role {
    PVR_MATERIAL_PASS_SURFACE = 0, /**< Ordinary surface colors and UVs. */
    PVR_MATERIAL_PASS_BUMP, /**< Black base RGB; oargb from pvr_pack_bump(). */
    PVR_MATERIAL_PASS_RESOLVE /**< Matching coverage/depth; colors/UVs unused. */
} pvr_material_pass_role_t;

/** \brief One material header and its vertex-data contract. */
typedef struct pvr_material_recipe_pass {
    pvr_material_t material;
    pvr_material_pass_role_t role;
} pvr_material_recipe_pass_t;

/** \brief Caller-owned compound material with at most three ordered steps.

    Each step is a header followed by geometry, not a scene/render pass.
    Use canonical packed-color, float-UV pvr_vertex_t packets. Coverage,
    clipping, winding and reciprocal depth must match across steps. Bump and
    surface UVs may differ; resolve vertices reuse either geometry.

    Presort (autosort disabled) is required. A translucent recipe must be
    contiguous per surface: do not batch all objects' first steps together.
    Intervening secondary-buffer users would overwrite live intermediates.

    Opaque recipes have an opaque seed and translucent completion at equal
    depth. Submit opaque seeds first, then completions before unrelated
    transparency. Coincident surfaces at exactly equal depth need caller
    disambiguation. No new VRAM buffer, allocator or renderer is introduced.

    \warning Physical-console composition/order validation is still required.
             Host arithmetic and packet tests do not certify raster behavior.
*/
typedef struct pvr_material_recipe {
    size_t pass_count;
    bool requires_presort;
    pvr_material_recipe_pass_t passes[3];
} pvr_material_recipe_t;

/** \brief Compile complementary trilinear phases and their composition.

    Select opaque or translucent polygons and a mipmapped non-bump texture.
    Opaque output uses two steps; translucent output accumulates both phases
    into the secondary buffer, then resolves RGBA using the source blend
    factors. Geometry and textures remain caller-owned through completion.

    Supported profiles require packed colors, float UVs, no modifiers, no
    fog/color clamp, and ordinary source/destination buffer selectors. Opaque
    seeds write depth; other steps disable writes regardless of the source
    write bit. Depth comparison is retained except for opaque completion's
    equal-depth test. Compile flags use PVR_COMPILE_SUPERSAMPLE.

    No submission/allocation occurs. Output/source overlap is rejected. Sources
    are unchanged, and failure leaves output unchanged. Returns 0, or -1 with
    EINVAL for unsupported/invalid state.
*/
int pvr_material_compile_trilinear(pvr_material_recipe_t *recipe,
                                   const pvr_poly_cxt_t *surface,
                                   uint32_t compile_flags);

/** \brief Compile bump lighting followed by surface-color modulation.

    Surface may be textured or colored, but not bump-formatted or trilinear.
    Bump must select a nonmipmapped bump texture and the same list as surface.
    Only its texture description is retained; geometry/depth state comes from
    surface. Both contexts must fit the profile described above.

    Bump vertices require black base RGB and packed light coefficients in
    oargb. Decal shading with forced seed alpha one produces (h,h,h,1), so
    multiplying the surface preserves its alpha rather than squaring h.
    Opaque output uses two steps; translucent output adds a secondary resolve.
    Ownership, failure and ordering contracts match the trilinear compiler.
*/
int pvr_material_compile_bump(pvr_material_recipe_t *recipe,
                              const pvr_poly_cxt_t *surface,
                              const pvr_poly_cxt_t *bump,
                              uint32_t compile_flags);

/** \brief Submit one material header to an explicit buffered PVR list.

    The encoded list must equal \p list. Scene and buffer ownership remain
    with the caller.

    \retval 0  Header accepted by the established buffered-list path.
    \retval -1 Invalid material, list mismatch, or unavailable buffer state,
               with errno set appropriately.
*/
int pvr_material_submit_list(const pvr_material_t *material, pvr_list_t list);

/** @} */

__END_DECLS

#endif /* __DC_PVR_MATERIAL_H */
