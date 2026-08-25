/* KallistiOS ##version##

   Host-side compact-model resource-binding contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_binding.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static pvr_poly_cxt_t compiled_context;
static uint32_t compiled_flags;
static pvr_material_kind_t compiled_kind;
static size_t compile_calls;
static size_t current_submits;
static size_t buffered_submits;

int pvr_txr_surface_get_level(const pvr_txr_surface_t *surface,
                              uint32_t level, pvr_txr_level_info_t *info) {
    if(!surface || !info || level || !surface->width || !surface->height ||
       !surface->byte_size || !surface->mip_levels) {
        errno = EINVAL;
        return -1;
    }
    info->width = surface->width;
    info->height = surface->height;
    info->offset = surface->codebook_size;
    info->byte_size = surface->data_size;
    return 0;
}

uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *surface) {
    static const uint32_t formats[] = {
        0u, PVR_TXRFMT_RGB565, 2u << 27, 3u << 27, 4u << 27,
        PVR_TXRFMT_PAL4BPP, PVR_TXRFMT_PAL8BPP
    };
    uint32_t format;

    if(!surface || surface->format > PVR_TXR_SURFACE_PAL8BPP) {
        errno = EINVAL;
        return UINT32_MAX;
    }
    format = formats[surface->format];
    if(surface->layout == PVR_TXR_SURFACE_LINEAR)
        format |= PVR_TXRFMT_NONTWIDDLED;
    else if(surface->layout == PVR_TXR_SURFACE_STRIDE)
        format |= PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE;
    else if(surface->layout == PVR_TXR_SURFACE_VQ)
        format |= PVR_TXRFMT_VQ_ENABLE;
    return format;
}

static int compile_material(pvr_material_t *material,
                            const pvr_poly_cxt_t *context,
                            uint32_t flags, pvr_material_kind_t kind) {
    ++compile_calls;
    compiled_context = *context;
    compiled_flags = flags;
    compiled_kind = kind;
    memset(material, 0, sizeof(*material));
    material->list = context->list_type;
    material->kind = kind;
    return 0;
}

int pvr_material_compile_polygon(pvr_material_t *material,
                                 const pvr_poly_cxt_t *context,
                                 uint32_t flags) {
    if(context->fmt.modifier) {
        errno = EINVAL;
        return -1;
    }
    return compile_material(material, context, flags, PVR_MATERIAL_POLYGON);
}

int pvr_material_compile_two_volume(pvr_material_t *material,
                                    const pvr_poly_cxt_t *context,
                                    uint32_t flags) {
    if(!context->fmt.modifier || !context->gen.modifier_mode) {
        errno = EINVAL;
        return -1;
    }
    return compile_material(material, context, flags,
                            PVR_MATERIAL_TWO_VOLUME);
}

int pvr_material_submit(const pvr_material_t *material) {
    assert(material && material->kind <= PVR_MATERIAL_TWO_VOLUME);
    ++current_submits;
    return 0;
}

int pvr_material_submit_list(const pvr_material_t *material, pvr_list_t list) {
    assert(material && material->list == list);
    ++buffered_submits;
    return 0;
}

static pvr_txr_surface_t make_surface(uintptr_t offset,
                                      pvr_txr_surface_format_t format,
                                      int mipmapped) {
    pvr_txr_surface_t surface = {
        .vram = (pvr_ptr_t)(uintptr_t)(PVR_RAM_INT_BASE + offset),
        .capacity = 32768,
        .byte_size = 8192,
        .codebook_size = 0,
        .data_size = 8192,
        .width = 64,
        .height = 64,
        .mip_levels = mipmapped ? 7 : 1,
        .format = format,
        .layout = PVR_TXR_SURFACE_TWIDDLED,
        .mipmapped = !!mipmapped,
        .owns_vram = false
    };

    return surface;
}

static pvr_poly_cxt_t make_context(int two_volume) {
    pvr_poly_cxt_t context;

    memset(&context, 0, sizeof(context));
    context.list_type = PVR_LIST_TR_POLY;
    context.gen.shading = true;
    context.gen.culling = PVR_CULLING_CCW;
    context.gen.fog_type = PVR_FOG_DISABLE;
    context.gen.fog_type2 = PVR_FOG_DISABLE;
    context.gen.modifier_mode = !!two_volume;
    context.fmt.color = PVR_CLRFMT_ARGBPACKED;
    context.fmt.modifier = !!two_volume;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    context.depth.write = true;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    context.blend.src2 = PVR_BLEND_ONE;
    context.blend.dst2 = PVR_BLEND_ZERO;
    context.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    context.txr2.mipmap_bias = PVR_MIPBIAS_NORMAL;
    context.txr.env = PVR_TXRENV_MODULATE;
    context.txr2.env = PVR_TXRENV_MODULATE;
    return context;
}

static void test_table(void) {
    pvr_txr_surface_t color = make_surface(0x1000,
                                           PVR_TXR_SURFACE_RGB565, 1);
    pvr_txr_surface_t pal4 = make_surface(0x9000,
                                          PVR_TXR_SURFACE_PAL4BPP, 0);
    pvr_chunk_texture_binding_t entries[] = {
        { 1, 0, &color }, { 5, 17, &pal4 }
    };
    pvr_chunk_texture_table_t table = { entries, 2 };
    pvr_chunk_texture_table_view_t view = { 0 };
    pvr_chunk_texture_table_view_t unchanged;
    const pvr_chunk_texture_binding_t *found;

    assert(pvr_chunk_texture_table_open(&table, &view) == 0);
    assert(view.table.bindings == entries && view.table.binding_count == 2);
    assert(pvr_chunk_texture_table_find(&view, 5, &found) == 0);
    assert(found == &entries[1]);
    errno = 0;
    assert(pvr_chunk_texture_table_find(&view, 4, &found) == -1);
    assert(errno == ENOENT && !found);

    unchanged = view;
    entries[1].identifier = 1;
    errno = 0;
    assert(pvr_chunk_texture_table_open(&table, &view) == -1);
    assert(errno == EEXIST && !memcmp(&view, &unchanged, sizeof(view)));
    entries[1].identifier = 5;
    entries[1].palette = 64;
    errno = 0;
    assert(pvr_chunk_texture_table_open(&table, &view) == -1);
    assert(errno == ERANGE && !memcmp(&view, &unchanged, sizeof(view)));
    entries[1].palette = 17;
    color.vram = (pvr_ptr_t)(uintptr_t)UINT32_C(0x1000);
    errno = 0;
    assert(pvr_chunk_texture_table_open(&table, &view) == -1);
    assert(errno == EFAULT && !memcmp(&view, &unchanged, sizeof(view)));
}

static void test_resolve(void) {
    pvr_txr_surface_t color = make_surface(0x1000,
                                           PVR_TXR_SURFACE_RGB565, 1);
    pvr_txr_surface_t pal4 = make_surface(0x9000,
                                          PVR_TXR_SURFACE_PAL4BPP, 0);
    pvr_chunk_texture_binding_t entries[] = {
        { 1, 0, &color }, { 5, 17, &pal4 }
    };
    pvr_chunk_texture_table_t table = { entries, 2 };
    pvr_chunk_texture_table_view_t view;
    pvr_poly_cxt_t context = make_context(0);
    pvr_chunk_render_state_t state;
    pvr_chunk_strip_view_t strip;
    pvr_material_t material;
    pvr_material_t unchanged;

    assert(pvr_chunk_texture_table_open(&table, &view) == 0);
    memset(&state, 0, sizeof(state));
    memset(&strip, 0, sizeof(strip));
    state.present = PVR_CHUNK_RENDER_BLEND | PVR_CHUNK_RENDER_TEXTURE |
                    PVR_CHUNK_RENDER_SPECULAR |
                    PVR_CHUNK_RENDER_MIPMAP_ADJUST;
    state.blend_source = PVR_BLEND_SRCALPHA;
    state.blend_destination = PVR_BLEND_INVSRCALPHA;
    state.mipmap_adjust = PVR_MIPBIAS_1_50;
    state.texture.identifier = 1;
    state.texture.filter = PVR_FILTER_BILINEAR;
    state.texture.supersample = 1;
    state.texture.uv_flip = PVR_UVFLIP_U;
    state.texture.uv_clamp = PVR_UVCLAMP_V;
    state.texture.mipmap_adjust = PVR_MIPBIAS_1_25;
    state.strip_flags = PVR_CHUNK_STRIP_USE_ALPHA |
                        PVR_CHUNK_STRIP_DOUBLE_SIDED |
                        PVR_CHUNK_STRIP_FLAT_SHADED;
    strip.type = PVR_CHUNK_STRIP_UV8;
    strip.flags = state.strip_flags;
    compile_calls = 0;
    assert(pvr_chunk_material_resolve(&material, &context, &view,
                                      &state, &strip) == 0);
    assert(compile_calls == 1 && compiled_kind == PVR_MATERIAL_POLYGON);
    assert(compiled_flags == PVR_COMPILE_SUPERSAMPLE);
    assert(compiled_context.txr.enable &&
           compiled_context.txr.base == color.vram);
    assert(compiled_context.txr.format == (int)PVR_TXRFMT_RGB565);
    assert(compiled_context.txr.mipmap &&
           compiled_context.txr.mipmap_bias == PVR_MIPBIAS_1_50);
    assert(compiled_context.txr.filter == PVR_FILTER_BILINEAR);
    assert(compiled_context.txr.uv_flip == PVR_UVFLIP_U &&
           compiled_context.txr.uv_clamp == PVR_UVCLAMP_V);
    assert(compiled_context.gen.alpha && !compiled_context.gen.shading &&
           compiled_context.gen.culling == PVR_CULLING_NONE &&
           compiled_context.gen.specular);
    assert(compiled_context.blend.src == PVR_BLEND_SRCALPHA &&
           compiled_context.blend.dst == PVR_BLEND_INVSRCALPHA);

    state.present = PVR_CHUNK_RENDER_TEXTURE;
    state.texture.identifier = 5;
    state.texture.filter = PVR_FILTER_NEAREST;
    state.texture.supersample = 0;
    state.texture.uv_flip = PVR_UVFLIP_NONE;
    state.texture.uv_clamp = PVR_UVCLAMP_NONE;
    state.strip_flags = 0;
    strip.flags = 0;
    assert(pvr_chunk_material_resolve(&material, &context, &view,
                                      &state, &strip) == 0);
    assert(compiled_context.txr.format ==
           (int)(PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(17)));

    memset(&material, 0x5a, sizeof(material));
    unchanged = material;
    state.texture.identifier = 4;
    errno = 0;
    assert(pvr_chunk_material_resolve(&material, &context, &view,
                                      &state, &strip) == -1);
    assert(errno == ENOENT && !memcmp(&material, &unchanged,
                                      sizeof(material)));
}

static void test_two_volume_and_submission(void) {
    pvr_txr_surface_t first = make_surface(0x1000,
                                           PVR_TXR_SURFACE_RGB565, 0);
    pvr_txr_surface_t second = make_surface(0x9000,
                                            PVR_TXR_SURFACE_ARGB4444, 0);
    pvr_chunk_texture_binding_t entries[] = {
        { 1, 0, &first }, { 2, 0, &second }
    };
    pvr_chunk_texture_table_t table = { entries, 2 };
    pvr_chunk_texture_table_view_t view;
    pvr_poly_cxt_t context = make_context(1);
    pvr_chunk_material_binding_t binding;
    pvr_chunk_render_state_t state;
    pvr_chunk_strip_view_t strip;
    pvr_material_t material;

    assert(pvr_chunk_texture_table_open(&table, &view) == 0);
    memset(&state, 0, sizeof(state));
    memset(&strip, 0, sizeof(strip));
    state.present = PVR_CHUNK_RENDER_TEXTURE | PVR_CHUNK_RENDER_BLEND;
    state.secondary_present = PVR_CHUNK_RENDER_TEXTURE;
    state.blend_source = PVR_BLEND_SRCALPHA;
    state.blend_destination = PVR_BLEND_INVSRCALPHA;
    state.texture.identifier = 1;
    state.texture.filter = PVR_FILTER_BILINEAR;
    state.texture.supersample = 1;
    state.secondary_texture.identifier = 2;
    state.secondary_texture.filter = PVR_FILTER_NEAREST;
    state.secondary_texture.supersample = 1;
    strip.type = PVR_CHUNK_STRIP_UV8_TWO_VOLUME;
    assert(pvr_chunk_material_resolve(&material, &context, &view,
                                      &state, &strip) == 0);
    assert(compiled_kind == PVR_MATERIAL_TWO_VOLUME);
    assert(compiled_flags == PVR_COMPILE_ALL_FLAGS);
    assert(compiled_context.txr.base == first.vram &&
           compiled_context.txr2.base == second.vram);
    assert(compiled_context.blend.src2 == PVR_BLEND_SRCALPHA &&
           compiled_context.blend.dst2 == PVR_BLEND_INVSRCALPHA);

    current_submits = buffered_submits = 0;
    assert(pvr_chunk_material_binding_init(
        &binding, &context, &view, PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    assert(pvr_chunk_material_binding_begin_strip(
        &state, &strip, &binding) == 0);
    assert(current_submits == 1 && buffered_submits == 0);
    assert(pvr_chunk_material_binding_init(
        &binding, &context, &view, PVR_GEOMETRY_SINK_BUFFERED_LIST) == 0);
    assert(pvr_chunk_material_binding_begin_strip(
        &state, &strip, &binding) == 0);
    assert(current_submits == 1 && buffered_submits == 1);

    errno = 0;
    assert(pvr_chunk_material_binding_init(
        &binding, &context, &view, PVR_GEOMETRY_SINK_MEMORY) == -1);
    assert(errno == EINVAL);
}

int main(void) {
    test_table();
    test_resolve();
    test_two_volume_and_submission();
    puts("PVR compact resource-binding tests passed");
    return 0;
}
