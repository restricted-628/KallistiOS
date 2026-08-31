/* KallistiOS ##version##

   Host-side compact volume-section tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_volume_asset.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 13),
    UINT32_C(0x00040000),
    0, 0, 0,
    UINT32_C(0x3f800000), 0, 0,
    0, UINT32_C(0x3f800000), 0,
    UINT32_C(0x3f800000), UINT32_C(0x3f800000), 0,
    UINT32_C(0x000000ff)
};

static const uint16_t volumes[] = {
    PVR_CHUNK_VOLUME_TRIANGLES, UINT16_C(9), UINT16_C(0x4002),
    0, 1, 2, UINT16_C(0x00aa),
    0, 2, 1, UINT16_C(0x00bb),
    PVR_CHUNK_VOLUME_QUADS, UINT16_C(5), UINT16_C(1),
    0, 1, 2, 3,
    PVR_CHUNK_VOLUME_STRIPS, UINT16_C(8), UINT16_C(0x4001),
    UINT16_C(4), 0, 1, 2, UINT16_C(0x00cc), 3, UINT16_C(0x00dd),
    UINT16_C(0x00ff)
};

static const uint16_t no_volumes[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), 0, 1, 2,
    UINT16_C(0x00ff)
};

static const uint32_t other_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 13),
    UINT32_C(0x0004000a),
    0, 0, 0,
    UINT32_C(0x3f800000), 0, 0,
    0, UINT32_C(0x3f800000), 0,
    UINT32_C(0x3f800000), UINT32_C(0x3f800000), 0,
    UINT32_C(0x000000ff)
};

static const uint16_t other_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), 10, 11, 12,
    UINT16_C(0x00ff)
};

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

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void refresh_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 32, crc32_bytes(
        bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES,
        size - PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
}

static pvr_chunk_model_view_t open_model(const uint32_t *vertex_words,
                                         size_t vertex_word_count,
                                         const uint16_t *polygon_words,
                                         size_t polygon_word_count) {
    pvr_chunk_model_t model = {
        vertex_words, vertex_word_count,
        polygon_words, polygon_word_count,
        { 0.5f, 0.5f, 0.0f }, 1.0f
    };
    pvr_chunk_model_view_t view;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    return view;
}

int main(void) {
    pvr_chunk_model_view_t model = open_model(
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        volumes, sizeof(volumes) / sizeof(volumes[0]));
    pvr_chunk_model_view_t unrelated = open_model(
        other_vertices, sizeof(other_vertices) / sizeof(other_vertices[0]),
        other_polygons, sizeof(other_polygons) / sizeof(other_polygons[0]));
    pvr_chunk_model_view_t ordinary = open_model(
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        no_volumes, sizeof(no_volumes) / sizeof(no_volumes[0]));
    pvr_chunk_volume_section_view_t view;
    pvr_chunk_volume_section_iterator_t iterator;
    pvr_chunk_volume_triangle_t triangle;
    pvr_chunk_record_t record;
    pvr_chunk_volume_iterator_t record_iterator;
    uint8_t *serialized = NULL;
    uint8_t *empty = (uint8_t *)(uintptr_t)1;
    uint8_t *corrupt;
    size_t serialized_bytes = 0;
    size_t empty_bytes = SIZE_MAX;
    size_t triangles = 0;
    size_t final_records = 0;
    int rv;

    assert(pvr_scene_ir_serialize_volumes(
        &model, &serialized, &serialized_bytes) == 0);
    assert(serialized && serialized_bytes ==
           PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES +
           3 * PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES +
           28 * sizeof(uint16_t));
    assert(pvr_chunk_volume_section_open(
        serialized, serialized_bytes, &view) == 0);
    assert(view.record_count == 3 && view.word_count == 28 &&
           view.triangle_count == 6);
    assert(pvr_chunk_volume_section_validate_model(&view, &model) == 0);

    assert(pvr_chunk_volume_section_record_get(&view, 1, &record) == 0);
    assert(record.type == PVR_CHUNK_VOLUME_QUADS &&
           pvr_chunk_volume_iterator_init(&record_iterator, &record) == 0 &&
           pvr_chunk_volume_iterator_next(
               &record_iterator, &triangle) == 1 &&
           triangle.index[0] == 0 && triangle.index[1] == 1 &&
           triangle.index[2] == 2);

    assert(pvr_chunk_volume_section_iterator_init(&iterator, &view) == 0);
    while((rv = pvr_chunk_volume_section_iterator_next(
               &iterator, &triangle)) > 0) {
        ++triangles;
        final_records += triangle.final_in_record != 0;
        assert(triangle.user_word_count <= 1);
    }
    assert(rv == 0 && triangles == 6 && final_records == 3);
    memset(&triangle, 0x5a, sizeof(triangle));
    assert(pvr_chunk_volume_section_iterator_next(
        &iterator, &triangle) == 0);
    assert(triangle.user_words == NULL && triangle.index[0] == 0);

    errno = 0;
    assert(pvr_chunk_volume_section_validate_model(
        &view, &unrelated) == -1 && errno == EILSEQ);
    assert(pvr_scene_ir_serialize_volumes(
        &ordinary, &empty, &empty_bytes) == 0);
    assert(empty == NULL && empty_bytes == 0);

    corrupt = malloc(serialized_bytes);
    assert(corrupt);
    memcpy(corrupt, serialized, serialized_bytes);
    corrupt[serialized_bytes - 1] ^= UINT8_C(0x01);
    errno = 0;
    assert(pvr_chunk_volume_section_open(
        corrupt, serialized_bytes, &view) == -1 && errno == EILSEQ);

    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES +
                   PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES,
               UINT32_C(0));
    refresh_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_volume_section_open(
        corrupt, serialized_bytes, &view) == -1 && errno == EILSEQ);

    errno = 0;
    assert(pvr_chunk_volume_section_open(
        serialized + 1, serialized_bytes - 1, &view) == -1 &&
        errno == EINVAL);

    free(corrupt);
    free(serialized);
    puts("pvr chunk volume section tests passed");
    return 0;
}
