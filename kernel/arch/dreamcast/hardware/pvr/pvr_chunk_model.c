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
        case PVR_CHUNK_VERTEX_XYZ_METADATA:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444:
        case PVR_CHUNK_VERTEX_XYZ_INTENSITY:
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL:
            return 4;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL:
            return 6;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_USER:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_METADATA:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_565:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_4444:
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY:
            return 7;
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER:
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_SPECULAR_ARGB:
        case PVR_CHUNK_VERTEX_XYZ_METADATA_ARGB:
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

        if(record->type >= PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL &&
           record->type <= PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER) {
            size_t vertex;

            for(vertex = 0; vertex < count; ++vertex) {
                if(payload[1u + vertex * stride + 3u] &
                   UINT32_C(0xc0000000)) {
                    errno = EILSEQ;
                    return -1;
                }
            }
        }

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

static int validate_unique_vertex_ranges(const pvr_chunk_model_t *model) {
    pvr_chunk_iterator_t outer;
    pvr_chunk_record_t outer_record;
    int outer_rv;

    if(pvr_chunk_vertex_iterator_init(&outer, model->vertex_words,
                                      model->vertex_word_count) < 0)
        return -1;

    while((outer_rv = pvr_chunk_iterator_next(&outer, &outer_record)) > 0) {
        const uint32_t *outer_payload;
        uint32_t outer_first;
        uint32_t outer_count;
        pvr_chunk_iterator_t inner;
        pvr_chunk_record_t inner_record;
        int inner_rv;

        if(outer_record.record_class != PVR_CHUNK_RECORD_VERTEX)
            continue;

        outer_payload = outer_record.payload;
        outer_first = outer_payload[0] & UINT32_C(0xffff);
        outer_count = outer_payload[0] >> 16;
        if(!outer_count)
            continue;
        if(pvr_chunk_vertex_iterator_init(&inner, model->vertex_words,
                                          model->vertex_word_count) < 0)
            return -1;

        while((inner_rv = pvr_chunk_iterator_next(&inner,
                                                   &inner_record)) > 0) {
            const uint32_t *inner_payload;
            uint32_t inner_first;
            uint32_t inner_count;

            if(inner_record.stream_word_offset >=
               outer_record.stream_word_offset)
                break;
            if(inner_record.record_class != PVR_CHUNK_RECORD_VERTEX)
                continue;

            inner_payload = inner_record.payload;
            inner_first = inner_payload[0] & UINT32_C(0xffff);
            inner_count = inner_payload[0] >> 16;
            if(inner_count && outer_first < inner_first + inner_count &&
               inner_first < outer_first + outer_count) {
                errno = EILSEQ;
                return -1;
            }
        }
        if(inner_rv < 0)
            return -1;
    }

    return outer_rv < 0 ? -1 : 0;
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

static int validate_strip_attributes(uint8_t type, const uint16_t *words) {
    uint16_t maximum;
    size_t uv_sets;
    size_t set;

    switch(type) {
        case PVR_CHUNK_STRIP_UV8:
        case PVR_CHUNK_STRIP_UV8_NORMAL:
        case PVR_CHUNK_STRIP_UV8_ARGB:
            maximum = 255u;
            uv_sets = 1u;
            break;
        case PVR_CHUNK_STRIP_UV10:
        case PVR_CHUNK_STRIP_UV10_NORMAL:
        case PVR_CHUNK_STRIP_UV10_ARGB:
            maximum = 1023u;
            uv_sets = 1u;
            break;
        case PVR_CHUNK_STRIP_UV8_TWO_VOLUME:
            maximum = 255u;
            uv_sets = 2u;
            break;
        case PVR_CHUNK_STRIP_UV10_TWO_VOLUME:
            maximum = 1023u;
            uv_sets = 2u;
            break;
        default:
            return 0;
    }

    for(set = 0; set < uv_sets; ++set) {
        if(words[1u + set * 2u] > maximum ||
           words[2u + set * 2u] > maximum) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
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
        if(vertex_count > info->maximum_strip_vertices)
            info->maximum_strip_vertices = vertex_count;

        /* Per-triangle user words follow the vertex which completes that
           triangle. Therefore no user words precede vertices zero through two,
           and each later vertex is preceded by the prior triangle's words. */
        for(vertex = 0; vertex < vertex_count; ++vertex) {
            size_t prior_flags = vertex > 2u ?
                                 (vertex - 2u) * user_words : 0u;
            size_t offset = vertex * vertex_words + prior_flags;

            if(validate_index(model, payload[offset], info) < 0 ||
               validate_strip_attributes(record->type,
                                         payload + offset) < 0)
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
    if(validate_unique_vertex_ranges(model) < 0)
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

int pvr_chunk_model_open(const pvr_chunk_model_t *model,
                         pvr_chunk_model_view_t *view) {
    pvr_chunk_model_info_t info;

    if(!view) {
        errno = EINVAL;
        return -1;
    }

    memset(view, 0, sizeof(*view));
    if(pvr_chunk_model_validate(model, &info) < 0)
        return -1;

    view->model = *model;
    view->info = info;
    return 0;
}

int pvr_chunk_vertex_batch_decode(const pvr_chunk_record_t *record,
                                  pvr_chunk_vertex_batch_t *batch) {
    const uint32_t *payload;
    size_t stride;
    size_t count;
    uint32_t first;

    if(batch)
        memset(batch, 0, sizeof(*batch));

    if(!record || !batch || record->stream != PVR_CHUNK_STREAM_VERTEX ||
       record->record_class != PVR_CHUNK_RECORD_VERTEX || !record->payload ||
       !record->payload_word_count) {
        errno = EINVAL;
        return -1;
    }

    stride = vertex_stride(record->type);
    payload = record->payload;
    first = payload[0] & UINT32_C(0xffff);
    count = payload[0] >> 16;

    if(!stride || count > (SIZE_MAX - 1u) / stride ||
       record->payload_word_count != 1u + count * stride ||
       count > UINT32_C(0x10000) - first) {
        errno = EILSEQ;
        return -1;
    }

    batch->type = record->type;
    batch->flags = record->flags;
    batch->first_index = (uint16_t)first;
    batch->entries = payload + 1u;
    batch->entry_count = count;
    batch->entry_word_count = stride;
    return 0;
}

int pvr_chunk_vertex_batch_get(const pvr_chunk_vertex_batch_t *batch,
                               size_t entry,
                               pvr_chunk_vertex_view_t *vertex) {
    const uint32_t *words;
    size_t expected_stride;
    size_t components;
    size_t component;

    if(vertex)
        memset(vertex, 0, sizeof(*vertex));

    if(!batch || !vertex || !batch->entries || entry >= batch->entry_count) {
        errno = EINVAL;
        return -1;
    }

    expected_stride = vertex_stride(batch->type);
    if(!expected_stride || batch->entry_word_count != expected_stride ||
       batch->entry_count > UINT32_C(0x10000) - batch->first_index ||
       entry > SIZE_MAX / batch->entry_word_count) {
        errno = EILSEQ;
        return -1;
    }

    words = batch->entries + entry * batch->entry_word_count;
    components = batch->type == PVR_CHUNK_VERTEX_XYZW ||
                 batch->type == PVR_CHUNK_VERTEX_XYZW_NORMAL ? 4u : 3u;

    for(component = 0; component < components; ++component) {
        vertex->position[component] = word_float(words[component]);
        if(!isfinite(vertex->position[component])) {
            memset(vertex, 0, sizeof(*vertex));
            errno = EILSEQ;
            return -1;
        }
    }

    if(components == 3u)
        vertex->position[3] = 1.0f;

    vertex->index = (uint16_t)(batch->first_index + entry);
    vertex->position_components = (uint8_t)components;
    vertex->words = words;
    vertex->word_count = batch->entry_word_count;
    return 0;
}

static uint8_t expand4(uint32_t value) {
    return (uint8_t)((value << 4) | value);
}

static uint8_t expand5(uint32_t value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(uint32_t value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static uint32_t color_rgb565(uint16_t value) {
    uint32_t red = expand5((value >> 11) & 31u);
    uint32_t green = expand6((value >> 5) & 63u);
    uint32_t blue = expand5(value & 31u);

    return UINT32_C(0xff000000) | (red << 16) | (green << 8) | blue;
}

static uint32_t color_argb4444(uint16_t value) {
    uint32_t alpha = expand4((value >> 12) & 15u);
    uint32_t red = expand4((value >> 8) & 15u);
    uint32_t green = expand4((value >> 4) & 15u);
    uint32_t blue = expand4(value & 15u);

    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

static float signed_normal(uint32_t value, unsigned int bits) {
    uint32_t sign = UINT32_C(1) << (bits - 1u);
    int32_t component = (value & sign) ?
                        (int32_t)(value - (UINT32_C(1) << bits)) :
                        (int32_t)value;
    int32_t maximum = (int32_t)sign - 1;

    if(component == -(int32_t)sign)
        return -1.0f;
    return (float)component / (float)maximum;
}

int pvr_chunk_vertex_attributes_get(
    const pvr_chunk_vertex_batch_t *batch, size_t entry,
    pvr_chunk_vertex_attributes_t *attributes) {
    pvr_chunk_vertex_attributes_t decoded;
    pvr_chunk_vertex_view_t vertex;
    const uint32_t *words;
    size_t normal_offset = SIZE_MAX;
    size_t color_offset = SIZE_MAX;

    if(attributes)
        memset(attributes, 0, sizeof(*attributes));
    if(!attributes || pvr_chunk_vertex_batch_get(batch, entry, &vertex) < 0)
        return -1;

    memset(&decoded, 0, sizeof(decoded));
    decoded.index = vertex.index;
    decoded.position.x = vertex.position[0];
    decoded.position.y = vertex.position[1];
    decoded.position.z = vertex.position[2];
    decoded.position.w = vertex.position[3];
    words = vertex.words;

    if(batch->type == PVR_CHUNK_VERTEX_XYZW_NORMAL)
        normal_offset = 4u;
    else if(batch->type >= PVR_CHUNK_VERTEX_XYZ_NORMAL &&
            batch->type <= PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY)
        normal_offset = 3u;

    if(normal_offset != SIZE_MAX) {
        decoded.normal.x = word_float(words[normal_offset]);
        decoded.normal.y = word_float(words[normal_offset + 1u]);
        decoded.normal.z = word_float(words[normal_offset + 2u]);
        decoded.normal.w = 0.0f;
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_NORMAL;
    }
    else if(batch->type >= PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL &&
            batch->type <= PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER) {
        uint32_t packed = words[3];

        if(packed & UINT32_C(0xc0000000)) {
            errno = EILSEQ;
            return -1;
        }
        decoded.normal.x = signed_normal((packed >> 20) & 1023u, 10);
        decoded.normal.y = signed_normal((packed >> 10) & 1023u, 10);
        decoded.normal.z = signed_normal(packed & 1023u, 10);
        decoded.normal.w = 0.0f;
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_NORMAL;
    }

    switch(batch->type) {
        case PVR_CHUNK_VERTEX_XYZ_ARGB:
            color_offset = 3u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB:
            color_offset = 6u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_ARGB:
            color_offset = 4u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_SPECULAR_ARGB:
            decoded.diffuse_argb = words[3];
            decoded.specular_argb = words[4];
            decoded.present |= PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR |
                               PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR;
            break;
        case PVR_CHUNK_VERTEX_XYZ_METADATA_ARGB:
            decoded.metadata = words[3];
            decoded.diffuse_argb = words[4];
            decoded.present |= PVR_CHUNK_VERTEX_ATTR_METADATA |
                               PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR;
            break;
        default:
            break;
    }

    if(color_offset != SIZE_MAX) {
        decoded.diffuse_argb = words[color_offset];
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR;
    }

    switch(batch->type) {
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565:
            color_offset = 3u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_565:
            color_offset = 6u;
            break;
        default:
            color_offset = SIZE_MAX;
            break;
    }
    if(color_offset != SIZE_MAX) {
        uint32_t packed = words[color_offset];

        decoded.diffuse_argb = color_rgb565((uint16_t)(packed >> 16));
        decoded.specular_argb = color_rgb565((uint16_t)packed);
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR |
                           PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR;
    }

    switch(batch->type) {
        case PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444:
            color_offset = 3u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_4444:
            color_offset = 6u;
            break;
        default:
            color_offset = SIZE_MAX;
            break;
    }
    if(color_offset != SIZE_MAX) {
        uint32_t packed = words[color_offset];

        decoded.diffuse_argb = color_argb4444((uint16_t)(packed >> 16));
        decoded.specular_argb = color_rgb565((uint16_t)packed);
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR |
                           PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR;
    }

    switch(batch->type) {
        case PVR_CHUNK_VERTEX_XYZ_INTENSITY:
            color_offset = 3u;
            break;
        case PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY:
            color_offset = 6u;
            break;
        default:
            color_offset = SIZE_MAX;
            break;
    }
    if(color_offset != SIZE_MAX) {
        uint32_t packed = words[color_offset];

        decoded.diffuse_intensity = (float)(packed >> 16) / 65535.0f;
        decoded.specular_intensity = (float)(packed & 65535u) / 65535.0f;
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_DIFFUSE_INTENSITY |
                           PVR_CHUNK_VERTEX_ATTR_SPECULAR_INTENSITY;
    }

    if(batch->type == PVR_CHUNK_VERTEX_XYZ_USER ||
       batch->type == PVR_CHUNK_VERTEX_XYZ_NORMAL_USER ||
       batch->type == PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER) {
        size_t offset = batch->type == PVR_CHUNK_VERTEX_XYZ_USER ? 3u :
                        (batch->type == PVR_CHUNK_VERTEX_XYZ_NORMAL_USER ?
                         6u : 4u);

        decoded.user_data = words[offset];
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_USER_DATA;
    }
    else if(batch->type == PVR_CHUNK_VERTEX_XYZ_METADATA ||
            batch->type == PVR_CHUNK_VERTEX_XYZ_NORMAL_METADATA) {
        size_t offset = batch->type == PVR_CHUNK_VERTEX_XYZ_METADATA ?
                        3u : 6u;

        decoded.metadata = words[offset];
        decoded.present |= PVR_CHUNK_VERTEX_ATTR_METADATA;
    }

    memcpy(attributes, &decoded, sizeof(decoded));
    return 0;
}

int pvr_chunk_model_vertex_attributes_get(
    const pvr_chunk_model_view_t *view, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    if(attributes)
        memset(attributes, 0, sizeof(*attributes));
    if(!view || !attributes || !view->model.vertex_words ||
       !view->model.vertex_word_count || index > view->info.maximum_vertex_index) {
        errno = !view || !attributes ? EINVAL : ENOENT;
        return -1;
    }

    if(pvr_chunk_vertex_iterator_init(&iterator, view->model.vertex_words,
                                      view->model.vertex_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        pvr_chunk_vertex_batch_t batch;
        uint32_t relative;

        if(record.record_class != PVR_CHUNK_RECORD_VERTEX)
            continue;
        if(pvr_chunk_vertex_batch_decode(&record, &batch) < 0)
            return -1;

        relative = (uint32_t)index - batch.first_index;
        if(index >= batch.first_index && relative < batch.entry_count)
            return pvr_chunk_vertex_attributes_get(&batch, relative,
                                                    attributes);
    }

    if(rv < 0)
        return -1;
    errno = ENOENT;
    return -1;
}

int pvr_chunk_strip_iterator_init(pvr_chunk_strip_iterator_t *iterator,
                                  const pvr_chunk_record_t *record) {
    const uint16_t *payload;
    size_t vertex_words;

    if(!iterator || !record || record->stream != PVR_CHUNK_STREAM_POLYGON ||
       record->record_class != PVR_CHUNK_RECORD_STRIP || !record->payload ||
       !record->payload_word_count) {
        errno = EINVAL;
        return -1;
    }

    memset(iterator, 0, sizeof(*iterator));
    vertex_words = strip_vertex_words(record->type);
    if(!vertex_words) {
        errno = EILSEQ;
        return -1;
    }

    payload = record->payload;
    iterator->type = record->type;
    iterator->flags = record->flags;
    iterator->cursor = payload + 1u;
    iterator->remaining_words = record->payload_word_count - 1u;
    iterator->remaining_strips = payload[0] & UINT16_C(0x3fff);
    iterator->vertex_word_count = vertex_words;
    iterator->user_word_count = payload[0] >> 14;
    return 0;
}

int pvr_chunk_strip_iterator_next(pvr_chunk_strip_iterator_t *iterator,
                                  pvr_chunk_strip_view_t *strip) {
    const uint16_t *words;
    uint16_t header;
    size_t vertex_count;
    size_t required;
    size_t vertex_words;
    size_t user_words;

    if(strip)
        memset(strip, 0, sizeof(*strip));

    if(!iterator || !strip || !iterator->cursor ||
       !iterator->vertex_word_count) {
        errno = EINVAL;
        return -1;
    }

    if(!iterator->remaining_strips) {
        if(iterator->remaining_words) {
            errno = EILSEQ;
            return -1;
        }
        return 0;
    }

    if(!iterator->remaining_words) {
        errno = EILSEQ;
        return -1;
    }

    header = *iterator->cursor;
    vertex_count = header & UINT16_C(0x7fff);
    vertex_words = iterator->vertex_word_count;
    user_words = iterator->user_word_count;

    if(vertex_count < 3u || vertex_count > SIZE_MAX / vertex_words ||
       (user_words && vertex_count - 2u >
        (SIZE_MAX - vertex_count * vertex_words) / user_words)) {
        errno = EILSEQ;
        return -1;
    }

    required = vertex_count * vertex_words +
               (vertex_count - 2u) * user_words;
    if(required > iterator->remaining_words - 1u ||
       (iterator->remaining_strips == 1u &&
        required != iterator->remaining_words - 1u)) {
        errno = EILSEQ;
        return -1;
    }

    words = iterator->cursor + 1u;
    strip->type = iterator->type;
    strip->flags = iterator->flags;
    strip->reversed = (header & UINT16_C(0x8000)) != 0;
    strip->words = words;
    strip->word_count = required;
    strip->vertex_count = vertex_count;
    strip->vertex_word_count = vertex_words;
    strip->user_word_count = user_words;

    iterator->cursor = words + required;
    iterator->remaining_words -= required + 1u;
    --iterator->remaining_strips;
    return 1;
}

int pvr_chunk_strip_vertex_get(const pvr_chunk_strip_view_t *strip,
                               size_t vertex_index,
                               pvr_chunk_strip_vertex_view_t *vertex) {
    size_t expected;
    size_t prior_user_words;
    size_t offset;

    if(vertex)
        memset(vertex, 0, sizeof(*vertex));

    if(!strip || !vertex || !strip->words ||
       vertex_index >= strip->vertex_count ||
       !strip->vertex_word_count || strip->vertex_count < 3u ||
       strip->vertex_count > SIZE_MAX / strip->vertex_word_count ||
       (strip->user_word_count && strip->vertex_count - 2u >
        (SIZE_MAX - strip->vertex_count * strip->vertex_word_count) /
        strip->user_word_count)) {
        errno = EINVAL;
        return -1;
    }

    expected = strip->vertex_count * strip->vertex_word_count +
               (strip->vertex_count - 2u) * strip->user_word_count;
    if(strip->word_count != expected) {
        errno = EILSEQ;
        return -1;
    }

    prior_user_words = vertex_index > 2u ?
                       (vertex_index - 2u) * strip->user_word_count : 0u;
    offset = vertex_index * strip->vertex_word_count + prior_user_words;

    vertex->index = strip->words[offset];
    vertex->attribute_words = strip->words + offset + 1u;
    vertex->attribute_word_count = strip->vertex_word_count - 1u;
    if(vertex_index >= 2u) {
        vertex->triangle_user_words =
            strip->words + offset + strip->vertex_word_count;
        vertex->triangle_user_word_count = strip->user_word_count;
    }
    return 0;
}

static int decode_uv(float uv[2], const uint16_t *words, uint16_t maximum) {
    if(words[0] > maximum || words[1] > maximum) {
        errno = EILSEQ;
        return -1;
    }

    uv[0] = (float)words[0] / (float)maximum;
    uv[1] = (float)words[1] / (float)maximum;
    return 0;
}

static uint32_t strip_color(const uint16_t *words) {
    uint32_t alpha = words[0] >> 8;
    uint32_t red = words[0] & 255u;
    uint32_t green = words[1] >> 8;
    uint32_t blue = words[1] & 255u;

    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

int pvr_chunk_strip_attributes_get(
    const pvr_chunk_strip_view_t *strip, size_t vertex_index,
    pvr_chunk_strip_attributes_t *attributes) {
    pvr_chunk_strip_attributes_t decoded;
    pvr_chunk_strip_vertex_view_t vertex;
    const uint16_t *words;
    uint16_t uv_maximum = 0;
    size_t expected;
    size_t normal_offset = SIZE_MAX;
    size_t color_offset = SIZE_MAX;

    if(attributes)
        memset(attributes, 0, sizeof(*attributes));
    if(!attributes ||
       pvr_chunk_strip_vertex_get(strip, vertex_index, &vertex) < 0)
        return -1;

    memset(&decoded, 0, sizeof(decoded));
    decoded.index = vertex.index;
    decoded.triangle_user_words = vertex.triangle_user_words;
    decoded.triangle_user_word_count = vertex.triangle_user_word_count;
    words = vertex.attribute_words;

    switch(strip->type) {
        case PVR_CHUNK_STRIP_INDEX:
        case PVR_CHUNK_STRIP_TWO_VOLUME:
            expected = 0u;
            break;
        case PVR_CHUNK_STRIP_UV8:
            expected = 2u;
            uv_maximum = 255u;
            break;
        case PVR_CHUNK_STRIP_UV10:
            expected = 2u;
            uv_maximum = 1023u;
            break;
        case PVR_CHUNK_STRIP_NORMAL:
            expected = 3u;
            normal_offset = 0u;
            break;
        case PVR_CHUNK_STRIP_UV8_NORMAL:
            expected = 5u;
            uv_maximum = 255u;
            normal_offset = 2u;
            break;
        case PVR_CHUNK_STRIP_UV10_NORMAL:
            expected = 5u;
            uv_maximum = 1023u;
            normal_offset = 2u;
            break;
        case PVR_CHUNK_STRIP_ARGB:
            expected = 2u;
            color_offset = 0u;
            break;
        case PVR_CHUNK_STRIP_UV8_ARGB:
            expected = 4u;
            uv_maximum = 255u;
            color_offset = 2u;
            break;
        case PVR_CHUNK_STRIP_UV10_ARGB:
            expected = 4u;
            uv_maximum = 1023u;
            color_offset = 2u;
            break;
        case PVR_CHUNK_STRIP_UV8_TWO_VOLUME:
            expected = 4u;
            uv_maximum = 255u;
            break;
        case PVR_CHUNK_STRIP_UV10_TWO_VOLUME:
            expected = 4u;
            uv_maximum = 1023u;
            break;
        default:
            errno = EILSEQ;
            return -1;
    }

    if(vertex.attribute_word_count != expected) {
        errno = EILSEQ;
        return -1;
    }

    if(uv_maximum) {
        if(decode_uv(decoded.uv[0], words, uv_maximum) < 0)
            return -1;
        decoded.present |= PVR_CHUNK_STRIP_ATTR_UV0;
        if(strip->type == PVR_CHUNK_STRIP_UV8_TWO_VOLUME ||
           strip->type == PVR_CHUNK_STRIP_UV10_TWO_VOLUME) {
            if(decode_uv(decoded.uv[1], words + 2u, uv_maximum) < 0)
                return -1;
            decoded.present |= PVR_CHUNK_STRIP_ATTR_UV1;
        }
    }

    if(normal_offset != SIZE_MAX) {
        decoded.normal.x = signed_normal(words[normal_offset], 16);
        decoded.normal.y = signed_normal(words[normal_offset + 1u], 16);
        decoded.normal.z = signed_normal(words[normal_offset + 2u], 16);
        decoded.normal.w = 0.0f;
        decoded.present |= PVR_CHUNK_STRIP_ATTR_NORMAL;
    }

    if(color_offset != SIZE_MAX) {
        decoded.argb = strip_color(words + color_offset);
        decoded.present |= PVR_CHUNK_STRIP_ATTR_COLOR;
    }

    memcpy(attributes, &decoded, sizeof(decoded));
    return 0;
}
