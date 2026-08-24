/* KallistiOS ##version##

   Minimal host-test PVR declarations.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_DC_PVR_H
#define TEST_DC_PVR_H

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *pvr_ptr_t;

typedef enum pvr_list_type {
    PVR_LIST_OP_POLY,
    PVR_LIST_OP_MOD,
    PVR_LIST_TR_POLY,
    PVR_LIST_TR_MOD,
    PVR_LIST_PT_POLY,
    PVR_LIST_NONE = -1
} pvr_list_t;

typedef enum pvr_color_fmts {
    PVR_CLRFMT_ARGBPACKED,
    PVR_CLRFMT_4FLOATS,
    PVR_CLRFMT_INTENSITY,
    PVR_CLRFMT_INTENSITY_PREV
} pvr_color_fmts_t;

typedef enum pvr_clip_mode {
    PVR_USERCLIP_DISABLE = 0,
    PVR_USERCLIP_INSIDE = 2,
    PVR_USERCLIP_OUTSIDE = 3
} pvr_clip_mode_t;

typedef enum pvr_cull_mode {
    PVR_CULLING_NONE,
    PVR_CULLING_SMALL,
    PVR_CULLING_CCW,
    PVR_CULLING_CW
} pvr_cull_mode_t;

typedef enum pvr_depthcmp_mode {
    PVR_DEPTHCMP_NEVER,
    PVR_DEPTHCMP_LESS,
    PVR_DEPTHCMP_EQUAL,
    PVR_DEPTHCMP_LEQUAL,
    PVR_DEPTHCMP_GREATER,
    PVR_DEPTHCMP_NOTEQUAL,
    PVR_DEPTHCMP_GEQUAL,
    PVR_DEPTHCMP_ALWAYS
} pvr_depthcmp_mode_t;

typedef enum pvr_txr_shading_mode {
    PVR_TXRENV_REPLACE,
    PVR_TXRENV_MODULATE,
    PVR_TXRENV_DECAL,
    PVR_TXRENV_MODULATEALPHA
} pvr_txr_shading_mode_t;

typedef enum pvr_filter_mode {
    PVR_FILTER_NEAREST,
    PVR_FILTER_BILINEAR,
    PVR_FILTER_TRILINEAR1,
    PVR_FILTER_TRILINEAR2
} pvr_filter_mode_t;

typedef enum pvr_fog_type {
    PVR_FOG_TABLE,
    PVR_FOG_VERTEX,
    PVR_FOG_DISABLE,
    PVR_FOG_TABLE2
} pvr_fog_type_t;

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

typedef enum pvr_mip_bias {
    PVR_MIPBIAS_0_25 = 1,
    PVR_MIPBIAS_1_00 = 4,
    PVR_MIPBIAS_3_75 = 15,
    PVR_MIPBIAS_NORMAL = PVR_MIPBIAS_1_00
} pvr_mip_bias_t;

typedef enum pvr_uv_flip {
    PVR_UVFLIP_NONE,
    PVR_UVFLIP_V,
    PVR_UVFLIP_U,
    PVR_UVFLIP_UV
} pvr_uv_flip_t;

typedef enum pvr_uv_clamp {
    PVR_UVCLAMP_NONE,
    PVR_UVCLAMP_V,
    PVR_UVCLAMP_U,
    PVR_UVCLAMP_UV
} pvr_uv_clamp_t;

#define PVR_TXRFMT_VQ_ENABLE       (1u << 30)
#define PVR_TXRFMT_RGB565          (1u << 27)
#define PVR_TXRFMT_PAL4BPP         (5u << 27)
#define PVR_TXRFMT_PAL8BPP         (6u << 27)
#define PVR_TXRFMT_NONTWIDDLED     (1u << 26)
#define PVR_TXRFMT_X32_STRIDE      (1u << 25)
#define PVR_TXRFMT_8BPP_PAL(x)     ((uint32_t)(x) << 25)
#define PVR_TXRFMT_4BPP_PAL(x)     ((uint32_t)(x) << 21)

#define PVR_COMPILE_SUPERSAMPLE    (1u << 0)
#define PVR_COMPILE_SUPERSAMPLE_2  (1u << 1)
#define PVR_COMPILE_ALL_FLAGS      3u

typedef struct pvr_poly_hdr {
    alignas(32) uint32_t words[8];
} pvr_poly_hdr_t;
typedef pvr_poly_hdr_t pvr_sprite_hdr_t;
typedef pvr_poly_hdr_t pvr_poly_mod_hdr_t;

#define TEST_GEN_FIELDS \
    bool alpha; pvr_fog_type_t fog_type; pvr_cull_mode_t culling; \
    bool color_clamp; pvr_clip_mode_t clip_mode; bool specular

#define TEST_BLEND_FIELDS \
    pvr_blend_mode_t src; pvr_blend_mode_t dst; \
    bool src_enable; bool dst_enable

#define TEST_TEXTURE_FIELDS \
    bool enable; pvr_filter_mode_t filter; bool mipmap; \
    pvr_mip_bias_t mipmap_bias; pvr_uv_flip_t uv_flip; \
    pvr_uv_clamp_t uv_clamp; bool alpha; pvr_txr_shading_mode_t env; \
    int width; int height; int format; pvr_ptr_t base

typedef struct pvr_poly_cxt {
    pvr_list_t list_type;
    struct {
        TEST_GEN_FIELDS;
        bool shading;
        bool modifier_mode;
        bool alpha2;
        pvr_fog_type_t fog_type2;
        bool color_clamp2;
    } gen;
    struct {
        TEST_BLEND_FIELDS;
        pvr_blend_mode_t src2;
        pvr_blend_mode_t dst2;
        bool src_enable2;
        bool dst_enable2;
    } blend;
    struct {
        pvr_color_fmts_t color;
        bool uv;
        bool modifier;
    } fmt;
    struct {
        pvr_depthcmp_mode_t comparison;
        bool write;
    } depth;
    struct { TEST_TEXTURE_FIELDS; } txr;
    struct { TEST_TEXTURE_FIELDS; } txr2;
} pvr_poly_cxt_t;

typedef struct pvr_sprite_cxt {
    pvr_list_t list_type;
    struct { TEST_GEN_FIELDS; } gen;
    struct { TEST_BLEND_FIELDS; } blend;
    struct {
        pvr_depthcmp_mode_t comparison;
        bool write;
    } depth;
    struct { TEST_TEXTURE_FIELDS; } txr;
} pvr_sprite_cxt_t;

#undef TEST_GEN_FIELDS
#undef TEST_BLEND_FIELDS
#undef TEST_TEXTURE_FIELDS

void pvr_poly_compile_ex(pvr_poly_hdr_t *, const pvr_poly_cxt_t *, uint32_t);
void pvr_sprite_compile_ex(pvr_sprite_hdr_t *, const pvr_sprite_cxt_t *,
                           uint32_t);
void pvr_poly_mod_compile_ex(pvr_poly_mod_hdr_t *, const pvr_poly_cxt_t *,
                             uint32_t);
int pvr_prim(const void *, size_t);
int pvr_list_prim(pvr_list_t, const void *, size_t);

#endif
