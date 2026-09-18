/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include "pvr-reference-ir.h"
#include <errno.h>

static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    if(!an || !bn)
        return 0;
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

static int resolve_pass(const pvr_chunk_model_t *model, size_t triangles,
                         pvr_reference_ir_t *refs, size_t count, bool write) {
    pvr_chunk_iterator_t polygons;
    pvr_chunk_record_t record;
    size_t cursor = 0, ordinal = 0;
    int next;
    if(pvr_chunk_polygon_iterator_init(&polygons, model->polygon_words,
                                       model->polygon_word_count) < 0)
        return -1;
    while((next = pvr_chunk_iterator_next(&polygons, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_VOLUME ||
           (record.record_class == PVR_CHUNK_RECORD_TEXTURE &&
            record.type == PVR_CHUNK_TEXTURE_TWO_VOLUME) ||
           (record.record_class == PVR_CHUNK_RECORD_MATERIAL &&
            record.type >= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME)) {
            errno = ENOTSUP;
            return -1;
        }
        if(record.record_class != PVR_CHUNK_RECORD_STRIP)
            continue;
        pvr_chunk_strip_iterator_t strips;
        pvr_chunk_strip_view_t strip;
        int found;
        if(pvr_chunk_strip_iterator_init(&strips, &record) < 0)
            return -1;
        while((found = pvr_chunk_strip_iterator_next(&strips, &strip)) > 0) {
            if(strip.type == PVR_CHUNK_STRIP_TWO_VOLUME ||
               strip.type == PVR_CHUNK_STRIP_UV8_TWO_VOLUME ||
               strip.type == PVR_CHUNK_STRIP_UV10_TWO_VOLUME ||
               strip.type == PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME ||
               strip.type == PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME ||
               strip.type == PVR_CHUNK_STRIP_UV_FLOAT_TWO_VOLUME) {
                errno = ENOTSUP;
                return -1;
            }
            for(size_t i = 0; i < strip.vertex_count; ++i) {
                pvr_chunk_strip_attributes_t attributes;
                if(cursor >= count) {
                    errno = EILSEQ;
                    return -1;
                }
                if(pvr_chunk_strip_attributes_get(&strip, i, &attributes) < 0)
                    return -1;
                if(attributes.present & PVR_CHUNK_STRIP_ATTR_UV1) {
                    errno = ENOTSUP;
                    return -1;
                }
                pvr_reference_ir_t *reference = &refs[cursor++];
                if(reference->triangle >= triangles || reference->corner > 2 ||
                   reference->vertex != attributes.index) {
                    errno = EILSEQ;
                    return -1;
                }
                if(write) {
                    reference->strip = ordinal;
                    reference->reversed = strip.reversed != 0;
                    reference->has_uv =
                        (attributes.present & PVR_CHUNK_STRIP_ATTR_UV0) != 0;
                    reference->canonical[0] = reference->has_uv ?
                        attributes.uv[0][0] : 0;
                    reference->canonical[1] = reference->has_uv ?
                        attributes.uv[0][1] : 0;
                }
            }
            ++ordinal;
        }
        if(found < 0)
            return -1;
    }
    if(next < 0)
        return -1;
    if(cursor != count) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_reference_ir_resolve(const pvr_chunk_model_t *model,
                             size_t triangle_count,
                             pvr_reference_ir_t *refs, size_t count) {
    pvr_chunk_model_info_t info;
    if(!model || !refs || !count || !triangle_count) {
        errno = EINVAL;
        return -1;
    }
    if(count > SIZE_MAX / sizeof(*refs) ||
       model->vertex_word_count > SIZE_MAX / sizeof(*model->vertex_words) ||
       model->polygon_word_count > SIZE_MAX / sizeof(*model->polygon_words)) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t bytes = count * sizeof(*refs);
    if(overlap(refs, bytes, model, sizeof(*model)) ||
       overlap(refs, bytes, model->vertex_words,
               model->vertex_word_count * sizeof(*model->vertex_words)) ||
       overlap(refs, bytes, model->polygon_words,
               model->polygon_word_count * sizeof(*model->polygon_words))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_validate(model, &info) < 0)
        return -1;
    if(info.requirements) {
        errno = ENOTSUP;
        return -1;
    }
    if(info.index_references != count || info.triangles != triangle_count) {
        errno = EILSEQ;
        return -1;
    }
    if(resolve_pass(model, triangle_count, refs, count, false) < 0)
        return -1;
    /* Complete framing and correspondence have passed. With immutable source
       streams, the second pass only publishes decoded host metadata. */
    return resolve_pass(model, triangle_count, refs, count, true);
}
