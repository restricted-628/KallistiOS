/* KallistiOS ##version##

   pvr_chunk_model.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* The streams deliberately use different natural words. Vertex records carry
   their payload length in the upper half of the first 32-bit word. Variable
   polygon records put a 16-bit payload length after the type/flags word. Both
   lengths exclude the word that contains the length itself. Keeping this
   distinction centralized in the iterator prevents every renderer and asset
   tool from reproducing the most error-prone pointer arithmetic. */

#define CHUNK_TYPE_NULL             0u
#define CHUNK_TYPE_BITS_FIRST       1u
#define CHUNK_TYPE_BITS_LAST        5u
#define CHUNK_TYPE_TEXTURE_FIRST    8u
#define CHUNK_TYPE_TEXTURE_LAST     9u
#define CHUNK_TYPE_MATERIAL_FIRST   17u
#define CHUNK_TYPE_MATERIAL_LAST    31u
#define CHUNK_TYPE_VERTEX_FIRST     32u
#define CHUNK_TYPE_VERTEX_LAST      52u
#define CHUNK_TYPE_VOLUME_FIRST     56u
#define CHUNK_TYPE_VOLUME_LAST      58u
#define CHUNK_TYPE_STRIP_FIRST      64u
#define CHUNK_TYPE_STRIP_LAST       75u
#define CHUNK_TYPE_SHAPE_FIRST      128u
#define CHUNK_TYPE_SHAPE_LAST       130u
#define CHUNK_TYPE_END              255u

static int checked_add(size_t *value, size_t addend) {
    if(addend > SIZE_MAX - *value) {
        errno = ERANGE;
        return -1;
    }

    *value += addend;
    return 0;
}

static int classify_vertex(uint8_t type,
                           pvr_chunk_record_class_t *record_class) {
    if(type == CHUNK_TYPE_NULL)
        *record_class = PVR_CHUNK_RECORD_NULL;
    else if(type >= CHUNK_TYPE_VERTEX_FIRST &&
            type <= CHUNK_TYPE_VERTEX_LAST)
        *record_class = PVR_CHUNK_RECORD_VERTEX;
    else if(type >= CHUNK_TYPE_SHAPE_FIRST &&
            type <= CHUNK_TYPE_SHAPE_LAST)
        *record_class = PVR_CHUNK_RECORD_SHAPE;
    else if(type == CHUNK_TYPE_END)
        *record_class = PVR_CHUNK_RECORD_END;
    else {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

static int classify_polygon(uint8_t type,
                            pvr_chunk_record_class_t *record_class) {
    if(type == CHUNK_TYPE_NULL)
        *record_class = PVR_CHUNK_RECORD_NULL;
    else if(type >= CHUNK_TYPE_BITS_FIRST && type <= CHUNK_TYPE_BITS_LAST)
        *record_class = PVR_CHUNK_RECORD_BITS;
    else if(type >= CHUNK_TYPE_TEXTURE_FIRST &&
            type <= CHUNK_TYPE_TEXTURE_LAST)
        *record_class = PVR_CHUNK_RECORD_TEXTURE;
    else if(type >= CHUNK_TYPE_MATERIAL_FIRST &&
            type <= CHUNK_TYPE_MATERIAL_LAST)
        *record_class = PVR_CHUNK_RECORD_MATERIAL;
    else if(type >= CHUNK_TYPE_VOLUME_FIRST &&
            type <= CHUNK_TYPE_VOLUME_LAST)
        *record_class = PVR_CHUNK_RECORD_VOLUME;
    else if(type >= CHUNK_TYPE_STRIP_FIRST &&
            type <= CHUNK_TYPE_STRIP_LAST)
        *record_class = PVR_CHUNK_RECORD_STRIP;
    else if(type == CHUNK_TYPE_END)
        *record_class = PVR_CHUNK_RECORD_END;
    else {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

static int iterator_init(pvr_chunk_iterator_t *iterator,
                         pvr_chunk_stream_kind_t kind, const void *words,
                         size_t word_count, size_t alignment) {
    if(!iterator || !words || !word_count ||
       ((uintptr_t)words & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }

    memset(iterator, 0, sizeof(*iterator));
    iterator->kind = kind;
    iterator->words = words;
    iterator->word_count = word_count;
    return 0;
}

int pvr_chunk_vertex_iterator_init(pvr_chunk_iterator_t *iterator,
                                   const uint32_t *words,
                                   size_t word_count) {
    return iterator_init(iterator, PVR_CHUNK_STREAM_VERTEX, words,
                         word_count, _Alignof(uint32_t));
}

int pvr_chunk_polygon_iterator_init(pvr_chunk_iterator_t *iterator,
                                    const uint16_t *words,
                                    size_t word_count) {
    return iterator_init(iterator, PVR_CHUNK_STREAM_POLYGON, words,
                         word_count, _Alignof(uint16_t));
}

static int next_vertex(pvr_chunk_iterator_t *iterator,
                       pvr_chunk_record_t *record) {
    const uint32_t *words = iterator->words;
    uint32_t header = words[iterator->offset];
    size_t record_words = 1;
    uint8_t type = (uint8_t)header;

    record->type = type;
    record->flags = (uint8_t)(header >> 8);

    if(classify_vertex(type, &record->record_class) < 0)
        return -1;

    if(record->record_class == PVR_CHUNK_RECORD_VERTEX ||
       record->record_class == PVR_CHUNK_RECORD_SHAPE) {
        size_t payload_words = header >> 16;

        if(payload_words > SIZE_MAX - 1u) {
            errno = ERANGE;
            return -1;
        }

        record_words += payload_words;
    }
    else if(header >> 8) {
        /* Null and end records have no flags, size, or payload. */
        errno = EILSEQ;
        return -1;
    }

    if(record_words > iterator->word_count - iterator->offset) {
        errno = EILSEQ;
        return -1;
    }

    record->word_count = record_words;
    record->payload = words + iterator->offset + 1u;
    record->payload_word_count = record_words - 1u;
    return 0;
}

static int next_polygon(pvr_chunk_iterator_t *iterator,
                        pvr_chunk_record_t *record) {
    const uint16_t *words = iterator->words;
    uint16_t header = words[iterator->offset];
    size_t remaining = iterator->word_count - iterator->offset;
    size_t record_words;
    uint8_t type = (uint8_t)header;

    record->type = type;
    record->flags = (uint8_t)(header >> 8);

    if(classify_polygon(type, &record->record_class) < 0)
        return -1;

    switch(record->record_class) {
        case PVR_CHUNK_RECORD_NULL:
        case PVR_CHUNK_RECORD_BITS:
        case PVR_CHUNK_RECORD_END:
            record_words = 1;
            break;

        case PVR_CHUNK_RECORD_TEXTURE:
            record_words = 2;
            break;

        case PVR_CHUNK_RECORD_MATERIAL:
        case PVR_CHUNK_RECORD_VOLUME:
        case PVR_CHUNK_RECORD_STRIP:
            if(remaining < 2u) {
                errno = EILSEQ;
                return -1;
            }
            record_words = (size_t)words[iterator->offset + 1u] + 2u;
            break;

        default:
            errno = EILSEQ;
            return -1;
    }

    if(record_words > remaining) {
        errno = EILSEQ;
        return -1;
    }

    if((record->record_class == PVR_CHUNK_RECORD_NULL ||
        record->record_class == PVR_CHUNK_RECORD_END) && record->flags) {
        errno = EILSEQ;
        return -1;
    }

    record->word_count = record_words;
    record->payload = words + iterator->offset +
                      ((record_words > 1u &&
                        record->record_class != PVR_CHUNK_RECORD_TEXTURE) ?
                       2u : 1u);
    record->payload_word_count = record_words -
                                 ((record_words > 1u &&
                                   record->record_class !=
                                   PVR_CHUNK_RECORD_TEXTURE) ? 2u : 1u);
    return 0;
}

int pvr_chunk_iterator_next(pvr_chunk_iterator_t *iterator,
                            pvr_chunk_record_t *record) {
    if(record)
        memset(record, 0, sizeof(*record));

    if(!iterator || !record || !iterator->words || !iterator->word_count ||
       (iterator->kind != PVR_CHUNK_STREAM_VERTEX &&
        iterator->kind != PVR_CHUNK_STREAM_POLYGON) ||
       iterator->offset > iterator->word_count) {
        errno = EINVAL;
        return -1;
    }

    if(iterator->ended)
        return 0;

    if(iterator->offset == iterator->word_count) {
        errno = EILSEQ;
        return -1;
    }

    record->stream = iterator->kind;
    record->words = iterator->kind == PVR_CHUNK_STREAM_VERTEX ?
                    (const void *)((const uint32_t *)iterator->words +
                                   iterator->offset) :
                    (const void *)((const uint16_t *)iterator->words +
                                   iterator->offset);
    record->stream_word_offset = iterator->offset;

    if((iterator->kind == PVR_CHUNK_STREAM_VERTEX ?
        next_vertex(iterator, record) : next_polygon(iterator, record)) < 0)
        return -1;

    iterator->offset += record->word_count;

    if(record->record_class == PVR_CHUNK_RECORD_END) {
        if(iterator->offset != iterator->word_count) {
            errno = EILSEQ;
            return -1;
        }
        iterator->ended = 1;
    }

    return 1;
}

static size_t vertex_stride(uint8_t type) {
    switch(type) {
        case PVR_CHUNK_VERTEX_XYZW:
            return 4;
        case PVR_CHUNK_VERTEX_XYZW_NORMAL:
            return 8;
        case PVR_CHUNK_VERTEX_XYZ:
            return 3;
        case PVR_CHUNK_VERTEX_XYZ_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_USER:
        case PVR_CHUNK_VERTEX_XYZ_WEIGHT:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444:
        case PVR_CHUNK_VERTEX_XYZ_INTENSITY:
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL:
            return 4;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL:
            return 6;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_USER:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_WEIGHT:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_565:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_4444:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY:
            return 7;
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_SPECULAR_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_WEIGHT_ARGB:
            return 5;
        case PVR_CHUNK_SHAPE_NORMAL:
            return 3;
        case PVR_CHUNK_SHAPE_NORMAL_ARGB:
            return 4;
        case PVR_CHUNK_SHAPE_ARGB:
            return 1;
        default:
            return 0;
    }
}

static float word_float(uint32_t word) {
    float value;

    memcpy(&value, &word, sizeof(value));
    return value;
}

static int validate_floats(const uint32_t *data, size_t count,
                           size_t stride, size_t first, size_t number) {
    size_t i;
    size_t component;

    for(i = 0; i < count; ++i) {
        for(component = 0; component < number; ++component) {
            if(!isfinite(word_float(data[i * stride + first + component]))) {
                errno = EILSEQ;
                return -1;
            }
        }
    }

    return 0;
}

static int validate_vertex_record(const pvr_chunk_record_t *record,
                                  pvr_chunk_model_info_t *info) {
    const uint32_t *payload = record->payload;
    size_t stride = vertex_stride(record->type);
    size_t count;
    size_t expected;
    uint32_t first_index;

    if(!stride || !record->payload_word_count) {
        errno = EILSEQ;
        return -1;
    }

    first_index = payload[0] & UINT32_C(0xffff);
    count = payload[0] >> 16;

    if(count > (SIZE_MAX - 1u) / stride) {
        errno = ERANGE;
        return -1;
    }

    expected = 1u + count * stride;
    if(record->payload_word_count != expected ||
       count > UINT32_C(0x10000) - first_index) {
        errno = EILSEQ;
        return -1;
    }

    if(record->record_class == PVR_CHUNK_RECORD_VERTEX) {
        if(validate_floats(payload + 1u, count, stride, 0, 3) < 0)
            return -1;

        if((record->type == PVR_CHUNK_VERTEX_XYZW ||
            record->type == PVR_CHUNK_VERTEX_XYZW_NORMAL) &&
           validate_floats(payload + 1u, count, stride, 3, 1) < 0)
            return -1;

        if((record->type == PVR_CHUNK_VERTEX_XYZW_NORMAL &&
            validate_floats(payload + 1u, count, stride, 4, 3) < 0) ||
           (record->type >= PVR_CHUNK_VERTEX_XYZ_NORMAL &&
            record->type <= PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY &&
            validate_floats(payload + 1u, count, stride, 3, 3) < 0))
            return -1;

        if(checked_add(&info->vertex_records, 1u) < 0 ||
           checked_add(&info->vertex_entries, count) < 0)
            return -1;

        if(count) {
            uint32_t last = first_index + (uint32_t)count - 1u;

            if(last > info->maximum_vertex_index)
                info->maximum_vertex_index = last;
        }
    }
    else {
        if((record->type == PVR_CHUNK_SHAPE_NORMAL ||
            record->type == PVR_CHUNK_SHAPE_NORMAL_ARGB) &&
           validate_floats(payload + 1u, count, stride, 0, 3) < 0)
            return -1;

        if(checked_add(&info->shape_records, 1u) < 0)
            return -1;
    }

    return 0;
}

static int vertex_index_defined(const pvr_chunk_model_t *model,
                                uint16_t index) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    /* Resolve ranges directly from the bounded stream. This is intentionally
       allocation-free: validation may be slower than a temporary 64K bitmap,
       but it adds no hidden workspace to small programs and remains bounded by
       the supplied model. A later renderer may build its own caller-owned
       indexed cache after this admission check succeeds. */
    if(pvr_chunk_vertex_iterator_init(&iterator, model->vertex_words,
                                      model->vertex_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        const uint32_t *payload;
        uint32_t first;
        uint32_t count;

        if(record.record_class != PVR_CHUNK_RECORD_VERTEX)
            continue;

        payload = record.payload;
        first = payload[0] & UINT32_C(0xffff);
        count = payload[0] >> 16;
        if(index >= first && (uint32_t)index - first < count)
            return 1;
    }

    return rv < 0 ? -1 : 0;
}

static size_t strip_vertex_words(uint8_t type) {
    switch(type) {
        case PVR_CHUNK_STRIP_INDEX:
        case PVR_CHUNK_STRIP_TWO_VOLUME:
            return 1;
        case PVR_CHUNK_STRIP_UV8:
        case PVR_CHUNK_STRIP_UV10:
        case PVR_CHUNK_STRIP_ARGB:
            return 3;
        case PVR_CHUNK_STRIP_NORMAL:
            return 4;
        case PVR_CHUNK_STRIP_UV8_ARGB:
        case PVR_CHUNK_STRIP_UV10_ARGB:
        case PVR_CHUNK_STRIP_UV8_TWO_VOLUME:
        case PVR_CHUNK_STRIP_UV10_TWO_VOLUME:
            return 5;
        case PVR_CHUNK_STRIP_UV8_NORMAL:
        case PVR_CHUNK_STRIP_UV10_NORMAL:
            return 6;
        default:
            return 0;
    }
}

static int validate_index(const pvr_chunk_model_t *model, uint16_t index,
                          pvr_chunk_model_info_t *info) {
    int defined = vertex_index_defined(model, index);

    if(defined <= 0) {
        if(!defined)
            errno = EILSEQ;
        return -1;
    }

    return checked_add(&info->index_references, 1u);
}

static int validate_strips(const pvr_chunk_model_t *model,
                           const pvr_chunk_record_t *record,
                           pvr_chunk_model_info_t *info) {
    const uint16_t *payload = record->payload;
    size_t remaining = record->payload_word_count;
    size_t vertex_words = strip_vertex_words(record->type);
    size_t strip_count;
    size_t user_words;
    size_t strip;

    if(!remaining || !vertex_words) {
        errno = EILSEQ;
        return -1;
    }

    user_words = payload[0] >> 14;
    strip_count = payload[0] & UINT16_C(0x3fff);
    ++payload;
    --remaining;

    for(strip = 0; strip < strip_count; ++strip) {
        size_t vertex_count;
        size_t required;
        size_t vertex;

        if(!remaining) {
            errno = EILSEQ;
            return -1;
        }

        vertex_count = *payload & UINT16_C(0x7fff);
        ++payload;
        --remaining;

        if(vertex_count < 3u ||
           vertex_count > SIZE_MAX / vertex_words ||
           vertex_count - 2u >
           (SIZE_MAX - vertex_count * vertex_words) /
           (user_words ? user_words : 1u)) {
            errno = EILSEQ;
            return -1;
        }

        required = vertex_count * vertex_words +
                   (vertex_count - 2u) * user_words;
        if(required > remaining) {
            errno = EILSEQ;
            return -1;
        }

        /* Per-triangle user words follow the vertex which completes that
           triangle. Therefore no user words precede vertices zero through two,
           and each later vertex is preceded by the prior triangle's words. */
        for(vertex = 0; vertex < vertex_count; ++vertex) {
            size_t prior_flags = vertex > 2u ?
                                 (vertex - 2u) * user_words : 0u;
            size_t offset = vertex * vertex_words + prior_flags;

            if(validate_index(model, payload[offset], info) < 0)
                return -1;
        }

        payload += required;
        remaining -= required;

        if(checked_add(&info->strips, 1u) < 0 ||
           checked_add(&info->triangles, vertex_count - 2u) < 0)
            return -1;
    }

    if(remaining) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

static int validate_volume(const pvr_chunk_model_t *model,
                           const pvr_chunk_record_t *record,
                           pvr_chunk_model_info_t *info) {
    const uint16_t *payload = record->payload;
    size_t remaining = record->payload_word_count;
    size_t user_words;
    size_t primitive_count;
    size_t indices;
    size_t primitive;

    if(!remaining) {
        errno = EILSEQ;
        return -1;
    }

    user_words = payload[0] >> 14;
    primitive_count = payload[0] & UINT16_C(0x3fff);

    if(record->type == CHUNK_TYPE_VOLUME_FIRST)
        indices = 3;
    else if(record->type == CHUNK_TYPE_VOLUME_FIRST + 1u)
        indices = 4;
    else {
        /* Volume strip framing matches an index-only polygon strip. */
        pvr_chunk_record_t strip_record = *record;

        strip_record.type = PVR_CHUNK_STRIP_INDEX;
        return validate_strips(model, &strip_record, info);
    }

    if(primitive_count > (SIZE_MAX - 1u) / (indices + user_words) ||
       remaining != 1u + primitive_count * (indices + user_words)) {
        errno = EILSEQ;
        return -1;
    }

    ++payload;
    for(primitive = 0; primitive < primitive_count; ++primitive) {
        size_t index;

        for(index = 0; index < indices; ++index) {
            if(validate_index(model, payload[index], info) < 0)
                return -1;
        }
        payload += indices + user_words;
    }

    return 0;
}

static size_t material_payload_words(uint8_t type) {
    static const uint8_t lengths[] = {
        2, 2, 4, 2, 4, 4, 6, 6, 2, 2, 4, 2, 4, 4, 6
    };

    return lengths[type - CHUNK_TYPE_MATERIAL_FIRST];
}

static int validate_polygon_record(const pvr_chunk_model_t *model,
                                   const pvr_chunk_record_t *record,
                                   pvr_chunk_model_info_t *info) {
    if(checked_add(&info->polygon_records, 1u) < 0)
        return -1;

    switch(record->record_class) {
        case PVR_CHUNK_RECORD_MATERIAL:
            if(record->payload_word_count !=
               material_payload_words(record->type)) {
                errno = EILSEQ;
                return -1;
            }
            return checked_add(&info->material_records, 1u);

        case PVR_CHUNK_RECORD_STRIP:
            if(validate_strips(model, record, info) < 0)
                return -1;
            return checked_add(&info->strip_records, 1u);

        case PVR_CHUNK_RECORD_VOLUME:
            return validate_volume(model, record, info);

        default:
            return 0;
    }
}

int pvr_chunk_model_validate(const pvr_chunk_model_t *model,
                             pvr_chunk_model_info_t *info) {
    pvr_chunk_model_info_t result;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int saw_end = 0;
    int rv;

    memset(&result, 0, sizeof(result));
    if(info)
        *info = result;

    if(!model || !model->vertex_words || !model->vertex_word_count ||
       !model->polygon_words || !model->polygon_word_count ||
       ((uintptr_t)model->vertex_words & (_Alignof(uint32_t) - 1u)) ||
       ((uintptr_t)model->polygon_words & (_Alignof(uint16_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    if(!isfinite(model->center[0]) || !isfinite(model->center[1]) ||
       !isfinite(model->center[2]) || !isfinite(model->radius) ||
       model->radius < 0.0f) {
        errno = EILSEQ;
        return -1;
    }

    if(pvr_chunk_vertex_iterator_init(&iterator, model->vertex_words,
                                      model->vertex_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END) {
            saw_end = 1;
            break;
        }

        if(record.record_class == PVR_CHUNK_RECORD_VERTEX ||
           record.record_class == PVR_CHUNK_RECORD_SHAPE) {
            if(validate_vertex_record(&record, &result) < 0)
                return -1;
        }
    }

    if(rv < 0 || !saw_end)
        return -1;

    saw_end = 0;
    if(pvr_chunk_polygon_iterator_init(&iterator, model->polygon_words,
                                       model->polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END) {
            saw_end = 1;
            break;
        }

        if(validate_polygon_record(model, &record, &result) < 0)
            return -1;
    }

    if(rv < 0 || !saw_end)
        return -1;

    if(info)
        *info = result;
    return 0;
}
