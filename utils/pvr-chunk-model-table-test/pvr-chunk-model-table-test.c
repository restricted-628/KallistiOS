/* KallistiOS ##version##

   Host-side compact model-table tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model_table.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERTEX_HEADER(type, size) \
    ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t vertices0[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint32_t vertices1[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xc0000000), UINT32_C(0xc0000000), UINT32_C(0),
    UINT32_C(0x40000000), UINT32_C(0xc0000000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x40000000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
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

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    write_le32(bytes, word);
}

static size_t align32(size_t value) {
    return (value + 31u) & ~(size_t)31u;
}

static void write_section(uint8_t *descriptor, uint32_t type,
                          size_t offset, const void *data, size_t bytes,
                          uint16_t alignment) {
    write_le32(descriptor, type);
    write_le32(descriptor + 8, (uint32_t)offset);
    write_le32(descriptor + 12, (uint32_t)bytes);
    write_le32(descriptor + 16, (uint32_t)bytes);
    write_le32(descriptor + 20, crc32_bytes(data, bytes));
    write_le16(descriptor + 28, PVR_CHUNK_ASSET_CODEC_RAW);
    write_le16(descriptor + 30, alignment);
}

static void init_record(pvr_chunk_model_table_record_t *record,
                        size_t ordinal, float center, float radius) {
    memset(record, 0, sizeof(*record));
    record->vertex_ordinal = ordinal;
    record->polygon_ordinal = ordinal;
    record->resource_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->volume_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->skin4_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->skin_general_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->skeleton_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->morph_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->cooked_cache_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
    record->center[0] = center;
    record->radius = radius;
}

static void refresh_table_checksums(uint8_t *table, size_t table_bytes) {
    assert(table_bytes >= PVR_CHUNK_MODEL_TABLE_HEADER_BYTES);
    write_le32(table + 20, crc32_bytes(
        table + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES,
        table_bytes - PVR_CHUNK_MODEL_TABLE_HEADER_BYTES));
    write_le32(table + 28, crc32_bytes(table, 28));
}

static size_t build_asset(uint8_t *asset, size_t capacity,
                          const void *table, size_t table_bytes) {
    const size_t section_count = 5;
    const size_t directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    size_t vertex0_offset = align32(
        PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + directory_bytes);
    size_t polygon0_offset = align32(vertex0_offset + sizeof(vertices0));
    size_t vertex1_offset = align32(polygon0_offset + sizeof(polygons));
    size_t polygon1_offset = align32(vertex1_offset + sizeof(vertices1));
    size_t table_offset = align32(polygon1_offset + sizeof(polygons));
    size_t file_bytes = table_offset + table_bytes;
    uint8_t *directory = asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;

    assert(file_bytes <= capacity);
    memset(asset, 0, capacity);
    memcpy(asset + vertex0_offset, vertices0, sizeof(vertices0));
    memcpy(asset + polygon0_offset, polygons, sizeof(polygons));
    memcpy(asset + vertex1_offset, vertices1, sizeof(vertices1));
    memcpy(asset + polygon1_offset, polygons, sizeof(polygons));
    memcpy(asset + table_offset, table, table_bytes);

    write_section(directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
                  vertex0_offset, vertices0, sizeof(vertices0), 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon0_offset, polygons, sizeof(polygons), 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 2u,
                  PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
                  vertex1_offset, vertices1, sizeof(vertices1), 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon1_offset, polygons, sizeof(polygons), 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 4u,
                  PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
                  table_offset, table, table_bytes, 4);

    write_le32(asset, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 28, 10.0f);
    write_le32(asset + 32, (uint32_t)section_count);
    write_le32(asset + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 40, (uint32_t)directory_bytes);
    write_le32(asset + 44, crc32_bytes(directory, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    return file_bytes;
}

static size_t build_shared_asset(uint8_t *asset, size_t capacity,
                                 const void *table, size_t table_bytes) {
    const size_t section_count = 4;
    const size_t directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    size_t vertex_offset = align32(
        PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + directory_bytes);
    size_t polygon0_offset = align32(vertex_offset + sizeof(vertices0));
    size_t polygon1_offset = align32(polygon0_offset + sizeof(polygons));
    size_t table_offset = align32(polygon1_offset + sizeof(polygons));
    size_t file_bytes = table_offset + table_bytes;
    uint8_t *directory = asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;

    assert(file_bytes <= capacity);
    memset(asset, 0, capacity);
    memcpy(asset + vertex_offset, vertices0, sizeof(vertices0));
    memcpy(asset + polygon0_offset, polygons, sizeof(polygons));
    memcpy(asset + polygon1_offset, polygons, sizeof(polygons));
    memcpy(asset + table_offset, table, table_bytes);

    write_section(directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
                  vertex_offset, vertices0, sizeof(vertices0), 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon0_offset, polygons, sizeof(polygons), 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 2u,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon1_offset, polygons, sizeof(polygons), 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
                  PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
                  table_offset, table, table_bytes, 4);

    write_le32(asset, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 28, 10.0f);
    write_le32(asset + 32, (uint32_t)section_count);
    write_le32(asset + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 40, (uint32_t)directory_bytes);
    write_le32(asset + 44, crc32_bytes(directory, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    return file_bytes;
}

static void test_model_table(void) {
    pvr_chunk_model_table_record_t source[2];
    pvr_chunk_model_table_record_t decoded;
    pvr_chunk_model_table_view_t table_view;
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    pvr_chunk_asset_section_t section;
    alignas(32) uint8_t asset[4096];
    uint8_t *table = NULL;
    size_t table_bytes = 0;
    size_t asset_bytes;

    init_record(&source[0], 0, 1.0f, 1.5f);
    init_record(&source[1], 1, 2.0f, 2.5f);
    source[1].vertex_ordinal = 0;
    assert(pvr_scene_ir_serialize_model_table(
        source, 2, &table, &table_bytes) == 0);
    assert(table_bytes == PVR_CHUNK_MODEL_TABLE_HEADER_BYTES +
                          2 * PVR_CHUNK_MODEL_TABLE_RECORD_BYTES);
    assert(pvr_chunk_model_table_open(
        table, table_bytes, &table_view) == 0);
    assert(table_view.version == PVR_CHUNK_MODEL_TABLE_VERSION &&
           table_view.model_count == 2);
    assert(pvr_chunk_model_table_record_get(
        &table_view, 1, &decoded) == 0);
    assert(decoded.vertex_ordinal == 0 && decoded.polygon_ordinal == 1 &&
           decoded.center[0] == 2.0f &&
           decoded.radius == 2.5f);

    memset(&decoded, 0x5a, sizeof(decoded));
    errno = 0;
    assert(pvr_chunk_model_table_record_get(
        &table_view, 2, &decoded) == -1);
    assert(errno == ENOENT && decoded.radius == 0.0f);

    asset_bytes = build_asset(asset, sizeof(asset), table, table_bytes);
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_asset_section_find(
        &asset_view, PVR_CHUNK_ASSET_SECTION_MODEL_TABLE, 0,
        &section) == 0);
    assert(pvr_chunk_model_table_open(
        section.stored_data, section.decoded_bytes, &table_view) == 0);
    assert(pvr_chunk_model_table_validate_asset(
        &table_view, &asset_view) == 0);
    assert(pvr_chunk_model_table_workspace_query(
        &table_view, &asset_view, 1, &requirements) == 0);
    assert(!requirements.bytes);
    assert(pvr_chunk_model_table_load(
        &table_view, &asset_view, 1, NULL, NULL, NULL, 0, &model) == 0);
    assert(pvr_chunk_asset_section_find(
        &asset_view, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, 0,
        &section) == 0);
    assert(model.model.vertex_words == section.stored_data);
    assert(model.model.center[0] == 2.0f && model.model.radius == 2.5f);

    free(table);
}

static void test_shared_vertex_stream(void) {
    pvr_chunk_model_table_record_t records[2];
    pvr_chunk_model_table_view_t table_view;
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    pvr_chunk_asset_section_t vertex;
    pvr_chunk_asset_section_t polygon;
    alignas(32) uint8_t asset[4096];
    uint8_t *table = NULL;
    size_t table_bytes = 0;
    size_t asset_bytes;

    init_record(&records[0], 0, 0.0f, 1.0f);
    init_record(&records[1], 1, 0.0f, 1.0f);
    records[1].vertex_ordinal = 0;
    assert(pvr_scene_ir_serialize_model_table(
        records, 2, &table, &table_bytes) == 0);
    asset_bytes = build_shared_asset(
        asset, sizeof(asset), table, table_bytes);
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(asset_view.model_count == 1);
    assert(pvr_chunk_model_table_open(
        table, table_bytes, &table_view) == 0);
    assert(pvr_chunk_model_table_validate_asset(
        &table_view, &asset_view) == 0);
    assert(pvr_chunk_model_table_workspace_query(
        &table_view, &asset_view, 1, &requirements) == 0);
    assert(requirements.bytes == 0);
    assert(pvr_chunk_model_table_load(
        &table_view, &asset_view, 1, NULL, NULL, NULL, 0, &model) == 0);
    assert(pvr_chunk_asset_section_find(
        &asset_view, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, 0,
        &vertex) == 0);
    assert(pvr_chunk_asset_section_find(
        &asset_view, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, 1,
        &polygon) == 0);
    assert(model.model.vertex_words == vertex.stored_data &&
           model.model.polygon_words == polygon.stored_data);
    errno = 0;
    assert(pvr_chunk_asset_model_load(
        &asset_view, 1, NULL, NULL, NULL, 0, &model) == -1);
    assert(errno == ENOENT);

    free(table);
}

static void test_model_table_rejection(void) {
    pvr_chunk_model_table_record_t records[2];
    pvr_chunk_model_table_view_t table_view;
    pvr_chunk_asset_view_t asset_view;
    alignas(32) uint8_t asset[4096];
    uint8_t *table = NULL;
    size_t table_bytes = 0;
    size_t asset_bytes;

    init_record(&records[0], 0, 0.0f, 1.0f);
    init_record(&records[1], 1, 0.0f, 1.0f);
    records[1].resource_ordinal = 0;
    assert(pvr_scene_ir_serialize_model_table(
        records, 2, &table, &table_bytes) == 0);
    asset_bytes = build_asset(asset, sizeof(asset), table, table_bytes);
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_model_table_open(
        table, table_bytes, &table_view) == 0);
    errno = 0;
    assert(pvr_chunk_model_table_validate_asset(
        &table_view, &asset_view) == -1);
    assert(errno == EILSEQ);
    free(table);

    init_record(&records[0], 0, 0.0f, 1.0f);
    assert(pvr_scene_ir_serialize_model_table(
        records, 1, &table, &table_bytes) == 0);
    asset_bytes = build_asset(asset, sizeof(asset), table, table_bytes);
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_model_table_open(
        table, table_bytes, &table_view) == 0);
    assert(pvr_chunk_model_table_validate_asset(
        &table_view, &asset_view) == 0);

    /* Obsolete development encodings must fail rather than be guessed. */
    write_le16(table + 4, 1u);
    refresh_table_checksums(table, table_bytes);
    errno = 0;
    assert(pvr_chunk_model_table_open(
        table, table_bytes, &table_view) == -1);
    assert(errno == EILSEQ);
    free(table);
}

static void test_corrupt_model_table(void) {
    pvr_chunk_model_table_record_t record;
    pvr_chunk_model_table_view_t view;
    uint8_t *table = NULL;
    size_t table_bytes = 0;

    init_record(&record, 0, 0.0f, 1.0f);
    assert(pvr_scene_ir_serialize_model_table(
        &record, 1, &table, &table_bytes) == 0);

    table[PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + 40] ^= 1u;
    memset(&view, 0x5a, sizeof(view));
    errno = 0;
    assert(pvr_chunk_model_table_open(table, table_bytes, &view) == -1);
    assert(errno == EILSEQ && view.data == NULL);
    table[PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + 40] ^= 1u;

    write_le32(table + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + 36, 1);
    refresh_table_checksums(table, table_bytes);
    errno = 0;
    assert(pvr_chunk_model_table_open(table, table_bytes, &view) == -1);
    assert(errno == EILSEQ);

    write_le32(table + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + 36, 0);
    write_le32(table + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + 40,
               UINT32_C(0x7fc00000));
    refresh_table_checksums(table, table_bytes);
    errno = 0;
    assert(pvr_chunk_model_table_open(table, table_bytes, &view) == -1);
    assert(errno == EILSEQ);
    free(table);
}

int main(void) {
    test_model_table();
    test_shared_vertex_stream();
    test_model_table_rejection();
    test_corrupt_model_table();
    puts("pvr-chunk-model-table-test: PASS");
    return 0;
}
