/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/pvr_chunk_layer.h
    \brief Compact auxiliary layer data, independent of texture ownership.
    \ingroup pvr_chunk_binding
*/
#ifndef __DC_PVR_CHUNK_LAYER_H
#define __DC_PVR_CHUNK_LAYER_H

#include <dc/pvr_chunk_render.h>
#include <dc/pvr_material_types.h>

/** \brief Auxiliary texture, tint and canonical-to-layer affine UV transform.
    \ingroup pvr_chunk_binding

    Only LIGHTMAP and EMISSIVE roles are admitted. Texture identifiers remain
    resource identities, not global roles. rgb is an unlit 0x00RRGGBB tint;
    role determines vertex alpha. The affine UV rows map the supplied source
    coordinates: u' = row[0]*u + row[1]*v + row[2], and likewise for v'.
    Use {{1,0,0},{0,1,0}} for identity. This is runtime metadata, not a wire
    format or an implicit extension of a model's resource manifest.
*/
typedef struct pvr_chunk_material_layer {
    pvr_material_pass_role_t role;
    pvr_chunk_texture_state_t texture;
    uint32_t rgb;
    float uv[2][3];
} pvr_chunk_material_layer_t;

#endif /* __DC_PVR_CHUNK_LAYER_H */
