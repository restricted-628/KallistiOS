/* KallistiOS ##version##

   pvr_chunk_binding.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_binding.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define PVR_CHUNK_RENDER_STATE_ALL \
    (PVR_CHUNK_RENDER_BLEND | PVR_CHUNK_RENDER_MIPMAP_ADJUST | \
     PVR_CHUNK_RENDER_SPECULAR_EXPONENT | PVR_CHUNK_RENDER_TEXTURE | \
     PVR_CHUNK_RENDER_DIFFUSE | PVR_CHUNK_RENDER_AMBIENT | \
     PVR_CHUNK_RENDER_SPECULAR | PVR_CHUNK_RENDER_BUMP_BASIS)

#define PVR_CHUNK_RENDER_SECONDARY_STATE_ALL \
    (PVR_CHUNK_RENDER_SPECULAR_EXPONENT | PVR_CHUNK_RENDER_TEXTURE | \
     PVR_CHUNK_RENDER_DIFFUSE | PVR_CHUNK_RENDER_AMBIENT | \
     PVR_CHUNK_RENDER_SPECULAR)

#define PVR_CHUNK_STRIP_FLAGS_ALL \
    (PVR_CHUNK_STRIP_IGNORE_LIGHT | PVR_CHUNK_STRIP_IGNORE_SPECULAR | \
     PVR_CHUNK_STRIP_IGNORE_AMBIENT | PVR_CHUNK_STRIP_USE_ALPHA | \
     PVR_CHUNK_STRIP_DOUBLE_SIDED | PVR_CHUNK_STRIP_FLAT_SHADED | \
     PVR_CHUNK_STRIP_ENVIRONMENT)

typedef struct resolved_texture {
    pvr_ptr_t base;
    int format;
    int width;
    int height;
    pvr_filter_mode_t filter;
    pvr_mip_bias_t mip_bias;
    pvr_uv_flip_t uv_flip;
    pvr_uv_clamp_t uv_clamp;
    int mipmapped;
    uint32_t compile_flag;
} resolved_texture_t;

static int checked_range(const void *pointer, size_t count,
                         size_t element_size, uintptr_t *start,
                         size_t *byte_size) {
    if(!pointer || !count || count > SIZE_MAX / element_size) {
        errno = !pointer || !count ? EINVAL : ERANGE;
        return -1;
    }

    *start = (uintptr_t)pointer;
    *byte_size = count * element_size;
    if(*byte_size > UINTPTR_MAX - *start) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static int table_view_valid(const pvr_chunk_texture_table_view_t *view) {
    uintptr_t start;
    size_t bytes;

    if(!view || (view->table.binding_count && !view->table.bindings)) {
        errno = EINVAL;
        return 0;
    }
    if(view->table.binding_count >
       SIZE_MAX / sizeof(*view->table.bindings)) {
        errno = ERANGE;
        return 0;
    }
    bytes = view->table.binding_count * sizeof(*view->table.bindings);
    start = (uintptr_t)view->table.bindings;
    if(bytes > UINTPTR_MAX - start) {
        errno = ERANGE;
        return 0;
    }
    return 1;
}

static int binding_surface_format(
    const pvr_chunk_texture_binding_t *binding, uint32_t *format) {
    const pvr_txr_surface_t *surface;
    pvr_txr_level_info_t level;
    uintptr_t address;
    uint32_t value;

    if(!binding || !format || !binding->surface) {
        errno = EINVAL;
        return -1;
    }
    surface = binding->surface;
    if(pvr_txr_surface_get_level(surface, 0, &level) < 0)
        return -1;
    /* Surface descriptors are caller-owned, so revalidate the complete VRAM
       binding whenever it is admitted or resolved. */
    if(!surface->vram) {
        errno = ENODEV;
        return -1;
    }
    if((uintptr_t)surface->vram & 7u) {
        errno = EINVAL;
        return -1;
    }
    address = (uintptr_t)surface->vram;
    if(address < PVR_RAM_INT_BASE || address >= PVR_RAM_INT_TOP) {
        errno = EFAULT;
        return -1;
    }
    if(surface->capacity < surface->byte_size ||
       surface->capacity > PVR_RAM_INT_TOP - address) {
        errno = ENOSPC;
        return -1;
    }

    value = pvr_txr_surface_pvr_format(surface);
    if(value == UINT32_MAX)
        return -1;
    switch(surface->format) {
        case PVR_TXR_SURFACE_PAL4BPP:
            if(binding->palette > 63u) {
                errno = ERANGE;
                return -1;
            }
            value |= PVR_TXRFMT_4BPP_PAL(binding->palette);
            break;
        case PVR_TXR_SURFACE_PAL8BPP:
            if(binding->palette > 3u) {
                errno = ERANGE;
                return -1;
            }
            value |= PVR_TXRFMT_8BPP_PAL(binding->palette);
            break;
        default:
            if(binding->palette) {
                errno = EINVAL;
                return -1;
            }
            break;
    }
    *format = value;
    return 0;
}

static int residency_binding_layout_valid(
        const pvr_chunk_residency_binding_t *binding) {
    uintptr_t texture_start;
    uintptr_t handle_start;
    size_t texture_bytes;
    size_t handle_bytes;

    if(!binding || !binding->residency || !binding->residency->slots
       || !binding->residency->surfaces || !binding->residency->slot_count
       || !binding->textures || !binding->handles || !binding->capacity
       || binding->count > binding->capacity
       || (binding->destination != PVR_GEOMETRY_SINK_CURRENT_LIST
           && binding->destination != PVR_GEOMETRY_SINK_BUFFERED_LIST)) {
        errno = EINVAL;
        return 0;
    }
    if(checked_range(binding->textures, binding->capacity,
                     sizeof(*binding->textures), &texture_start,
                     &texture_bytes) < 0
       || checked_range(binding->handles, binding->capacity,
                        sizeof(*binding->handles), &handle_start,
                        &handle_bytes) < 0)
        return 0;
    if(ranges_overlap(texture_start, texture_bytes,
                      handle_start, handle_bytes)) {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

static int residency_binding_structural_valid(
        const pvr_chunk_residency_binding_t *binding) {
    pvr_txr_residency_status_t status;

    return residency_binding_layout_valid(binding)
        && pvr_txr_residency_get_status(binding->residency, &status) == 0;
}

static int residency_binding_entries_valid(
        const pvr_chunk_residency_binding_t *binding) {
    uint16_t previous = 0;
    size_t i;

    if(!residency_binding_structural_valid(binding))
        return 0;
    for(i = 0; i < binding->count; ++i) {
        const pvr_chunk_texture_binding_t *texture = &binding->textures[i];
        const pvr_txr_residency_handle_t *handle = &binding->handles[i];
        uint32_t format;

        if(texture->identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX
           || (i && texture->identifier <= previous)
           || !texture->surface || !handle->generation
           || handle->slot >= binding->residency->slot_count) {
            errno = EINVAL;
            return 0;
        }
        if(binding->residency->slots[handle->slot].generation
           != handle->generation) {
            errno = ESTALE;
            return 0;
        }
        if(binding->residency->slots[handle->slot].state
           != PVR_TXR_RESIDENCY_READY
           || binding->residency->slots[handle->slot].pin_count == 0
           || binding->residency->slots[handle->slot].identifier
              != texture->identifier
           || texture->surface != &binding->residency->surfaces[handle->slot]) {
            errno = EINVAL;
            return 0;
        }
        if(binding_surface_format(texture, &format) < 0)
            return 0;
        previous = texture->identifier;
    }
    return 1;
}

static size_t residency_binding_lower_bound(
        const pvr_chunk_residency_binding_t *binding, uint16_t identifier) {
    size_t lower = 0;
    size_t upper = binding->count;

    while(lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;

        if(binding->textures[middle].identifier < identifier)
            lower = middle + 1u;
        else
            upper = middle;
    }
    return lower;
}

static int residency_binding_identifier_valid(
        const pvr_chunk_residency_binding_t *binding, uint16_t identifier) {
    const pvr_chunk_texture_binding_t *texture;
    const pvr_txr_residency_handle_t *handle;
    size_t position = residency_binding_lower_bound(binding, identifier);

    if(position == binding->count
       || binding->textures[position].identifier != identifier) {
        errno = ENOENT;
        return 0;
    }
    texture = &binding->textures[position];
    handle = &binding->handles[position];
    if(!handle->generation
       || handle->slot >= binding->residency->slot_count) {
        errno = EINVAL;
        return 0;
    }
    if(binding->residency->slots[handle->slot].generation
       != handle->generation) {
        errno = ESTALE;
        return 0;
    }
    if(binding->residency->slots[handle->slot].state
       != PVR_TXR_RESIDENCY_READY
       || !binding->residency->slots[handle->slot].pin_count
       || binding->residency->slots[handle->slot].identifier != identifier
       || texture->surface != &binding->residency->surfaces[handle->slot]) {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

static int residency_binding_acquire(
        pvr_chunk_residency_binding_t *binding, uint16_t identifier) {
    pvr_chunk_texture_binding_t texture;
    pvr_txr_residency_handle_t handle;
    pvr_txr_surface_t *surface;
    size_t position = residency_binding_lower_bound(binding, identifier);
    uint32_t format;
    uint8_t palette = 0;

    if(position < binding->count
       && binding->textures[position].identifier == identifier)
        return 0;
    if(binding->count == binding->capacity) {
        errno = ENOSPC;
        return -1;
    }
    if(binding->palette_resolver
       && binding->palette_resolver(identifier, &palette,
                                    binding->palette_data) < 0)
        return -1;
    if(pvr_txr_residency_acquire(binding->residency, identifier, &handle,
                                 &surface) < 0)
        return -1;

    texture.identifier = identifier;
    texture.palette = palette;
    texture.surface = surface;
    if(binding_surface_format(&texture, &format) < 0) {
        int saved_errno = errno;

        (void)pvr_txr_residency_unpin(binding->residency, handle);
        errno = saved_errno;
        return -1;
    }

    if(position < binding->count) {
        memmove(&binding->textures[position + 1u],
                &binding->textures[position],
                (binding->count - position) * sizeof(*binding->textures));
        memmove(&binding->handles[position + 1u],
                &binding->handles[position],
                (binding->count - position) * sizeof(*binding->handles));
    }
    binding->textures[position] = texture;
    binding->handles[position] = handle;
    ++binding->count;
    return 0;
}

int pvr_chunk_texture_table_open(
        const pvr_chunk_texture_table_t *table,
        pvr_chunk_texture_table_view_t *view) {
    pvr_chunk_texture_table_view_t candidate;
    uint16_t previous = 0;
    size_t i;

    if(!view || !table || (table->binding_count && !table->bindings)) {
        errno = EINVAL;
        return -1;
    }
    if(table->binding_count > SIZE_MAX / sizeof(*table->bindings) ||
       table->binding_count * sizeof(*table->bindings) >
       UINTPTR_MAX - (uintptr_t)table->bindings) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < table->binding_count; ++i) {
        const pvr_chunk_texture_binding_t *binding = &table->bindings[i];
        uint32_t format;

        if(binding->identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX) {
            errno = ERANGE;
            return -1;
        }
        if(i && binding->identifier <= previous) {
            errno = binding->identifier == previous ? EEXIST : EINVAL;
            return -1;
        }
        if(binding_surface_format(binding, &format) < 0)
            return -1;
        previous = binding->identifier;
    }

    candidate.table = *table;
    *view = candidate;
    return 0;
}

int pvr_chunk_texture_table_find(
        const pvr_chunk_texture_table_view_t *view, uint16_t identifier,
        const pvr_chunk_texture_binding_t **binding) {
    size_t lower = 0;
    size_t upper;

    if(!binding) {
        errno = EINVAL;
        return -1;
    }
    *binding = NULL;
    if(identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX) {
        errno = ERANGE;
        return -1;
    }
    if(!table_view_valid(view))
        return -1;

    /* Use a lower-bound search so the result is deterministic for every table
       size and never reads outside the admitted array. */
    upper = view->table.binding_count;
    while(lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;
        uint16_t candidate = view->table.bindings[middle].identifier;

        if(candidate < identifier)
            lower = middle + 1u;
        else
            upper = middle;
    }
    if(lower == view->table.binding_count ||
       view->table.bindings[lower].identifier != identifier) {
        errno = ENOENT;
        return -1;
    }
    *binding = &view->table.bindings[lower];
    return 0;
}

static int resolve_texture(
    const pvr_chunk_texture_table_view_t *textures,
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_texture_state_t *texture,
    uint32_t compile_flag, resolved_texture_t *resolved) {
    const pvr_chunk_texture_binding_t *binding;
    const pvr_txr_surface_t *surface;
    uint32_t format;
    uint8_t mip_bias;

    if(texture->filter > PVR_FILTER_TRILINEAR2 || texture->supersample > 1u ||
       texture->uv_flip > PVR_UVFLIP_UV ||
       texture->uv_clamp > PVR_UVCLAMP_UV) {
        errno = EILSEQ;
        return -1;
    }
    if(pvr_chunk_texture_table_find(textures, texture->identifier,
                                    &binding) < 0 ||
       binding_surface_format(binding, &format) < 0)
        return -1;
    surface = binding->surface;
    mip_bias = state->present & PVR_CHUNK_RENDER_MIPMAP_ADJUST ?
               state->mipmap_adjust : texture->mipmap_adjust;
    if(surface->mipmapped &&
       (mip_bias < PVR_MIPBIAS_0_25 || mip_bias > PVR_MIPBIAS_3_75)) {
        errno = EILSEQ;
        return -1;
    }

    if(pvr_txr_surface_get_texture_address(surface, &resolved->base) < 0)
        return -1;
    resolved->format = (int)format;
    resolved->width = (int)surface->width;
    resolved->height = (int)surface->height;
    resolved->filter = (pvr_filter_mode_t)texture->filter;
    resolved->mip_bias = surface->mipmapped ?
                         (pvr_mip_bias_t)mip_bias : PVR_MIPBIAS_NORMAL;
    resolved->uv_flip = (pvr_uv_flip_t)texture->uv_flip;
    resolved->uv_clamp = (pvr_uv_clamp_t)texture->uv_clamp;
    resolved->mipmapped = surface->mipmapped;
    resolved->compile_flag = texture->supersample ? compile_flag : 0;
    return 0;
}

static void apply_primary_texture(pvr_poly_cxt_t *context,
                                  const resolved_texture_t *texture) {
    context->txr.enable = true;
    context->txr.filter = texture->filter;
    context->txr.mipmap = texture->mipmapped;
    context->txr.mipmap_bias = texture->mip_bias;
    context->txr.uv_flip = texture->uv_flip;
    context->txr.uv_clamp = texture->uv_clamp;
    context->txr.width = texture->width;
    context->txr.height = texture->height;
    context->txr.format = texture->format;
    context->txr.base = texture->base;
}

static void apply_secondary_texture(pvr_poly_cxt_t *context,
                                    const resolved_texture_t *texture) {
    context->txr2.enable = true;
    context->txr2.filter = texture->filter;
    context->txr2.mipmap = texture->mipmapped;
    context->txr2.mipmap_bias = texture->mip_bias;
    context->txr2.uv_flip = texture->uv_flip;
    context->txr2.uv_clamp = texture->uv_clamp;
    context->txr2.width = texture->width;
    context->txr2.height = texture->height;
    context->txr2.format = texture->format;
    context->txr2.base = texture->base;
}

static int strip_is_two_volume(uint8_t type) {
    return type == PVR_CHUNK_STRIP_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV8_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV10_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME;
}

static int render_state_valid(const pvr_chunk_render_state_t *state,
                              const pvr_chunk_strip_view_t *strip) {
    if(!state || !strip || strip->type < PVR_CHUNK_STRIP_INDEX ||
       strip->type > PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME ||
       (strip->flags & ~PVR_CHUNK_STRIP_FLAGS_ALL) ||
       /* The callback state and view must describe the same decoded strip. */
       state->strip_flags != strip->flags ||
       (state->present & ~PVR_CHUNK_RENDER_STATE_ALL) ||
       (state->secondary_present & ~PVR_CHUNK_RENDER_SECONDARY_STATE_ALL) ||
       state->blend_source < PVR_BLEND_ZERO ||
       state->blend_source > PVR_BLEND_INVDESTALPHA ||
       state->blend_destination < PVR_BLEND_ZERO ||
       state->blend_destination > PVR_BLEND_INVDESTALPHA) {
        errno = EILSEQ;
        return 0;
    }
    return 1;
}

int pvr_chunk_material_resolve(
        pvr_material_t *material, const pvr_poly_cxt_t *base_context,
        const pvr_chunk_texture_table_view_t *textures,
        const pvr_chunk_render_state_t *state,
        const pvr_chunk_strip_view_t *strip) {
    pvr_poly_cxt_t context;
    pvr_material_t candidate;
    resolved_texture_t primary;
    resolved_texture_t secondary;
    uint32_t compile_flags = 0;
    int two_volume;

    if(!material || !base_context || !table_view_valid(textures) ||
       !render_state_valid(state, strip)) {
        if(!material || !base_context)
            errno = EINVAL;
        return -1;
    }
    two_volume = strip_is_two_volume(strip->type);
    if((two_volume && !base_context->fmt.modifier) ||
       (!two_volume && base_context->fmt.modifier) ||
       (!two_volume && state->secondary_present)) {
        errno = EINVAL;
        return -1;
    }

    context = *base_context;
    context.gen.alpha = !!(strip->flags & PVR_CHUNK_STRIP_USE_ALPHA);
    context.gen.shading = !(strip->flags & PVR_CHUNK_STRIP_FLAT_SHADED) &&
                          base_context->gen.shading;
    context.gen.culling = strip->flags & PVR_CHUNK_STRIP_DOUBLE_SIDED ?
                          PVR_CULLING_NONE : base_context->gen.culling;
    context.gen.specular =
        !(strip->flags & PVR_CHUNK_STRIP_IGNORE_SPECULAR) &&
        (base_context->gen.specular ||
         (state->present & PVR_CHUNK_RENDER_SPECULAR));
    if(two_volume)
        context.gen.alpha2 = context.gen.alpha;

    if(state->present & PVR_CHUNK_RENDER_BLEND) {
        context.blend.src = state->blend_source;
        context.blend.dst = state->blend_destination;
        if(two_volume) {
            context.blend.src2 = state->blend_source;
            context.blend.dst2 = state->blend_destination;
        }
    }

    context.txr.enable = false;
    context.txr2.enable = false;
    if(state->present & PVR_CHUNK_RENDER_TEXTURE) {
        if(resolve_texture(textures, state, &state->texture,
                           PVR_COMPILE_SUPERSAMPLE, &primary) < 0)
            return -1;
        apply_primary_texture(&context, &primary);
        compile_flags |= primary.compile_flag;
    }
    if(state->secondary_present & PVR_CHUNK_RENDER_TEXTURE) {
        if(!two_volume ||
           resolve_texture(textures, state, &state->secondary_texture,
                           PVR_COMPILE_SUPERSAMPLE_2, &secondary) < 0)
            return -1;
        apply_secondary_texture(&context, &secondary);
        compile_flags |= secondary.compile_flag;
    }

    /* Compile into a local candidate so every validation or compiler failure
       leaves the caller's material byte-for-byte unchanged. */
    if(two_volume) {
        if(pvr_material_compile_two_volume(&candidate, &context,
                                           compile_flags) < 0)
            return -1;
    }
    else if(pvr_material_compile_polygon(&candidate, &context,
                                         compile_flags) < 0) {
        return -1;
    }
    *material = candidate;
    return 0;
}

int pvr_chunk_material_binding_init(
        pvr_chunk_material_binding_t *binding,
        const pvr_poly_cxt_t *base_context,
        const pvr_chunk_texture_table_view_t *textures,
        pvr_geometry_sink_kind_t destination) {
    pvr_chunk_material_binding_t candidate;
    pvr_material_t material;

    if(!binding || !base_context || !table_view_valid(textures) ||
       (destination != PVR_GEOMETRY_SINK_CURRENT_LIST &&
        destination != PVR_GEOMETRY_SINK_BUFFERED_LIST)) {
        if(!binding || !base_context ||
           (destination != PVR_GEOMETRY_SINK_CURRENT_LIST &&
            destination != PVR_GEOMETRY_SINK_BUFFERED_LIST))
            errno = EINVAL;
        return -1;
    }
    if(base_context->fmt.modifier) {
        if(pvr_material_compile_two_volume(&material, base_context, 0) < 0)
            return -1;
    }
    else if(pvr_material_compile_polygon(&material, base_context, 0) < 0) {
        return -1;
    }

    candidate.textures = *textures;
    candidate.context = *base_context;
    candidate.destination = destination;
    *binding = candidate;
    return 0;
}

int pvr_chunk_material_binding_begin_strip(
        const pvr_chunk_render_state_t *state,
        const pvr_chunk_strip_view_t *strip, void *data) {
    const pvr_chunk_material_binding_t *binding = data;
    pvr_material_t material;

    if(!binding) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_material_resolve(&material, &binding->context,
                                  &binding->textures, state, strip) < 0)
        return -1;
    /* This adapter deliberately retains no material cache: each strip's
       persistent model state is resolved at the callback boundary. */
    if(binding->destination == PVR_GEOMETRY_SINK_CURRENT_LIST)
        return pvr_material_submit(&material);
    if(binding->destination == PVR_GEOMETRY_SINK_BUFFERED_LIST)
        return pvr_material_submit_list(&material, binding->context.list_type);

    errno = EINVAL;
    return -1;
}

int pvr_chunk_residency_binding_init(
        pvr_chunk_residency_binding_t *binding,
        pvr_txr_residency_t *residency,
        pvr_chunk_texture_binding_t *textures,
        pvr_txr_residency_handle_t *handles, size_t capacity,
        pvr_chunk_residency_palette_resolver_t palette_resolver,
        void *palette_data, const pvr_poly_cxt_t *base_context,
        pvr_geometry_sink_kind_t destination) {
    pvr_chunk_residency_binding_t candidate;
    pvr_txr_residency_status_t status;
    pvr_material_t material;
    uintptr_t binding_start = (uintptr_t)binding;
    uintptr_t residency_start = (uintptr_t)residency;
    uintptr_t context_start = (uintptr_t)base_context;
    uintptr_t texture_start;
    uintptr_t handle_start;
    size_t texture_bytes;
    size_t handle_bytes;

    if(!binding || !residency || !base_context
       || (destination != PVR_GEOMETRY_SINK_CURRENT_LIST
           && destination != PVR_GEOMETRY_SINK_BUFFERED_LIST)) {
        errno = EINVAL;
        return -1;
    }
    if(checked_range(textures, capacity, sizeof(*textures), &texture_start,
                     &texture_bytes) < 0
       || checked_range(handles, capacity, sizeof(*handles), &handle_start,
                        &handle_bytes) < 0)
        return -1;
    if(ranges_overlap(binding_start, sizeof(*binding), texture_start,
                      texture_bytes)
       || ranges_overlap(binding_start, sizeof(*binding), residency_start,
                         sizeof(*residency))
       || ranges_overlap(binding_start, sizeof(*binding), context_start,
                         sizeof(*base_context))
       || ranges_overlap(residency_start, sizeof(*residency), context_start,
                         sizeof(*base_context))
       || ranges_overlap(binding_start, sizeof(*binding), handle_start,
                         handle_bytes)
       || ranges_overlap(residency_start, sizeof(*residency), texture_start,
                         texture_bytes)
       || ranges_overlap(residency_start, sizeof(*residency), handle_start,
                         handle_bytes)
       || ranges_overlap(context_start, sizeof(*base_context), texture_start,
                         texture_bytes)
       || ranges_overlap(context_start, sizeof(*base_context), handle_start,
                         handle_bytes)
       || ranges_overlap(texture_start, texture_bytes, handle_start,
                         handle_bytes)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_txr_residency_get_status(residency, &status) < 0)
        return -1;
    if(base_context->fmt.modifier) {
        if(pvr_material_compile_two_volume(&material, base_context, 0) < 0)
            return -1;
    }
    else if(pvr_material_compile_polygon(&material, base_context, 0) < 0) {
        return -1;
    }

    memset(textures, 0, texture_bytes);
    memset(handles, 0, handle_bytes);
    memset(&candidate, 0, sizeof(candidate));
    candidate.residency = residency;
    candidate.textures = textures;
    candidate.handles = handles;
    candidate.capacity = capacity;
    candidate.context = *base_context;
    candidate.destination = destination;
    candidate.palette_resolver = palette_resolver;
    candidate.palette_data = palette_data;
    *binding = candidate;
    return 0;
}

int pvr_chunk_residency_binding_prepare_model(
        pvr_chunk_residency_binding_t *binding,
        const pvr_chunk_model_view_t *view) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    if(!view || !residency_binding_entries_valid(binding)) {
        if(!view)
            errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_polygon_iterator_init(&iterator,
                                       view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        uint16_t identifier;

        if(record.record_class != PVR_CHUNK_RECORD_TEXTURE)
            continue;
        if(record.payload_word_count != 1u || !record.payload) {
            errno = EILSEQ;
            return -1;
        }
        identifier = *(const uint16_t *)record.payload
                   & PVR_CHUNK_TEXTURE_IDENTIFIER_MAX;
        if(residency_binding_acquire(binding, identifier) < 0)
            return -1;
    }
    return rv < 0 ? -1 : 0;
}

int pvr_chunk_residency_binding_begin_strip(
        const pvr_chunk_render_state_t *state,
        const pvr_chunk_strip_view_t *strip, void *data) {
    pvr_chunk_residency_binding_t *binding = data;
    pvr_chunk_texture_table_view_t view;
    pvr_material_t material;

    /* Preparation admitted the sorted pin set. Keep the per-strip path
       proportional to the referenced texture lookup rather than rescanning
       every resident entry for every strip. */
    if(!residency_binding_layout_valid(binding))
        return -1;
    if(state && (state->present & PVR_CHUNK_RENDER_TEXTURE)
       && !residency_binding_identifier_valid(binding,
                                               state->texture.identifier))
        return -1;
    if(state && (state->secondary_present & PVR_CHUNK_RENDER_TEXTURE)
       && !residency_binding_identifier_valid(
              binding, state->secondary_texture.identifier))
        return -1;
    view.table.bindings = binding->textures;
    view.table.binding_count = binding->count;
    if(pvr_chunk_material_resolve(&material, &binding->context, &view,
                                  state, strip) < 0)
        return -1;
    if(binding->destination == PVR_GEOMETRY_SINK_CURRENT_LIST)
        return pvr_material_submit(&material);
    if(binding->destination == PVR_GEOMETRY_SINK_BUFFERED_LIST)
        return pvr_material_submit_list(&material,
                                        binding->context.list_type);

    errno = EINVAL;
    return -1;
}

int pvr_chunk_residency_binding_release(
        pvr_chunk_residency_binding_t *binding) {
    size_t retained = 0;
    size_t i;
    int saved_errno = 0;

    if(!residency_binding_structural_valid(binding))
        return -1;
    for(i = 0; i < binding->count; ++i) {
        if(pvr_txr_residency_unpin(binding->residency,
                                   binding->handles[i]) < 0) {
            if(!saved_errno)
                saved_errno = errno;
            if(retained != i) {
                binding->textures[retained] = binding->textures[i];
                binding->handles[retained] = binding->handles[i];
            }
            ++retained;
        }
    }
    memset(&binding->textures[retained], 0,
           (binding->capacity - retained) * sizeof(*binding->textures));
    memset(&binding->handles[retained], 0,
           (binding->capacity - retained) * sizeof(*binding->handles));
    binding->count = retained;
    if(saved_errno) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}
