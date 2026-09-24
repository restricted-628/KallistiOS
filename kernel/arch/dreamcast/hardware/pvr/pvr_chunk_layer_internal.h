/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_CHUNK_LAYER_INTERNAL_H
#define PVR_CHUNK_LAYER_INTERNAL_H
#include <dc/pvr_chunk_layer.h>
#include <errno.h>
#include <math.h>

/* Shared by runtime preparation and the pointer-free section codec. Sampler
   fields use their hardware encodings (filter 0..1, flip/clamp 0..3, bias 1..15),
   avoiding packet/residency headers in the independent asset reader. */
static inline int material_layer_valid(const pvr_chunk_material_layer_t *layer) {
    if(!layer ||
       (layer->role != PVR_MATERIAL_PASS_LIGHTMAP &&
        layer->role != PVR_MATERIAL_PASS_EMISSIVE) ||
       (layer->rgb & UINT32_C(0xff000000)) ||
       layer->texture.identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX ||
       layer->texture.filter > 1u ||
       layer->texture.supersample > 1u ||
       layer->texture.uv_flip > 3u ||
       layer->texture.uv_clamp > 3u ||
       layer->texture.mipmap_adjust < 1u ||
       layer->texture.mipmap_adjust > 15u) {
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
