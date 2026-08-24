/* KallistiOS ##version##

   pvr_material.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_material.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(pvr_poly_hdr_t) == 32,
               "compiled PVR headers must occupy one TA block");

static int polygon_list(pvr_list_t list) {
    return list == PVR_LIST_OP_POLY || list == PVR_LIST_TR_POLY ||
           list == PVR_LIST_PT_POLY;
}

static int clip_valid(pvr_clip_mode_t value) {
    return value == PVR_USERCLIP_DISABLE || value == PVR_USERCLIP_INSIDE ||
           value == PVR_USERCLIP_OUTSIDE;
}

static int texture_format_valid(int format) {
    const uint32_t value = (uint32_t)format;
    const uint32_t pixel = (value >> 27) & 7u;
    const int vq = !!(value & PVR_TXRFMT_VQ_ENABLE);
    const int linear = !!(value & PVR_TXRFMT_NONTWIDDLED);
    const int x32 = !!(value & PVR_TXRFMT_X32_STRIDE);

    /* Bits below the palette selector overlap the encoded VRAM address. */
    if(value & UINT32_C(0x001fffff))
        return 0;

    if(pixel > 6u)
        return 0;

    /* VQ is established only for 16-bit texels. Palette selectors share the
       layout bits and therefore cannot describe linear palette textures. */
    if(vq && (linear || pixel > 4u))
        return 0;

    /* Palette-bank selectors intentionally reuse the two layout bits. Their
       format cannot be interpreted as linear/X32 state. */
    if(pixel >= 5u)
        return !vq;

    if(x32 && !linear)
        return 0;

    return 1;
}

static int texture_valid(int enabled, int width, int height, pvr_ptr_t base,
                         int format, pvr_filter_mode_t filter,
                         pvr_mip_bias_t mip_bias, pvr_uv_flip_t flip,
                         pvr_uv_clamp_t clamp,
                         pvr_txr_shading_mode_t environment) {
    uintptr_t address;

    if(!enabled)
        return 1;

    if(width < 8 || width > 1024 || (width & (width - 1)) ||
       height < 8 || height > 1024 || (height & (height - 1)) || !base)
        return 0;

    address = (uintptr_t)base;
    if(address & 7u)
        return 0;

    if(filter < PVR_FILTER_NEAREST || filter > PVR_FILTER_TRILINEAR2 ||
       mip_bias < PVR_MIPBIAS_0_25 || mip_bias > PVR_MIPBIAS_3_75 ||
       flip < PVR_UVFLIP_NONE || flip > PVR_UVFLIP_UV ||
       clamp < PVR_UVCLAMP_NONE || clamp > PVR_UVCLAMP_UV ||
       environment < PVR_TXRENV_REPLACE ||
       environment > PVR_TXRENV_MODULATEALPHA)
        return 0;

    return texture_format_valid(format);
}

static int common_valid(pvr_list_t list, pvr_cull_mode_t culling,
                        pvr_clip_mode_t clipping,
                        pvr_depthcmp_mode_t depth,
                        pvr_blend_mode_t source,
                        pvr_blend_mode_t destination,
                        pvr_fog_type_t fog) {
    return polygon_list(list) && culling >= PVR_CULLING_NONE &&
           culling <= PVR_CULLING_CW && clip_valid(clipping) &&
           depth >= PVR_DEPTHCMP_NEVER && depth <= PVR_DEPTHCMP_ALWAYS &&
           source >= PVR_BLEND_ZERO && source <= PVR_BLEND_INVDESTALPHA &&
           destination >= PVR_BLEND_ZERO &&
           destination <= PVR_BLEND_INVDESTALPHA &&
           fog >= PVR_FOG_TABLE && fog <= PVR_FOG_TABLE2;
}

static int polygon_context_valid(const pvr_poly_cxt_t *context,
                                 int two_volume) {
    if(!common_valid(context->list_type, context->gen.culling,
                     context->gen.clip_mode, context->depth.comparison,
                     context->blend.src, context->blend.dst,
                     context->gen.fog_type) ||
       context->fmt.color < PVR_CLRFMT_ARGBPACKED ||
       context->fmt.color > PVR_CLRFMT_INTENSITY_PREV)
        return 0;

    if(two_volume) {
        if(!context->fmt.modifier || !context->gen.modifier_mode ||
           context->blend.src2 < PVR_BLEND_ZERO ||
           context->blend.src2 > PVR_BLEND_INVDESTALPHA ||
           context->blend.dst2 < PVR_BLEND_ZERO ||
           context->blend.dst2 > PVR_BLEND_INVDESTALPHA ||
           context->gen.fog_type2 < PVR_FOG_TABLE ||
           context->gen.fog_type2 > PVR_FOG_TABLE2)
            return 0;
    }
    else if(context->fmt.modifier || context->gen.modifier_mode ||
            context->txr2.enable) {
        return 0;
    }

    if(!texture_valid(context->txr.enable, context->txr.width,
                      context->txr.height, context->txr.base,
                      context->txr.format, context->txr.filter,
                      context->txr.mipmap_bias, context->txr.uv_flip,
                      context->txr.uv_clamp, context->txr.env))
        return 0;

    return !two_volume ||
           texture_valid(context->txr2.enable, context->txr2.width,
                         context->txr2.height, context->txr2.base,
                         context->txr2.format, context->txr2.filter,
                         context->txr2.mipmap_bias, context->txr2.uv_flip,
                         context->txr2.uv_clamp, context->txr2.env);
}

static int sprite_context_valid(const pvr_sprite_cxt_t *context) {
    return common_valid(context->list_type, context->gen.culling,
                        context->gen.clip_mode, context->depth.comparison,
                        context->blend.src, context->blend.dst,
                        context->gen.fog_type) &&
           texture_valid(context->txr.enable, context->txr.width,
                         context->txr.height, context->txr.base,
                         context->txr.format, context->txr.filter,
                         context->txr.mipmap_bias, context->txr.uv_flip,
                         context->txr.uv_clamp, context->txr.env);
}

int pvr_material_compile_polygon(pvr_material_t *material,
                                 const pvr_poly_cxt_t *context,
                                 uint32_t compile_flags) {
    pvr_material_t candidate;

    if(!material || !context || (compile_flags & ~PVR_COMPILE_SUPERSAMPLE) ||
       !polygon_context_valid(context, 0)) {
        errno = EINVAL;
        return -1;
    }

    memset(&candidate, 0, sizeof(candidate));
    pvr_poly_compile_ex(&candidate.header, context, compile_flags);
    candidate.list = context->list_type;
    candidate.kind = PVR_MATERIAL_POLYGON;
    memcpy(material, &candidate, sizeof(candidate));
    return 0;
}

int pvr_material_compile_sprite(pvr_material_t *material,
                                const pvr_sprite_cxt_t *context,
                                uint32_t compile_flags) {
    pvr_material_t candidate;

    if(!material || !context || (compile_flags & ~PVR_COMPILE_SUPERSAMPLE) ||
       !sprite_context_valid(context)) {
        errno = EINVAL;
        return -1;
    }

    memset(&candidate, 0, sizeof(candidate));
    pvr_sprite_compile_ex((pvr_sprite_hdr_t *)&candidate.header, context,
                          compile_flags);
    candidate.list = context->list_type;
    candidate.kind = PVR_MATERIAL_SPRITE;
    memcpy(material, &candidate, sizeof(candidate));
    return 0;
}

int pvr_material_compile_two_volume(pvr_material_t *material,
                                    const pvr_poly_cxt_t *context,
                                    uint32_t compile_flags) {
    pvr_material_t candidate;

    if(!material || !context || (compile_flags & ~PVR_COMPILE_ALL_FLAGS) ||
       !polygon_context_valid(context, 1)) {
        errno = EINVAL;
        return -1;
    }

    memset(&candidate, 0, sizeof(candidate));
    pvr_poly_mod_compile_ex((pvr_poly_mod_hdr_t *)&candidate.header, context,
                            compile_flags);
    candidate.list = context->list_type;
    candidate.kind = PVR_MATERIAL_TWO_VOLUME;
    memcpy(material, &candidate, sizeof(candidate));
    return 0;
}

static int material_valid(const pvr_material_t *material) {
    return material && polygon_list(material->list) &&
           material->kind >= PVR_MATERIAL_POLYGON &&
           material->kind <= PVR_MATERIAL_TWO_VOLUME;
}

int pvr_material_submit(const pvr_material_t *material) {
    int saved_errno;
    int rv;

    if(!material_valid(material)) {
        errno = EINVAL;
        return -1;
    }

    saved_errno = errno;
    errno = 0;
    rv = pvr_prim(&material->header, sizeof(material->header));
    if(rv < 0) {
        if(!errno)
            errno = EPERM;
        return -1;
    }

    errno = saved_errno;
    return 0;
}

int pvr_material_submit_list(const pvr_material_t *material, pvr_list_t list) {
    int saved_errno;
    int rv;

    if(!material_valid(material) || list != material->list) {
        errno = EINVAL;
        return -1;
    }

    saved_errno = errno;
    errno = 0;
    rv = pvr_list_prim(list, &material->header, sizeof(material->header));
    if(rv < 0) {
        if(!errno)
            errno = EPERM;
        return -1;
    }

    errno = saved_errno;
    return 0;
}
