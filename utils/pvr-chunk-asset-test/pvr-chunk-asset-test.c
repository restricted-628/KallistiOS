/* KallistiOS ##version##

   Host-side compact-model asset tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_asset.h>
#include <kos/pvr_chunk_asset_lz4.h>

#include <lz4frame.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0xffff), UINT16_C(0xffff),
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
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
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

static size_t build_asset(uint8_t *asset, size_t capacity, int compressed) {
    const size_t vertex_bytes = sizeof(vertices);
    const size_t polygon_bytes = sizeof(polygons);
    size_t vertex_offset = PVR_CHUNK_ASSET_HEADER_BYTES;
    size_t vertex_stored = vertex_bytes;
    size_t polygon_offset;
    size_t file_bytes;
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;

    memset(asset, 0, capacity);
    if(compressed) {
        size_t bound;
        size_t result;

        preferences.frameInfo.blockSizeID = LZ4F_max64KB;
        preferences.frameInfo.blockMode = LZ4F_blockIndependent;
        preferences.frameInfo.contentChecksumFlag =
            LZ4F_contentChecksumEnabled;
        preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
        preferences.frameInfo.contentSize = vertex_bytes;
        bound = LZ4F_compressFrameBound(vertex_bytes, &preferences);
        assert(vertex_offset + bound <= capacity);
        result = LZ4F_compressFrame(asset + vertex_offset, bound, vertices,
                                    vertex_bytes, &preferences);
        assert(!LZ4F_isError(result));
        vertex_stored = result;
    }
    else {
        memcpy(asset + vertex_offset, vertices, vertex_bytes);
    }

    polygon_offset = align32(vertex_offset + vertex_stored);
    file_bytes = polygon_offset + polygon_bytes;
    assert(file_bytes <= capacity);
    memcpy(asset + polygon_offset, polygons, polygon_bytes);

    write_le32(asset, PVR_CHUNK_ASSET_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 16, 0.0f);
    write_float(asset + 20, 0.0f);
    write_float(asset + 24, 0.0f);
    write_float(asset + 28, 1.5f);

    write_le32(asset + 32, (uint32_t)vertex_offset);
    write_le32(asset + 36, (uint32_t)vertex_stored);
    write_le32(asset + 40, (uint32_t)vertex_bytes);
    write_le32(asset + 44, crc32_bytes(vertices, vertex_bytes));
    write_le16(asset + 48, compressed ? PVR_CHUNK_ASSET_CODEC_LZ4_FRAME :
                                        PVR_CHUNK_ASSET_CODEC_RAW);

    write_le32(asset + 56, (uint32_t)polygon_offset);
    write_le32(asset + 60, (uint32_t)polygon_bytes);
    write_le32(asset + 64, (uint32_t)polygon_bytes);
    write_le32(asset + 68, crc32_bytes(polygons, polygon_bytes));
    write_le16(asset + 72, PVR_CHUNK_ASSET_CODEC_RAW);
    write_le32(asset + 80, crc32_bytes(asset, 80));
    return file_bytes;
}

static void test_raw_borrow(void) {
    alignas(32) uint8_t asset[2048];
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    size_t bytes = build_asset(asset, sizeof(asset), 0);

    assert(pvr_chunk_asset_open(asset, bytes, &asset_view) == 0);
    assert(pvr_chunk_asset_workspace_query(&asset_view, &requirements) == 0);
    assert(requirements.bytes == 0);
    assert(!requirements.copies_vertex && !requirements.copies_polygon);
    assert(pvr_chunk_asset_load(&asset_view, NULL, NULL, NULL, 0,
                                &model) == 0);
    assert(model.info.vertex_entries == 3 && model.info.triangles == 1);
    assert(model.model.vertex_words == asset_view.vertex.stored_data);
    assert(model.model.polygon_words == asset_view.polygon.stored_data);
}

static void test_lz4_vertex(void) {
    alignas(32) uint8_t asset[2048];
    alignas(32) uint8_t workspace[2048];
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    size_t bytes = build_asset(asset, sizeof(asset), 1);

    assert(pvr_chunk_asset_open(asset, bytes, &asset_view) == 0);
    assert(pvr_chunk_asset_workspace_query(&asset_view, &requirements) == 0);
    assert(requirements.copies_vertex && !requirements.copies_polygon);
    assert(requirements.bytes == sizeof(vertices));
    assert(pvr_chunk_asset_load(&asset_view, pvr_chunk_asset_lz4_decode,
                                NULL, workspace, sizeof(workspace),
                                &model) == 0);
    assert(model.info.vertex_entries == 3 && model.info.triangles == 1);
    assert(memcmp(model.model.vertex_words, vertices, sizeof(vertices)) == 0);

    errno = 0;
    assert(pvr_chunk_asset_load(&asset_view, NULL, NULL, workspace,
                                sizeof(workspace), &model) == -1);
    assert(errno == ENOTSUP);

    asset[asset_view.vertex.stored_bytes / 2u +
          PVR_CHUNK_ASSET_HEADER_BYTES] ^= UINT8_C(0x20);
    errno = 0;
    assert(pvr_chunk_asset_load(&asset_view, pvr_chunk_asset_lz4_decode,
                                NULL, workspace, sizeof(workspace),
                                &model) == -1);
    assert(errno == EILSEQ);
}

static void test_unaligned_container(void) {
    alignas(32) uint8_t storage[2080];
    alignas(32) uint8_t workspace[2048];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    size_t bytes = build_asset(storage, sizeof(storage) - 1u, 0);

    memmove(storage + 1, storage, bytes);
    assert(pvr_chunk_asset_open(storage + 1, bytes, &view) == 0);
    assert(pvr_chunk_asset_workspace_query(&view, &requirements) == 0);
    assert(requirements.copies_vertex && requirements.copies_polygon);
    assert(requirements.polygon_offset == align32(sizeof(vertices)));
    assert(pvr_chunk_asset_load(&view, NULL, NULL, workspace,
                                sizeof(workspace), &model) == 0);
    assert(model.info.triangles == 1);
}

static void test_corrupt_headers(void) {
    alignas(32) uint8_t asset[2048];
    pvr_chunk_asset_view_t view;
    size_t bytes = build_asset(asset, sizeof(asset), 0);

    asset[16] ^= 1u;
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_asset(asset, sizeof(asset), 0);
    write_le32(asset + 56, PVR_CHUNK_ASSET_HEADER_BYTES);
    write_le32(asset + 80, crc32_bytes(asset, 80));
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_asset(asset, sizeof(asset), 0);
    write_le16(asset + 48, UINT16_C(99));
    write_le32(asset + 80, crc32_bytes(asset, 80));
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == ENOTSUP);
}

int main(void) {
    test_raw_borrow();
    test_lz4_vertex();
    test_unaligned_container();
    test_corrupt_headers();
    puts("pvr chunk asset tests passed");
    return 0;
}
