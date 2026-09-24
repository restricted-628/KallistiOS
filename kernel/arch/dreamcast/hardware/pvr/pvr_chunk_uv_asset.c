/* KallistiOS ##version##
   Independent UV source tables and layer bindings.
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_uv_asset.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4, "PUV1 requires 32-bit floats");
_Static_assert(FLT_RADIX == 2, "PUV1 requires binary floats");
_Static_assert(FLT_MANT_DIG == 24, "PUV1 requires binary32 precision");
_Static_assert(FLT_MAX_EXP == 128, "PUV1 requires binary32 range");

static uint32_t get32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t value = UINT32_MAX;
    for(size_t i = 0; i < n; ++i) {
        value ^= p[i];
        for(unsigned b = 0; b < 8; ++b)
            value = (value >> 1) ^ (UINT32_C(0xedb88320) &
                                      (uint32_t)-(int32_t)(value & 1));
    }
    return ~value;
}
static int invalid(void) { errno = EILSEQ; return -1; }
static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y ||
           (an && bn && x < y + bn && y < x + an);
}
static float get_float(const uint8_t *p) {
    uint32_t bits = get32(p);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}
static void put_float(uint8_t *p, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    put32(p, bits);
}

int pvr_chunk_uv_section_query(size_t sources, size_t bindings, size_t uvs,
                               size_t *bytes) {
    if(!bytes || !sources || !bindings || !uvs) {
        errno = EINVAL;
        return -1;
    }
    /* Restrict the entire size, not only individual table counts. */
    uint64_t n;
    if(sources > UINT32_MAX || bindings > UINT32_MAX || uvs > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    n = 40 + (uint64_t)sources * 16 + (uint64_t)bindings * 8 + (uint64_t)uvs * 8;
    if(n > UINT32_MAX || n > SIZE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    *bytes = (size_t)n;
    return 0;
}

int pvr_chunk_uv_section_write(const pvr_chunk_uv_asset_source_t *sources,
    size_t source_count, const pvr_chunk_uv_asset_binding_t *bindings,
    size_t binding_count, void *data, size_t capacity) {
    size_t total = 0, bytes;
    if(!sources || !bindings || !data || !source_count || !binding_count ||
       source_count > UINT32_MAX || binding_count > UINT32_MAX ||
       source_count > SIZE_MAX / sizeof(*sources) ||
       binding_count > SIZE_MAX / sizeof(*bindings) ||
       overlap(sources, source_count * sizeof(*sources), NULL, 0) ||
       overlap(bindings, binding_count * sizeof(*bindings), NULL, 0)) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < source_count; ++i) {
        if(!sources[i].uv || !sources[i].uv_count ||
           sources[i].uv_count > UINT32_MAX - total)
            return invalid();
        total += sources[i].uv_count;
    }
    if(pvr_chunk_uv_section_query(source_count, binding_count, total, &bytes) < 0)
        return -1;
    if(capacity < bytes) { errno = ENOSPC; return -1; }
    if(overlap(data, bytes, sources, source_count * sizeof(*sources)) ||
       overlap(data, bytes, bindings, binding_count * sizeof(*bindings))) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < source_count; ++i) {
        if(sources[i].uv_count > SIZE_MAX / sizeof(pvr_chunk_uv_t) ||
           overlap(data, bytes, sources[i].uv,
                   sources[i].uv_count * sizeof(pvr_chunk_uv_t))) {
            errno = EINVAL;
            return -1;
        }
        for(size_t v = 0; v < sources[i].uv_count; ++v) {
            if(!isfinite(sources[i].uv[v].u) || !isfinite(sources[i].uv[v].v))
                return invalid();
        }
    }
    for(size_t i = 0; i < binding_count; ++i) {
        if(bindings[i].source >= source_count ||
           (i && bindings[i].layer <= bindings[i - 1].layer))
            return invalid();
    }
    /* No writes until the entire source graph, destination and metadata pass. */
    uint8_t *p = data;
    size_t coordinate_offset = 40 + source_count * 16 + binding_count * 8;
    size_t first = 0;
    memset(p, 0, bytes);
    put32(p, PVR_CHUNK_UV_SECTION_MAGIC);
    put32(p + 4, 1u | (40u << 16));
    put32(p + 8, (uint32_t)bytes);
    put32(p + 12, (uint32_t)source_count);
    put32(p + 16, (uint32_t)binding_count);
    put32(p + 20, 16u | (8u << 16));
    put32(p + 24, 8);
    put32(p + 28, (uint32_t)total);
    for(size_t i = 0; i < source_count; ++i) {
        uint8_t *s = p + 40 + i * 16;
        put32(s, sources[i].model);
        put32(s + 4, (uint32_t)first);
        put32(s + 8, (uint32_t)sources[i].uv_count);
        for(size_t v = 0; v < sources[i].uv_count; ++v) {
            uint8_t *out = p + coordinate_offset + (first + v) * 8;
            put_float(out, sources[i].uv[v].u);
            put_float(out + 4, sources[i].uv[v].v);
        }
        first += sources[i].uv_count;
    }
    for(size_t i = 0; i < binding_count; ++i) {
        uint8_t *b = p + 40 + source_count * 16 + i * 8;
        put32(b, bindings[i].layer);
        put32(b + 4, bindings[i].source);
    }
    put32(p + 32, crc(p + 40, bytes - 40));
    put32(p + 36, crc(p, 36));
    return 0;
}

int pvr_chunk_uv_section_open(const void *data, size_t size,
                              pvr_chunk_uv_section_view_t *view) {
    const uint8_t *p = data;
    size_t bytes, first = 0;
    if(!data || !view || overlap(data, size, view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(size < 40 || get32(p) != PVR_CHUNK_UV_SECTION_MAGIC ||
       get32(p + 4) != (1u | (40u << 16)) || get32(p + 8) != size ||
       get32(p + 20) != (16u | (8u << 16)) || get32(p + 24) != 8 ||
       get32(p + 36) != crc(p, 36))
        return invalid();
    size_t sources = get32(p + 12), bindings = get32(p + 16), uvs = get32(p + 28);
    if(pvr_chunk_uv_section_query(sources, bindings, uvs, &bytes) < 0 ||
       bytes != size || get32(p + 32) != crc(p + 40, bytes - 40))
        return invalid();
    for(size_t i = 0; i < sources; ++i) {
        const uint8_t *s = p + 40 + i * 16;
        size_t count = get32(s + 8);
        if(get32(s + 4) != first || !count || count > uvs - first || get32(s + 12))
            return invalid();
        first += count;
    }
    if(first != uvs)
        return invalid();
    uint32_t previous = 0;
    for(size_t i = 0; i < bindings; ++i) {
        const uint8_t *b = p + 40 + sources * 16 + i * 8;
        if(get32(b + 4) >= sources || (i && get32(b) <= previous))
            return invalid();
        previous = get32(b);
    }
    const uint8_t *coordinates = p + 40 + sources * 16 + bindings * 8;
    for(size_t i = 0; i < uvs; ++i) {
        if(!isfinite(get_float(coordinates + i * 8)) ||
           !isfinite(get_float(coordinates + i * 8 + 4)))
            return invalid();
    }
    *view = (pvr_chunk_uv_section_view_t){data, size, sources, bindings, uvs};
    return 0;
}

static int output_valid(const pvr_chunk_uv_section_view_t *v,
                         const void *out, size_t bytes) {
    size_t total;
    if(!v || !v->data || !out ||
       pvr_chunk_uv_section_query(v->source_count, v->binding_count,
                                  v->uv_count, &total) < 0 || total != v->size ||
       overlap(v, sizeof(*v), out, bytes) || overlap(v->data, v->size, out, bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_uv_section_source_get(const pvr_chunk_uv_section_view_t *view,
    size_t source, pvr_chunk_uv_asset_info_t *info) {
    if(output_valid(view, info, sizeof(*info)) < 0)
        return -1;
    if(source >= view->source_count) { errno = ERANGE; return -1; }
    const uint8_t *s = (const uint8_t *)view->data + 40 + source * 16;
    *info = (pvr_chunk_uv_asset_info_t){get32(s), get32(s + 8)};
    return 0;
}

int pvr_chunk_uv_section_find(const pvr_chunk_uv_section_view_t *view,
                             uint32_t layer, uint32_t *source) {
    if(output_valid(view, source, sizeof(*source)) < 0)
        return -1;
    size_t lo = 0, hi = view->binding_count;
    const uint8_t *table = (const uint8_t *)view->data + 40 + view->source_count * 16;
    while(lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if(get32(table + mid * 8) < layer) lo = mid + 1;
        else hi = mid;
    }
    if(lo == view->binding_count || get32(table + lo * 8) != layer) {
        errno = ENOENT;
        return -1;
    }
    *source = get32(table + lo * 8 + 4);
    return 0;
}

int pvr_chunk_uv_section_decode(const pvr_chunk_uv_section_view_t *view,
    size_t source, pvr_chunk_uv_t *uv, size_t capacity) {
    pvr_chunk_uv_asset_info_t info;
    if(pvr_chunk_uv_section_source_get(view, source, &info) < 0)
        return -1;
    if(capacity < info.uv_count) { errno = ENOSPC; return -1; }
    if(info.uv_count > SIZE_MAX / sizeof(*uv)) { errno = EOVERFLOW; return -1; }
    if(output_valid(view, uv, info.uv_count * sizeof(*uv)) < 0)
        return -1;
    const uint8_t *p = view->data;
    size_t first = get32(p + 40 + source * 16 + 4);
    p += 40 + view->source_count * 16 + view->binding_count * 8 + first * 8;
    for(size_t i = 0; i < info.uv_count; ++i)
        uv[i] = (pvr_chunk_uv_t){get_float(p + i * 8), get_float(p + i * 8 + 4)};
    return 0;
}

int pvr_chunk_uv_section_validate_layers(const pvr_chunk_uv_section_view_t *view,
    const pvr_chunk_layer_section_view_t *layers,
    const pvr_chunk_model_view_t *models, size_t model_count) {
    pvr_chunk_uv_section_view_t checked;
    pvr_chunk_layer_section_view_t checked_layers;
    if(!view || !layers) { errno = EINVAL; return -1; }
    if(pvr_chunk_uv_section_open(view->data, view->size, &checked) < 0 ||
       pvr_chunk_layer_section_open(layers->data, layers->size, &checked_layers) < 0 ||
       pvr_chunk_layer_section_validate_models(&checked_layers, models, model_count) < 0)
        return -1;
    const uint8_t *p = checked.data;
    for(size_t i = 0; i < checked.source_count; ++i) {
        const uint8_t *s = p + 40 + i * 16;
        size_t strips, count;
        if(get32(s) >= model_count)
            return invalid();
        if(pvr_chunk_uv_source_query(&models[get32(s)], &strips, &count) < 0)
            return -1;
        if(count != get32(s + 8))
            return invalid();
    }
    for(size_t i = 0; i < checked.binding_count; ++i) {
        const uint8_t *b = p + 40 + checked.source_count * 16 + i * 8;
        pvr_chunk_layer_entry_t layer;
        if(get32(b) >= checked_layers.entry_count)
            return invalid();
        if(pvr_chunk_layer_section_entry_get(&checked_layers, get32(b), &layer) < 0)
            return -1;
        if(layer.model != get32(p + 40 + get32(b + 4) * 16))
            return invalid();
    }
    return 0;
}
