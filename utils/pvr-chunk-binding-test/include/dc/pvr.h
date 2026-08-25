/* KallistiOS ##version##

   Host-test PVR declarations for compact resource binding.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_CHUNK_BINDING_DC_PVR_H
#define TEST_CHUNK_BINDING_DC_PVR_H

#include "../../../pvr-residency-test/include/dc/pvr.h"

#define PVR_CMD_VERTEX     UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)

#define PVR_MIPBIAS_1_25 ((pvr_mip_bias_t)5)
#define PVR_MIPBIAS_1_50 ((pvr_mip_bias_t)6)

typedef struct pvr_vertex {
    alignas(32) uint32_t flags;
    float x, y, z, u, v;
    uint32_t argb, oargb;
} pvr_vertex_t;

typedef struct pvr_vertex_pcm {
    alignas(32) uint32_t flags;
    float x, y, z;
    uint32_t argb0, argb1, d1, d2;
} pvr_vertex_pcm_t;

typedef struct pvr_vertex_tpcm {
    alignas(32) uint32_t flags;
    float x, y, z, u0, v0;
    uint32_t argb0, oargb0;
    float u1, v1;
    uint32_t argb1, oargb1, d1, d2, d3, d4;
} pvr_vertex_tpcm_t;

typedef struct pvr_modifier_vol {
    alignas(32) uint32_t flags;
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    uint32_t d1, d2, d3, d4, d5, d6;
} pvr_modifier_vol_t;

typedef struct pvr_sprite_txr {
    alignas(32) uint32_t flags;
    float ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy;
    uint32_t dummy, auv, buv, cuv;
} pvr_sprite_txr_t;

uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *surface);

#endif /* TEST_CHUNK_BINDING_DC_PVR_H */
