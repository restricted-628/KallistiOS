/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_CHUNK_LAYER_INTERNAL_H
#define PVR_CHUNK_LAYER_INTERNAL_H
#include <dc/pvr_chunk_binding.h>
#include <errno.h>
#include <math.h>

/* Shared by runtime preparation and the pointer-free section codec. */
static inline int material_layer_valid(const pvr_chunk_material_layer_t *layer) {
    if(!layer ||
       (layer->role != PVR_MATERIAL_PASS_LIGHTMAP &&
        layer->role != PVR_MATERIAL_PASS_EMISSIVE) ||
       (layer->rgb & UINT32_C(0xff000000)) ||
       layer->texture.identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX ||
       layer->texture.filter > PVR_FILTER_BILINEAR ||
       layer->texture.supersample > 1u ||
       layer->texture.uv_flip > PVR_UVFLIP_UV ||
       layer->texture.uv_clamp > PVR_UVCLAMP_UV ||
       layer->texture.mipmap_adjust < PVR_MIPBIAS_0_25 ||
       layer->texture.mipmap_adjust > PVR_MIPBIAS_3_75) {
        errno = EINVAL;
        return -1;
    }
    for(size_t row = 0; row < 2; ++row) {
        for(size_t column = 0; column < 3; ++column) {
            if(!isfinite(layer->uv[row][column])) {
                errno = EDOM;
                return -1;
            }
        }
    }
    return 0;
}
#endif
