/* KallistiOS ##version##

   Host-side checked PVR material tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/pvr_material.h>

static int primitive_result;
static int primitive_errno;
static size_t primitive_size;
static pvr_list_t primitive_list;

void pvr_poly_compile_ex(pvr_poly_hdr_t *header,
                         const pvr_poly_cxt_t *context, uint32_t flags) {
    memset(header, 0, sizeof(*header));
    header->words[0] = UINT32_C(0x80000000) | (uint32_t)context->list_type;
    header->words[1] = flags;
}

void pvr_sprite_compile_ex(pvr_sprite_hdr_t *header,
                           const pvr_sprite_cxt_t *context, uint32_t flags) {
    memset(header, 0, sizeof(*header));
    header->words[0] = UINT32_C(0x90000000) | (uint32_t)context->list_type;
    header->words[1] = flags;
}

void pvr_poly_mod_compile_ex(pvr_poly_mod_hdr_t *header,
                             const pvr_poly_cxt_t *context, uint32_t flags) {
    memset(header, 0, sizeof(*header));
    header->words[0] = UINT32_C(0xa0000000) | (uint32_t)context->list_type;
    header->words[1] = flags;
}

int pvr_prim(const void *header, size_t size) {
    assert(header);
    primitive_size = size;
    errno = primitive_errno;
    return primitive_result;
}

int pvr_list_prim(pvr_list_t list, const void *header, size_t size) {
    primitive_list = list;
    return pvr_prim(header, size);
}

static pvr_poly_cxt_t polygon_context(void) {
    pvr_poly_cxt_t context;

    memset(&context, 0, sizeof(context));
    context.list_type = PVR_LIST_OP_POLY;
    context.gen.fog_type = PVR_FOG_DISABLE;
    context.gen.culling = PVR_CULLING_CCW;
    context.gen.clip_mode = PVR_USERCLIP_DISABLE;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    context.fmt.color = PVR_CLRFMT_ARGBPACKED;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    return context;
}

static pvr_sprite_cxt_t sprite_context(void) {
    pvr_sprite_cxt_t context;

    memset(&context, 0, sizeof(context));
    context.list_type = PVR_LIST_TR_POLY;
    context.gen.fog_type = PVR_FOG_DISABLE;
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.clip_mode = PVR_USERCLIP_OUTSIDE;
    context.blend.src = PVR_BLEND_SRCALPHA;
    context.blend.dst = PVR_BLEND_INVSRCALPHA;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    return context;
}

static void enable_texture(pvr_poly_cxt_t *context, int format) {
    context->txr.enable = true;
    context->txr.width = 256;
    context->txr.height = 128;
    context->txr.base = (pvr_ptr_t)(uintptr_t)0x1000;
    context->txr.format = format;
    context->txr.filter = PVR_FILTER_BILINEAR;
    context->txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    context->txr.uv_flip = PVR_UVFLIP_NONE;
    context->txr.uv_clamp = PVR_UVCLAMP_NONE;
    context->txr.env = PVR_TXRENV_MODULATE;
}

static void test_polygon(void) {
    pvr_poly_cxt_t context = polygon_context();
    pvr_material_t material;
    pvr_material_t saved;

    memset(&material, 0x5a, sizeof(material));
    assert(pvr_material_compile_polygon(&material, &context, 0) == 0);
    assert(material.kind == PVR_MATERIAL_POLYGON);
    assert(material.list == PVR_LIST_OP_POLY);
    assert(material.header.words[0] == UINT32_C(0x80000000));

    enable_texture(&context, PVR_TXRFMT_RGB565);
    assert(pvr_material_compile_polygon(&material, &context,
                                        PVR_COMPILE_SUPERSAMPLE) == 0);
    assert(material.header.words[1] == PVR_COMPILE_SUPERSAMPLE);

    /* A high palette bank reuses layout bits and must remain valid. */
    context.txr.format = PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(31);
    assert(pvr_material_compile_polygon(&material, &context, 0) == 0);

    saved = material;
    context.txr.width = 63;
    assert(pvr_material_compile_polygon(&material, &context, 0) == -1);
    assert(errno == EINVAL);
    assert(memcmp(&material, &saved, sizeof(material)) == 0);

    context = polygon_context();
    context.list_type = PVR_LIST_OP_MOD;
    assert(pvr_material_compile_polygon(&material, &context, 0) == -1);

    context = polygon_context();
    context.gen.clip_mode = (pvr_clip_mode_t)1;
    assert(pvr_material_compile_polygon(&material, &context, 0) == -1);

    context = polygon_context();
    assert(pvr_material_compile_polygon(&material, &context,
                                        PVR_COMPILE_SUPERSAMPLE_2) == -1);
}

static void test_sprite_and_two_volume(void) {
    pvr_sprite_cxt_t sprite = sprite_context();
    pvr_poly_cxt_t two = polygon_context();
    pvr_material_t material;

    assert(pvr_material_compile_sprite(&material, &sprite, 0) == 0);
    assert(material.kind == PVR_MATERIAL_SPRITE);
    assert(material.list == PVR_LIST_TR_POLY);
    assert(material.header.words[0] == UINT32_C(0x90000002));

    two.fmt.modifier = true;
    two.gen.modifier_mode = true;
    two.gen.fog_type2 = PVR_FOG_DISABLE;
    two.blend.src2 = PVR_BLEND_ONE;
    two.blend.dst2 = PVR_BLEND_ZERO;
    assert(pvr_material_compile_two_volume(&material, &two,
                                           PVR_COMPILE_ALL_FLAGS) == 0);
    assert(material.kind == PVR_MATERIAL_TWO_VOLUME);
    assert(material.header.words[0] == UINT32_C(0xa0000000));

    two.gen.modifier_mode = false;
    assert(pvr_material_compile_two_volume(&material, &two, 0) == -1);
}

static void test_submission(void) {
    pvr_poly_cxt_t context = polygon_context();
    pvr_material_t material;

    assert(pvr_material_compile_polygon(&material, &context, 0) == 0);
    primitive_result = 0;
    primitive_errno = 0;
    errno = EDOM;
    assert(pvr_material_submit(&material) == 0);
    assert(errno == EDOM);
    assert(primitive_size == sizeof(pvr_poly_hdr_t));

    assert(pvr_material_submit_list(&material, PVR_LIST_OP_POLY) == 0);
    assert(primitive_list == PVR_LIST_OP_POLY);
    assert(pvr_material_submit_list(&material, PVR_LIST_TR_POLY) == -1);
    assert(errno == EINVAL);

    primitive_result = -1;
    primitive_errno = 0;
    assert(pvr_material_submit(&material) == -1);
    assert(errno == EPERM);
}

int main(void) {
    test_polygon();
    test_sprite_and_two_volume();
    test_submission();
    puts("pvr-material-test: PASS");
    return 0;
}
