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

int pvr_chunk_polygon_iterator_init(pvr_chunk_iterator_t *iterator,
                                    const uint16_t *words,
                                    size_t word_count) {
    if(!iterator || !words || !word_count) {
        errno = EINVAL;
        return -1;
    }
    memset(iterator, 0, sizeof(*iterator));
    iterator->kind = PVR_CHUNK_STREAM_POLYGON;
    iterator->words = words;
    iterator->word_count = word_count;
    return 0;
}

int pvr_chunk_iterator_next(pvr_chunk_iterator_t *iterator,
                            pvr_chunk_record_t *record) {
    const uint16_t *words;

    if(!iterator || !record || iterator->kind != PVR_CHUNK_STREAM_POLYGON) {
        errno = EINVAL;
        return -1;
    }
    if(iterator->ended || iterator->offset >= iterator->word_count)
        return 0;

    words = iterator->words;
    memset(record, 0, sizeof(*record));
    record->stream = PVR_CHUNK_STREAM_POLYGON;
    record->stream_word_offset = iterator->offset;
    record->words = &words[iterator->offset];
    record->word_count = 1;
    if(words[iterator->offset] == UINT16_C(0xffff)) {
        record->record_class = PVR_CHUNK_RECORD_END;
        record->type = PVR_CHUNK_CONTROL_END;
        iterator->ended = 1;
    }
    else {
        record->record_class = PVR_CHUNK_RECORD_TEXTURE;
        record->type = PVR_CHUNK_TEXTURE;
        record->payload = &words[iterator->offset];
        record->payload_word_count = 1;
    }
    ++iterator->offset;
    return 1;
}

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

int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *surface,
                                        pvr_ptr_t *address) {
    size_t bias = 0;

    if(address)
        *address = NULL;
    if(!surface || !address || !surface->vram) {
        errno = EINVAL;
        return -1;
    }
    if(surface->layout == PVR_TXR_SURFACE_VQ) {
        if(!surface->codebook_size || surface->codebook_size > 2048u) {
            errno = EINVAL;
            return -1;
        }
        bias = 2048u - surface->codebook_size;
    }
    *address = (uint8_t *)surface->vram - bias;
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

int pvr_txr_residency_get_status(const pvr_txr_residency_t *cache,
                                 pvr_txr_residency_status_t *status) {
    size_t i;

    if(status)
        memset(status, 0, sizeof(*status));
    if(!cache || !status || !cache->slots || !cache->surfaces
       || !cache->slot_count) {
        errno = EINVAL;
        return -1;
    }
    status->slot_count = cache->slot_count;
    for(i = 0; i < cache->slot_count; ++i) {
        if(cache->slots[i].state == PVR_TXR_RESIDENCY_READY)
            ++status->ready_slots;
        else if(cache->slots[i].state == PVR_TXR_RESIDENCY_LOADING)
            ++status->loading_slots;
        if(cache->slots[i].pin_count)
            ++status->pinned_slots;
        status->pin_count += cache->slots[i].pin_count;
    }
    return 0;
}

int pvr_txr_residency_acquire(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface) {
    size_t i;

    if(handle)
        memset(handle, 0, sizeof(*handle));
    if(surface)
        *surface = NULL;
    if(!cache || !handle || !surface) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < cache->slot_count; ++i) {
        if(cache->slots[i].state == PVR_TXR_RESIDENCY_EMPTY
           || cache->slots[i].identifier != identifier)
            continue;
        if(cache->slots[i].state == PVR_TXR_RESIDENCY_LOADING) {
            errno = EAGAIN;
            return -1;
        }
        ++cache->slots[i].pin_count;
        handle->slot = i;
        handle->generation = cache->slots[i].generation;
        *surface = &cache->surfaces[i];
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int pvr_txr_residency_unpin(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle) {
    pvr_txr_residency_slot_t *slot;

    if(!cache || handle.slot >= cache->slot_count) {
        errno = ERANGE;
        return -1;
    }
    slot = &cache->slots[handle.slot];
    if(!handle.generation || slot->generation != handle.generation) {
        errno = ESTALE;
        return -1;
    }
    if(slot->state != PVR_TXR_RESIDENCY_READY || !slot->pin_count) {
        errno = EINVAL;
        return -1;
    }
    --slot->pin_count;
    return 0;
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

    color.layout = PVR_TXR_SURFACE_VQ;
    color.codebook_size = 1024;
    color.data_size = color.byte_size - color.codebook_size;

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
           compiled_context.txr.base == (uint8_t *)color.vram - 1024u);
    assert(compiled_context.txr.format ==
           (int)(PVR_TXRFMT_RGB565 | PVR_TXRFMT_VQ_ENABLE));
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

static void init_residency(pvr_txr_residency_t *cache,
                           pvr_txr_residency_slot_t slots[3],
                           pvr_txr_surface_t surfaces[3]) {
    size_t i;

    memset(cache, 0, sizeof(*cache));
    memset(slots, 0, 3u * sizeof(*slots));
    for(i = 0; i < 3; ++i) {
        surfaces[i] = make_surface(0x1000u + i * 0x9000u,
                                   PVR_TXR_SURFACE_RGB565, 0);
        slots[i].generation = i + 1u;
        slots[i].state = PVR_TXR_RESIDENCY_READY;
    }
    slots[0].identifier = 5;
    slots[1].identifier = 1;
    slots[2].identifier = 3;
    slots[2].state = PVR_TXR_RESIDENCY_LOADING;
    slots[2].pin_count = 1;
    cache->slots = slots;
    cache->surfaces = surfaces;
    cache->slot_count = 3;
}

static int palette_one(uint16_t identifier, uint8_t *palette, void *data) {
    assert(identifier <= PVR_CHUNK_TEXTURE_IDENTIFIER_MAX);
    assert(palette && !data);
    *palette = 1;
    return 0;
}

static void test_residency_binding(void) {
    static const uint16_t model_textures[] = { 5, 1, 5, UINT16_C(0xffff) };
    static const uint16_t absent_texture[] = { 2, UINT16_C(0xffff) };
    static const uint16_t loading_texture[] = { 3, UINT16_C(0xffff) };
    pvr_txr_residency_t cache;
    pvr_txr_residency_slot_t slots[3];
    pvr_txr_surface_t surfaces[3];
    pvr_chunk_texture_binding_t textures[2];
    pvr_txr_residency_handle_t handles[2];
    pvr_chunk_residency_binding_t binding;
    pvr_chunk_model_view_t model;
    pvr_poly_cxt_t context = make_context(0);
    pvr_chunk_render_state_t state;
    pvr_chunk_strip_view_t strip;

    init_residency(&cache, slots, surfaces);
    memset(&model, 0, sizeof(model));
    model.model.polygon_words = model_textures;
    model.model.polygon_word_count = 4;
    assert(pvr_chunk_residency_binding_init(
        &binding, &cache, textures, handles, 2, NULL, NULL, &context,
        PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    assert(binding.count == 0);
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == 0);
    assert(binding.count == 2 && textures[0].identifier == 1
           && textures[1].identifier == 5);
    assert(slots[0].pin_count == 1 && slots[1].pin_count == 1);

    memset(&state, 0, sizeof(state));
    memset(&strip, 0, sizeof(strip));
    state.present = PVR_CHUNK_RENDER_TEXTURE;
    state.texture.identifier = 5;
    state.texture.filter = PVR_FILTER_NEAREST;
    strip.type = PVR_CHUNK_STRIP_UV8;
    current_submits = 0;
    assert(pvr_chunk_residency_binding_begin_strip(
        &state, &strip, &binding) == 0);
    assert(current_submits == 1);

    model.model.polygon_words = absent_texture;
    model.model.polygon_word_count = 2;
    errno = 0;
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == -1);
    assert(errno == ENOSPC && binding.count == 2);

    assert(pvr_chunk_residency_binding_release(&binding) == 0);
    assert(binding.count == 0 && slots[0].pin_count == 0
           && slots[1].pin_count == 0);
    errno = 0;
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == -1);
    assert(errno == ENOENT && binding.count == 0);

    model.model.polygon_words = loading_texture;
    errno = 0;
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == -1);
    assert(errno == EAGAIN && binding.count == 0);

    model.model.polygon_words = model_textures;
    model.model.polygon_word_count = 4;
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == 0);
    ++slots[handles[0].slot].generation;
    errno = 0;
    assert(pvr_chunk_residency_binding_release(&binding) == -1);
    assert(errno == ESTALE && binding.count == 1);
    --slots[binding.handles[0].slot].generation;
    assert(pvr_chunk_residency_binding_release(&binding) == 0);
    assert(binding.count == 0);

    errno = 0;
    assert(pvr_chunk_residency_binding_init(
        &binding, &cache, textures, (pvr_txr_residency_handle_t *)textures,
        1, NULL, NULL, &context,
        PVR_GEOMETRY_SINK_CURRENT_LIST) == -1);
    assert(errno == EINVAL);

    assert(pvr_chunk_residency_binding_init(
        &binding, &cache, textures, handles, 2, palette_one, NULL, &context,
        PVR_GEOMETRY_SINK_CURRENT_LIST) == 0);
    errno = 0;
    assert(pvr_chunk_residency_binding_prepare_model(&binding, &model) == -1);
    assert(errno == EINVAL && binding.count == 0);
    assert(slots[0].pin_count == 0 && slots[1].pin_count == 0);
}

int main(void) {
    test_table();
    test_resolve();
    test_two_volume_and_submission();
    test_residency_binding();
    puts("PVR compact resource-binding tests passed");
    return 0;
}
