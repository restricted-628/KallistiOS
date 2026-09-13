/* KallistiOS ##version##
   Pointer-free auxiliary layer associations.
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_layer_asset.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <string.h>
#include "pvr_chunk_layer_internal.h"

_Static_assert(sizeof(float) == 4, "PML1 requires 32-bit floats");
_Static_assert(FLT_RADIX == 2, "PML1 requires binary floats");
_Static_assert(FLT_MANT_DIG == 24, "PML1 requires binary32 precision");
_Static_assert(FLT_MAX_EXP == 128, "PML1 requires binary32 range");

static int invalid(void) {
    errno = EILSEQ;
    return -1;
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t av = (uintptr_t)a, bv = (uintptr_t)b;
    return av > UINTPTR_MAX - an || bv > UINTPTR_MAX - bn ||
           (av < bv + bn && bv < av + an);
}

static uint32_t read32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static unsigned read16(const uint8_t *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static void write32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static void write16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t crc32(const uint8_t *p, size_t bytes) {
    uint32_t crc = UINT32_MAX;
    for(size_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for(unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                               (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static int range_valid(const pvr_chunk_layer_entry_t *entry,
                        const pvr_chunk_layer_entry_t *previous) {
    if(!entry->strip_count ||
       entry->strip_count - 1u > UINT32_MAX - entry->first_strip ||
       (previous && (entry->model < previous->model ||
        (entry->model == previous->model &&
         (entry->first_strip <= previous->first_strip ||
          entry->first_strip - previous->first_strip < previous->strip_count)))))
        return invalid();
    return material_layer_valid(&entry->layer);
}

static void decode(const uint8_t *p, pvr_chunk_layer_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));
    entry->model = read32(p);
    entry->first_strip = read32(p + 4);
    entry->strip_count = read32(p + 8);
    entry->layer.role = read16(p + 12) == 1 ? PVR_MATERIAL_PASS_LIGHTMAP :
                                            PVR_MATERIAL_PASS_EMISSIVE;
    entry->layer.texture.identifier = (uint16_t)read16(p + 14);
    entry->layer.texture.filter = p[16];
    entry->layer.texture.supersample = p[17];
    entry->layer.texture.uv_flip = p[18];
    entry->layer.texture.uv_clamp = p[19];
    entry->layer.texture.mipmap_adjust = p[20];
    entry->layer.rgb = read32(p + 24);
    for(unsigned row = 0; row < 2; ++row) {
        for(unsigned col = 0; col < 3; ++col) {
            uint32_t bits = read32(p + 28 + (row * 3 + col) * 4);
            memcpy(&entry->layer.uv[row][col], &bits, 4);
        }
    }
}

int pvr_chunk_layer_section_query(size_t count, size_t *bytes) {
    if(!bytes || !count) {
        errno = EINVAL;
        return -1;
    }
    if(count > (UINT32_MAX - 32u) / 64u ||
       count > (SIZE_MAX - 32u) / 64u) {
        errno = EOVERFLOW;
        return -1;
    }
    *bytes = 32 + count * 64;
    return 0;
}

int pvr_chunk_layer_section_write(const pvr_chunk_layer_entry_t *entries,
                                  size_t count, void *data, size_t capacity) {
    size_t bytes, input_bytes;
    uintptr_t src = (uintptr_t)entries, dst = (uintptr_t)data;
    uint8_t *p = data;

    if(!entries || !data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_layer_section_query(count, &bytes) < 0)
        return -1;
    if(capacity < bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(count > SIZE_MAX / sizeof(*entries)) {
        errno = EOVERFLOW;
        return -1;
    }
    input_bytes = count * sizeof(*entries);
    if(src > UINTPTR_MAX - input_bytes || dst > UINTPTR_MAX - bytes ||
       (src < dst + bytes && dst < src + input_bytes)) {
        errno = EINVAL;
        return -1;
    }
    /* Complete admission before writing even the header. */
    for(size_t i = 0; i < count; ++i) {
        if(range_valid(&entries[i], i ? &entries[i - 1] : NULL) < 0)
            return -1;
    }
    memset(p, 0, bytes);
    write32(p, PVR_CHUNK_LAYER_SECTION_MAGIC);
    write16(p + 4, 1);
    write16(p + 6, 32);
    write32(p + 8, (uint32_t)bytes);
    write32(p + 12, (uint32_t)count);
    write16(p + 16, 64);
    for(size_t i = 0; i < count; ++i) {
        uint8_t *r = p + 32 + i * 64;
        const pvr_chunk_layer_entry_t *e = &entries[i];
        write32(r, e->model);
        write32(r + 4, e->first_strip);
        write32(r + 8, e->strip_count);
        write16(r + 12, e->layer.role == PVR_MATERIAL_PASS_LIGHTMAP ? 1 : 2);
        write16(r + 14, e->layer.texture.identifier);
        r[16] = e->layer.texture.filter;
        r[17] = e->layer.texture.supersample;
        r[18] = e->layer.texture.uv_flip;
        r[19] = e->layer.texture.uv_clamp;
        r[20] = e->layer.texture.mipmap_adjust;
        write32(r + 24, e->layer.rgb);
        for(unsigned row = 0; row < 2; ++row) {
            for(unsigned col = 0; col < 3; ++col) {
                uint32_t bits;
                memcpy(&bits, &e->layer.uv[row][col], 4);
                write32(r + 28 + (row * 3 + col) * 4, bits);
            }
        }
    }
    write32(p + 20, crc32(p + 32, bytes - 32));
    write32(p + 28, crc32(p, 28));
    return 0;
}

int pvr_chunk_layer_section_open(const void *data, size_t size,
                                 pvr_chunk_layer_section_view_t *view) {
    const uint8_t *p = data;
    pvr_chunk_layer_entry_t previous;
    size_t bytes, count;
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(overlaps(data, size, view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(size < 32 || (uintptr_t)data > UINTPTR_MAX - size ||
       read32(p) != PVR_CHUNK_LAYER_SECTION_MAGIC ||
       read16(p + 4) != 1 || read16(p + 6) != 32 ||
       read32(p + 8) != size || read16(p + 16) != 64 ||
       read16(p + 18) || read32(p + 24) ||
       read32(p + 28) != crc32(p, 28))
        return invalid();
    count = read32(p + 12);
    if(pvr_chunk_layer_section_query(count, &bytes) < 0 || bytes != size ||
       read32(p + 20) != crc32(p + 32, size - 32))
        return invalid();
    for(size_t i = 0; i < count; ++i) {
        const uint8_t *r = p + 32 + i * 64;
        pvr_chunk_layer_entry_t entry;
        if((read16(r + 12) != 1 && read16(r + 12) != 2) ||
           r[21] || read16(r + 22) || read32(r + 52) ||
           read32(r + 56) || read32(r + 60))
            return invalid();
        decode(r, &entry);
        if(range_valid(&entry, i ? &previous : NULL) < 0)
            return invalid();
        previous = entry;
    }
    *view = (pvr_chunk_layer_section_view_t){ data, size, count };
    return 0;
}

/* Views are admitted once; keep lookup O(log N), not a CRC rescan per strip. */
static int view_valid(const pvr_chunk_layer_section_view_t *view) {
    size_t bytes;
    if(!view || !view->data ||
       pvr_chunk_layer_section_query(view->entry_count, &bytes) < 0 ||
       bytes != view->size || (uintptr_t)view->data > UINTPTR_MAX - bytes) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int output_valid(const pvr_chunk_layer_section_view_t *view,
                         const pvr_chunk_layer_entry_t *entry) {
    if(!entry || overlaps(view->data, view->size, entry, sizeof(*entry)) ||
       overlaps(view, sizeof(*view), entry, sizeof(*entry))) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_layer_section_entry_get(
        const pvr_chunk_layer_section_view_t *view, size_t index,
        pvr_chunk_layer_entry_t *entry) {
    if(!entry) {
        errno = EINVAL;
        return -1;
    }
    if(view_valid(view) < 0 || output_valid(view, entry) < 0)
        return -1;
    if(index >= view->entry_count) {
        errno = ERANGE;
        return -1;
    }
    decode((const uint8_t *)view->data + 32 + index * 64, entry);
    return 0;
}

int pvr_chunk_layer_section_find(const pvr_chunk_layer_section_view_t *view,
                                 uint32_t model, uint32_t strip,
                                 pvr_chunk_layer_entry_t *entry) {
    size_t low = 0, high;
    pvr_chunk_layer_entry_t candidate;
    if(!entry) {
        errno = EINVAL;
        return -1;
    }
    if(view_valid(view) < 0 || output_valid(view, entry) < 0)
        return -1;
    high = view->entry_count;
    while(low < high) {
        size_t mid = low + (high - low) / 2;
        const uint8_t *r = (const uint8_t *)view->data + 32 + mid * 64;
        uint32_t m = read32(r), first = read32(r + 4);
        if(m < model || (m == model && first <= strip))
            low = mid + 1;
        else
            high = mid;
    }
    if(low) {
        decode((const uint8_t *)view->data + 32 + (low - 1) * 64, &candidate);
        if(candidate.model == model && strip >= candidate.first_strip &&
           strip - candidate.first_strip < candidate.strip_count) {
            *entry = candidate;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int pvr_chunk_layer_section_validate_models(
        const pvr_chunk_layer_section_view_t *view,
        const pvr_chunk_model_view_t *models, size_t model_count) {
    pvr_chunk_layer_section_view_t checked;
    pvr_chunk_model_view_t model;
    uint32_t previous = 0;
    if(!view || !models || !model_count ||
       model_count > SIZE_MAX / sizeof(*models) ||
       (uintptr_t)models > UINTPTR_MAX - model_count * sizeof(*models)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_layer_section_open(view->data, view->size, &checked) < 0)
        return -1;
    for(size_t i = 0; i < checked.entry_count; ++i) {
        pvr_chunk_layer_entry_t e;
        decode((const uint8_t *)checked.data + 32 + i * 64, &e);
        if(e.model >= model_count)
            return invalid();
        if(!i || e.model != previous) {
            if(pvr_chunk_model_open(&models[e.model].model, &model) < 0)
                return -1;
            if(model.info.requirements) {
                errno = ENOTSUP;
                return -1;
            }
        }
        if(e.first_strip >= model.info.strips ||
           e.strip_count > model.info.strips - e.first_strip)
            return invalid();
        previous = e.model;
    }
    return 0;
}
