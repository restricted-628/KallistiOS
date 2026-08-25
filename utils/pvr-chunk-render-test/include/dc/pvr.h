#ifndef TEST_DC_PVR_H
#define TEST_DC_PVR_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define PVR_CMD_VERTEX     UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)

typedef enum pvr_list {
    PVR_LIST_OP_POLY = 0,
    PVR_LIST_OP_MOD,
    PVR_LIST_TR_POLY,
    PVR_LIST_TR_MOD,
    PVR_LIST_PT_POLY
} pvr_list_t;

typedef enum pvr_blend_mode {
    PVR_BLEND_ZERO,
    PVR_BLEND_ONE,
    PVR_BLEND_DESTCOLOR,
    PVR_BLEND_INVDESTCOLOR,
    PVR_BLEND_SRCALPHA,
    PVR_BLEND_INVSRCALPHA,
    PVR_BLEND_DESTALPHA,
    PVR_BLEND_INVDESTALPHA
} pvr_blend_mode_t;

typedef struct pvr_vertex {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    union {
        struct {
            float u;
            float v;
        };
        struct {
            uint32_t argb0;
            uint32_t argb1;
        };
    };
    uint32_t argb;
    uint32_t oargb;
} pvr_vertex_t;

typedef struct pvr_vertex_pcm {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    uint32_t argb0;
    uint32_t argb1;
    uint32_t d1;
    uint32_t d2;
} pvr_vertex_pcm_t;

typedef struct pvr_vertex_tpcm {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    float u0;
    float v0;
    uint32_t argb0;
    uint32_t oargb0;
    float u1;
    float v1;
    uint32_t argb1;
    uint32_t oargb1;
    uint32_t d1;
    uint32_t d2;
    uint32_t d3;
    uint32_t d4;
} pvr_vertex_tpcm_t;

typedef struct pvr_modifier_vol {
    alignas(32) uint32_t flags;
    float ax, ay, az;
    float bx, by, bz;
    float cx, cy, cz;
    uint32_t d1, d2, d3, d4, d5, d6;
} pvr_modifier_vol_t;

typedef struct pvr_sprite_txr {
    alignas(32) uint32_t flags;
    float ax, ay, az;
    float bx, by, bz;
    float cx, cy, cz;
    float dx, dy;
    uint32_t dummy;
    uint32_t auv, buv, cuv;
} pvr_sprite_txr_t;

#define PVR_MODIFIER_OTHER_POLY         0u
#define PVR_MODIFIER_INCLUDE_LAST_POLY  1u
#define PVR_MODIFIER_EXCLUDE_LAST_POLY  2u

typedef enum pvr_cull_mode {
    PVR_CULLING_NONE,
    PVR_CULLING_SMALL,
    PVR_CULLING_CCW,
    PVR_CULLING_CW
} pvr_cull_mode_t;

typedef struct pvr_poly_hdr {
    alignas(32) uint32_t cmd;
    uint32_t mode1;
    uint32_t mode2;
    uint32_t mode3;
    uint32_t d1, d2, d3, d4;
} pvr_poly_hdr_t;
typedef pvr_poly_hdr_t pvr_mod_hdr_t;

void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode,
                     uint32_t cull);

int pvr_prim(const void *data, size_t size);
int pvr_list_prim(pvr_list_t list, const void *data, size_t size);

#endif
