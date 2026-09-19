/* KallistiOS ##version##

   dc/pvr/pvr_chunk_skin_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skin_asset.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 36,
    HEADER_RESERVED_OFFSET = 40,
    HEADER_CRC_OFFSET = 44,
    HEADER_CRC_BYTES = 44
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t crc32_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    for(index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int ranges_overlap(const void *left, size_t left_bytes,
                          const void *right, size_t right_bytes) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if(!left_bytes || !right_bytes)
        return 0;
    if(left_start > UINTPTR_MAX - left_bytes ||
       right_start > UINTPTR_MAX - right_bytes)
        return 1;
    return left_start < right_start + right_bytes &&
           right_start < left_start + left_bytes;
}

static void decode_span(const uint8_t *record,
                        pvr_chunk_skin_span_t *span) {
    span->vertex_index = read_le16(record);
    span->weight_count = read_le16(record + 2);
    span->first_weight = read_le32(record + 4);
}

static void decode_weight(const uint8_t *record,
                          pvr_chunk_skin_weight_t *weight) {
    weight->joint = read_le16(record);
    weight->weight = read_le16(record + 2);
}

int pvr_chunk_skin_general_section_open(
    const void *data, size_t size,
    pvr_chunk_skin_general_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_skin_general_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t span_count;
    uint32_t weight_count;
    uint32_t joint_count;
    uint32_t span_bytes;
    uint32_t weight_bytes;
    uint64_t encoded_payload_bytes;
    size_t payload_bytes;
    size_t next_weight = 0;
    size_t index;
    uint16_t previous_vertex = 0;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_SKIN_GENERAL_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_SKIN_GENERAL_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES ||
       read_le16(bytes + 24) != PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES ||
       read_le16(bytes + 26) != PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES ||
       read_le32(bytes + HEADER_RESERVED_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    span_count = read_le32(bytes + 12);
    weight_count = read_le32(bytes + 16);
    joint_count = read_le32(bytes + 20);
    span_bytes = read_le32(bytes + 28);
    weight_bytes = read_le32(bytes + 32);
    if(file_bytes != size || !span_count || !weight_count || !joint_count ||
       joint_count > UINT16_MAX + 1u ||
       span_count > UINT32_MAX / PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES ||
       weight_count > UINT32_MAX / PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES ||
       span_bytes != span_count * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES ||
       weight_bytes != weight_count * PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    encoded_payload_bytes = (uint64_t)span_bytes + weight_bytes;
    if(encoded_payload_bytes >
       UINT32_MAX - PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    payload_bytes = (size_t)encoded_payload_bytes;
    if(payload_bytes != file_bytes - PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES, payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.spans = bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES;
    parsed.span_count = span_count;
    parsed.weights = bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES +
                     span_bytes;
    parsed.weight_count = weight_count;
    parsed.joint_count = joint_count;
    parsed.version = PVR_CHUNK_SKIN_GENERAL_VERSION;

    for(index = 0; index < parsed.span_count; ++index) {
        pvr_chunk_skin_span_t span;
        uint32_t total = 0;
        size_t slot;

        decode_span((const uint8_t *)parsed.spans +
                        index * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES,
                    &span);
        if((index && span.vertex_index <= previous_vertex) ||
           !span.weight_count || span.first_weight != next_weight ||
           span.weight_count > parsed.weight_count - next_weight) {
            errno = EILSEQ;
            return -1;
        }
        for(slot = 0; slot < span.weight_count; ++slot) {
            pvr_chunk_skin_weight_t weight;

            decode_weight((const uint8_t *)parsed.weights +
                              (next_weight + slot) *
                                  PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES,
                          &weight);
            if(!weight.weight || weight.joint >= parsed.joint_count) {
                errno = EILSEQ;
                return -1;
            }
            total += weight.weight;
        }
        if(total != PVR_CHUNK_SKIN_WEIGHT_SUM) {
            errno = EILSEQ;
            return -1;
        }
        previous_vertex = span.vertex_index;
        next_weight += span.weight_count;
    }
    if(next_weight != parsed.weight_count) {
        errno = EILSEQ;
        return -1;
    }

    *view = parsed;
    return 0;
}

int pvr_chunk_skin_general_section_span_get(
    const pvr_chunk_skin_general_section_view_t *view, size_t index,
    pvr_chunk_skin_span_t *span) {
    pvr_chunk_skin_general_section_view_t checked;

    if(!view || !span || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_general_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.span_count) {
        errno = ENOENT;
        return -1;
    }
    decode_span((const uint8_t *)checked.spans +
                    index * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES,
                span);
    return 0;
}

int pvr_chunk_skin_general_section_weight_get(
    const pvr_chunk_skin_general_section_view_t *view, size_t index,
    pvr_chunk_skin_weight_t *weight) {
    pvr_chunk_skin_general_section_view_t checked;

    if(!view || !weight || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_general_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.weight_count) {
        errno = ENOENT;
        return -1;
    }
    decode_weight((const uint8_t *)checked.weights +
                      index * PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES,
                  weight);
    return 0;
}

int pvr_chunk_skin_general_section_materialize(
    const pvr_chunk_skin_general_section_view_t *view,
    pvr_chunk_skin_span_t *spans, size_t span_capacity,
    pvr_chunk_skin_weight_t *weights, size_t weight_capacity,
    pvr_chunk_skin_general_t *skin) {
    pvr_chunk_skin_general_section_view_t checked;
    pvr_chunk_skin_general_t materialized;
    size_t span_bytes;
    size_t weight_bytes;
    size_t index;

    if(!view || !skin || !view->data || !spans || !weights ||
       ((uintptr_t)skin &
        (_Alignof(pvr_chunk_skin_general_t) - 1u)) ||
       ((uintptr_t)spans & (_Alignof(pvr_chunk_skin_span_t) - 1u)) ||
       ((uintptr_t)weights &
        (_Alignof(pvr_chunk_skin_weight_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_general_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(span_capacity < checked.span_count ||
       weight_capacity < checked.weight_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.span_count > SIZE_MAX / sizeof(*spans) ||
       checked.weight_count > SIZE_MAX / sizeof(*weights)) {
        errno = EOVERFLOW;
        return -1;
    }
    span_bytes = checked.span_count * sizeof(*spans);
    weight_bytes = checked.weight_count * sizeof(*weights);
    if(ranges_overlap(spans, span_bytes, weights, weight_bytes) ||
       ranges_overlap(spans, span_bytes, checked.data, checked.size) ||
       ranges_overlap(weights, weight_bytes, checked.data, checked.size) ||
       ranges_overlap(skin, sizeof(*skin), checked.data, checked.size) ||
       ranges_overlap(skin, sizeof(*skin), spans, span_bytes) ||
       ranges_overlap(skin, sizeof(*skin), weights, weight_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(index = 0; index < checked.span_count; ++index)
        decode_span((const uint8_t *)checked.spans +
                        index * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES,
                    spans + index);
    for(index = 0; index < checked.weight_count; ++index)
        decode_weight((const uint8_t *)checked.weights +
                          index * PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES,
                      weights + index);

    materialized.spans = spans;
    materialized.span_count = checked.span_count;
    materialized.weights = weights;
    materialized.weight_count = checked.weight_count;
    materialized.joint_count = checked.joint_count;
    *skin = materialized;
    return 0;
}
