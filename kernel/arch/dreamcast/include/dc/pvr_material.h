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
