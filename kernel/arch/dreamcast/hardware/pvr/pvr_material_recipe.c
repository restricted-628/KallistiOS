/* KallistiOS ##version##

   Ordered material-header recipes over the existing PVR context compiler.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_material.h>
#include <errno.h>
#include <string.h>

static int invalid(void) {
    errno = EINVAL;
    return -1;
}

static int profile_valid(const pvr_poly_cxt_t *context, uint32_t flags) {
    pvr_material_t checked;

    if(!context ||
       (context->list_type != PVR_LIST_OP_POLY &&
        context->list_type != PVR_LIST_TR_POLY) ||
       context->fmt.color != PVR_CLRFMT_ARGBPACKED || context->fmt.uv ||
       context->gen.fog_type != PVR_FOG_DISABLE || context->gen.color_clamp ||
       context->blend.src_enable || context->blend.dst_enable)
        return invalid();
    return pvr_material_compile_polygon(&checked, context, flags);
}

static unsigned pixel_format(const pvr_poly_cxt_t *context) {
    return ((uint32_t)context->txr.format >> 27) & 7u;
}

static bool aliases(const pvr_material_recipe_t *recipe,
                     const pvr_poly_cxt_t *context) {
    uintptr_t a = (uintptr_t)recipe, b = (uintptr_t)context;
    return a <= b ? b - a < sizeof(*recipe) : a - b < sizeof(*context);
}

static int append(pvr_material_recipe_t *recipe,
                   const pvr_poly_cxt_t *context,
                   pvr_material_pass_role_t role, uint32_t flags) {
    pvr_material_recipe_pass_t *pass = &recipe->passes[recipe->pass_count];

    if(pvr_material_compile_polygon(&pass->material, context, flags) < 0)
        return -1;
    pass->role = role;
    ++recipe->pass_count;
    return 0;
}

static void seed_state(pvr_poly_cxt_t *context, bool secondary) {
    /* These are buffer selectors, not blend-factor enables. Overwrite first:
       secondary storage must never be assumed cleared on entry. */
    context->blend.src_enable = false;
    context->blend.dst_enable = secondary;
    context->blend.src = PVR_BLEND_ONE;
    context->blend.dst = PVR_BLEND_ZERO;
    context->depth.write = secondary ? PVR_DEPTHWRITE_DISABLE :
                                      PVR_DEPTHWRITE_ENABLE;
}

static void completion_state(pvr_poly_cxt_t *context, bool secondary) {
    context->list_type = PVR_LIST_TR_POLY;
    context->blend.src_enable = false;
    context->blend.dst_enable = secondary;
    context->depth.write = PVR_DEPTHWRITE_DISABLE;
    /* The opaque seed established visibility. A closer opaque object must
       block this completion rather than receive its addition/modulation. */
    if(!secondary)
        context->depth.comparison = PVR_DEPTHCMP_EQUAL;
}

static int resolve(pvr_material_recipe_t *recipe,
                    const pvr_poly_cxt_t *surface) {
    pvr_poly_cxt_t context = *surface;

    /* Read completed secondary RGBA without shading/fogging it again. The
       original blend factors combine it with the primary scene just once. */
    context.txr.enable = false;
    context.gen.specular = false;
    context.gen.alpha = true;
    context.blend.src_enable = true;
    context.blend.dst_enable = false;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    return append(recipe, &context, PVR_MATERIAL_PASS_RESOLVE, 0);
}

int pvr_material_compile_trilinear(pvr_material_recipe_t *recipe,
                                   const pvr_poly_cxt_t *surface,
                                   uint32_t compile_flags) {
    pvr_material_recipe_t candidate = { 0 };
    pvr_poly_cxt_t context;
    bool secondary;

    if(!recipe || profile_valid(surface, compile_flags) < 0)
        return invalid();
    if(aliases(recipe, surface) || !surface->txr.enable || !surface->txr.mipmap ||
       pixel_format(surface) == 4)
        return invalid();
    secondary = surface->list_type == PVR_LIST_TR_POLY;
    candidate.requires_presort = true;
    context = *surface;
    context.txr.filter = PVR_FILTER_TRILINEAR1;
    seed_state(&context, secondary);
    if(append(&candidate, &context, PVR_MATERIAL_PASS_SURFACE, compile_flags) < 0)
        return -1;

    context.txr.filter = PVR_FILTER_TRILINEAR2;
    completion_state(&context, secondary);
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ONE;
    if(append(&candidate, &context, PVR_MATERIAL_PASS_SURFACE, compile_flags) < 0 ||
       (secondary && resolve(&candidate, surface) < 0))
        return -1;
    memcpy(recipe, &candidate, sizeof(candidate));
    return 0;
}

int pvr_material_compile_bump(pvr_material_recipe_t *recipe,
                              const pvr_poly_cxt_t *surface,
                              const pvr_poly_cxt_t *bump,
                              uint32_t compile_flags) {
    pvr_material_recipe_t candidate = { 0 };
    pvr_poly_cxt_t context;
    bool secondary;

    if(!recipe || profile_valid(surface, compile_flags) < 0 ||
       profile_valid(bump, compile_flags) < 0)
        return invalid();
    if(aliases(recipe, surface) || aliases(recipe, bump) ||
       !bump->txr.enable || pixel_format(bump) != 4 || bump->txr.mipmap ||
       bump->txr.filter > PVR_FILTER_BILINEAR ||
       bump->list_type != surface->list_type ||
       (surface->txr.enable && (pixel_format(surface) == 4 ||
        surface->txr.filter > PVR_FILTER_BILINEAR)))
        return invalid();
    secondary = surface->list_type == PVR_LIST_TR_POLY;
    candidate.requires_presort = true;
    context = *surface;
    context.txr = bump->txr;
    context.txr.env = PVR_TXRENV_DECAL;
    context.txr.alpha = false;
    context.gen.alpha = false;
    context.gen.specular = true;
    seed_state(&context, secondary);
    if(append(&candidate, &context, PVR_MATERIAL_PASS_BUMP, compile_flags) < 0)
        return -1;

    context = *surface;
    completion_state(&context, secondary);
    /* Decal over black gave (h,h,h,1). Component-wise multiplication shades
       RGB while preserving the surface's authored alpha. */
    context.blend.src = PVR_BLEND_DESTCOLOR;
    context.blend.dst = PVR_BLEND_ZERO;
    if(append(&candidate, &context, PVR_MATERIAL_PASS_SURFACE, compile_flags) < 0 ||
       (secondary && resolve(&candidate, surface) < 0))
        return -1;
    memcpy(recipe, &candidate, sizeof(candidate));
    return 0;
}
