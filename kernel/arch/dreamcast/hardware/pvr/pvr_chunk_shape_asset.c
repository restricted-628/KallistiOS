/* KallistiOS ##version##

   dc/pvr/pvr_chunk_shape_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_shape_asset.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 32,
    HEADER_RESERVED0_OFFSET = 36,
    HEADER_RESERVED1_OFFSET = 40,
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

static float read_float(const uint8_t *bytes) {
    uint32_t word = read_le32(bytes);
    float value;

    memcpy(&value, &word, sizeof(value));
    return value;
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

static void decode_target(const uint8_t *record,
                          pvr_chunk_shape_section_target_t *target) {
    target->first_delta = read_le32(record);
    target->delta_count = read_le32(record + 4);
}

static void decode_delta(const uint8_t *record,
                         pvr_chunk_shape_delta_t *delta) {
    delta->vertex_index = read_le16(record);
    delta->reserved = read_le16(record + 2);
    delta->delta.position.x = read_float(record + 4);
    delta->delta.position.y = read_float(record + 8);
    delta->delta.position.z = read_float(record + 12);
    delta->delta.position.w = 0.0f;
    delta->delta.normal.x = read_float(record + 16);
    delta->delta.normal.y = read_float(record + 20);
    delta->delta.normal.z = read_float(record + 24);
    delta->delta.normal.w = 0.0f;
}

static int finite_delta(const pvr_chunk_shape_delta_t *delta) {
    return !delta->reserved && isfinite(delta->delta.position.x) &&
           isfinite(delta->delta.position.y) &&
           isfinite(delta->delta.position.z) &&
           isfinite(delta->delta.normal.x) &&
           isfinite(delta->delta.normal.y) &&
           isfinite(delta->delta.normal.z);
}

int pvr_chunk_shape_section_open(
    const void *data, size_t size, pvr_chunk_shape_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_shape_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t target_count;
    uint32_t delta_count;
    uint32_t target_bytes;
    uint32_t delta_bytes;
    uint64_t encoded_payload_bytes;
    size_t payload_bytes;
    size_t next_delta = 0;
    size_t target_index;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_SHAPE_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_SHAPE_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES ||
       read_le16(bytes + 20) != PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES ||
       read_le16(bytes + 22) != PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES ||
       read_le32(bytes + HEADER_RESERVED0_OFFSET) ||
       read_le32(bytes + HEADER_RESERVED1_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    target_count = read_le32(bytes + 12);
    delta_count = read_le32(bytes + 16);
    target_bytes = read_le32(bytes + 24);
    delta_bytes = read_le32(bytes + 28);
    if(file_bytes != size || !target_count || !delta_count ||
       target_count > UINT32_MAX / PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES ||
       delta_count > UINT32_MAX / PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES ||
       target_bytes != target_count * PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES ||
       delta_bytes != delta_count * PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    encoded_payload_bytes = (uint64_t)target_bytes + delta_bytes;
    if(encoded_payload_bytes >
       UINT32_MAX - PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    payload_bytes = (size_t)encoded_payload_bytes;
    if(payload_bytes != file_bytes - PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES, payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.targets = bytes + PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES;
    parsed.target_count = target_count;
    parsed.deltas = bytes + PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES +
                    target_bytes;
    parsed.delta_count = delta_count;
    parsed.version = PVR_CHUNK_SHAPE_SECTION_VERSION;

    for(target_index = 0; target_index < parsed.target_count;
        ++target_index) {
        pvr_chunk_shape_section_target_t target;
        uint16_t previous_vertex = 0;
        size_t delta_index;

        decode_target((const uint8_t *)parsed.targets + target_index *
                          PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES,
                      &target);
        if(!target.delta_count || target.first_delta != next_delta ||
           target.delta_count > parsed.delta_count - next_delta) {
            errno = EILSEQ;
            return -1;
        }
        for(delta_index = 0; delta_index < target.delta_count;
            ++delta_index) {
            pvr_chunk_shape_delta_t delta;

            decode_delta((const uint8_t *)parsed.deltas +
                             (next_delta + delta_index) *
                                 PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES,
                         &delta);
            if(!finite_delta(&delta) ||
               (delta_index && delta.vertex_index <= previous_vertex)) {
                errno = EILSEQ;
                return -1;
            }
            previous_vertex = delta.vertex_index;
        }
        next_delta += target.delta_count;
    }
    if(next_delta != parsed.delta_count) {
        errno = EILSEQ;
        return -1;
    }

    *view = parsed;
    return 0;
}

int pvr_chunk_shape_section_target_get(
    const pvr_chunk_shape_section_view_t *view, size_t index,
    pvr_chunk_shape_section_target_t *target) {
    pvr_chunk_shape_section_view_t checked;

    if(!view || !target || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_shape_section_open(view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.target_count) {
        errno = ENOENT;
        return -1;
    }
    decode_target((const uint8_t *)checked.targets +
                      index * PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES,
                  target);
    return 0;
}

int pvr_chunk_shape_section_delta_get(
    const pvr_chunk_shape_section_view_t *view, size_t index,
    pvr_chunk_shape_delta_t *delta) {
    pvr_chunk_shape_section_view_t checked;

    if(!view || !delta || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_shape_section_open(view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.delta_count) {
        errno = ENOENT;
        return -1;
    }
    decode_delta((const uint8_t *)checked.deltas +
                     index * PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES,
                 delta);
    return 0;
}

int pvr_chunk_shape_section_materialize(
    const pvr_chunk_shape_section_view_t *view,
    pvr_chunk_shape_target_t *targets, size_t target_capacity,
    pvr_chunk_shape_delta_t *deltas, size_t delta_capacity,
    pvr_chunk_shape_set_t *shapes) {
    pvr_chunk_shape_section_view_t checked;
    pvr_chunk_shape_set_t materialized;
    size_t target_bytes;
    size_t delta_bytes;
    size_t target_index;
    size_t delta_index;

    if(!view || !shapes || !view->data || !targets || !deltas ||
       ((uintptr_t)shapes & (_Alignof(pvr_chunk_shape_set_t) - 1u)) ||
       ((uintptr_t)targets & (_Alignof(pvr_chunk_shape_target_t) - 1u)) ||
       ((uintptr_t)deltas & (_Alignof(pvr_chunk_shape_delta_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_shape_section_open(view->data, view->size, &checked) < 0)
        return -1;
    if(target_capacity < checked.target_count ||
       delta_capacity < checked.delta_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.target_count > SIZE_MAX / sizeof(*targets) ||
       checked.delta_count > SIZE_MAX / sizeof(*deltas)) {
        errno = EOVERFLOW;
        return -1;
    }
    target_bytes = checked.target_count * sizeof(*targets);
    delta_bytes = checked.delta_count * sizeof(*deltas);
    if(ranges_overlap(targets, target_bytes, deltas, delta_bytes) ||
       ranges_overlap(targets, target_bytes, checked.data, checked.size) ||
       ranges_overlap(deltas, delta_bytes, checked.data, checked.size) ||
       ranges_overlap(shapes, sizeof(*shapes), checked.data, checked.size) ||
       ranges_overlap(shapes, sizeof(*shapes), targets, target_bytes) ||
       ranges_overlap(shapes, sizeof(*shapes), deltas, delta_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(delta_index = 0; delta_index < checked.delta_count; ++delta_index)
        decode_delta((const uint8_t *)checked.deltas + delta_index *
                         PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES,
                     deltas + delta_index);
    for(target_index = 0; target_index < checked.target_count;
        ++target_index) {
        pvr_chunk_shape_section_target_t target;

        decode_target((const uint8_t *)checked.targets + target_index *
                          PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES,
                      &target);
        targets[target_index].deltas = deltas + target.first_delta;
        targets[target_index].delta_count = target.delta_count;
    }

    materialized.targets = targets;
    materialized.target_count = checked.target_count;
    *shapes = materialized;
    return 0;
}
