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

static const uint32_t vertices_second[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xc0000000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0x40000000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x40000000), UINT32_C(0),
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

static void write_pcm2_section(uint8_t *descriptor, uint32_t type,
                               uint32_t offset, const void *decoded,
                               size_t decoded_bytes, uint16_t alignment) {
    write_le32(descriptor, type);
    write_le32(descriptor + 8, offset);
    write_le32(descriptor + 12, (uint32_t)decoded_bytes);
    write_le32(descriptor + 16, (uint32_t)decoded_bytes);
    write_le32(descriptor + 20, crc32_bytes(decoded, decoded_bytes));
    write_le16(descriptor + 28, PVR_CHUNK_ASSET_CODEC_RAW);
    write_le16(descriptor + 30, alignment);
}

static size_t build_directory_asset(uint8_t *asset, size_t capacity) {
    static const uint8_t application0[] = { 1, 2, 3, 4 };
    static const uint8_t application1[] = { 5, 6, 7, 8, 9, 10, 11, 12 };
    const size_t section_count = 4;
    const size_t directory_offset = PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
    const size_t directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    size_t vertex_offset = align32(directory_offset + directory_bytes);
    size_t polygon_offset = align32(vertex_offset + sizeof(vertices));
    size_t application0_offset = align32(polygon_offset + sizeof(polygons));
    size_t application1_offset =
        align32(application0_offset + sizeof(application0));
    size_t file_bytes = application1_offset + sizeof(application1);
    uint8_t *directory;

    assert(file_bytes <= capacity);
    memset(asset, 0, capacity);
    memcpy(asset + vertex_offset, vertices, sizeof(vertices));
    memcpy(asset + polygon_offset, polygons, sizeof(polygons));
    memcpy(asset + application0_offset, application0, sizeof(application0));
    memcpy(asset + application1_offset, application1, sizeof(application1));

    directory = asset + directory_offset;
    write_pcm2_section(
        directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
        (uint32_t)vertex_offset, vertices, sizeof(vertices), 4);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
        (uint32_t)polygon_offset, polygons, sizeof(polygons), 2);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 2u,
        PVR_CHUNK_ASSET_SECTION_APPLICATION,
        (uint32_t)application0_offset, application0, sizeof(application0), 4);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
        PVR_CHUNK_ASSET_SECTION_APPLICATION,
        (uint32_t)application1_offset, application1, sizeof(application1), 1);

    write_le32(asset, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 16, 0.0f);
    write_float(asset + 20, 0.0f);
    write_float(asset + 24, 0.0f);
    write_float(asset + 28, 1.5f);
    write_le32(asset + 32, (uint32_t)section_count);
    write_le32(asset + 36, (uint32_t)directory_offset);
    write_le32(asset + 40, (uint32_t)directory_bytes);
    write_le32(asset + 44, crc32_bytes(directory, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    return file_bytes;
}

static size_t build_multi_model_asset(uint8_t *asset, size_t capacity) {
    const size_t section_count = 4;
    const size_t directory_offset = PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
    const size_t directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    size_t vertex0_offset = align32(directory_offset + directory_bytes);
    size_t polygon0_offset = align32(vertex0_offset + sizeof(vertices));
    size_t vertex1_offset = align32(polygon0_offset + sizeof(polygons));
    size_t polygon1_offset = align32(vertex1_offset +
                                     sizeof(vertices_second));
    size_t file_bytes = polygon1_offset + sizeof(polygons);
    uint8_t *directory;

    assert(file_bytes <= capacity);
    memset(asset, 0, capacity);
    memcpy(asset + vertex0_offset, vertices, sizeof(vertices));
    memcpy(asset + polygon0_offset, polygons, sizeof(polygons));
    memcpy(asset + vertex1_offset, vertices_second, sizeof(vertices_second));
    memcpy(asset + polygon1_offset, polygons, sizeof(polygons));

    directory = asset + directory_offset;
    write_pcm2_section(
        directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
        (uint32_t)vertex0_offset, vertices, sizeof(vertices), 4);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
        (uint32_t)polygon0_offset, polygons, sizeof(polygons), 2);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 2u,
        PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
        (uint32_t)vertex1_offset, vertices_second,
        sizeof(vertices_second), 4);
    write_pcm2_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
        PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
        (uint32_t)polygon1_offset, polygons, sizeof(polygons), 2);

    write_le32(asset, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 28, 3.0f);
    write_le32(asset + 32, (uint32_t)section_count);
    write_le32(asset + 36, (uint32_t)directory_offset);
    write_le32(asset + 40, (uint32_t)directory_bytes);
    write_le32(asset + 44, crc32_bytes(directory, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    return file_bytes;
}

static void refresh_directory_checksums(uint8_t *asset) {
    uint32_t directory_offset =
        (uint32_t)asset[36] | (uint32_t)asset[37] << 8 |
        (uint32_t)asset[38] << 16 | (uint32_t)asset[39] << 24;
    uint32_t directory_bytes =
        (uint32_t)asset[40] | (uint32_t)asset[41] << 8 |
        (uint32_t)asset[42] << 16 | (uint32_t)asset[43] << 24;

    write_le32(asset + 44,
               crc32_bytes(asset + directory_offset, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
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
    assert(asset_view.model_count == 1);
    assert(pvr_chunk_asset_workspace_query(&asset_view, &requirements) == 0);
    assert(requirements.bytes == 0);
    assert(!requirements.copies_vertex && !requirements.copies_polygon);
    assert(pvr_chunk_asset_load(&asset_view, NULL, NULL, NULL, 0,
                                &model) == 0);
    assert(model.info.vertex_entries == 3 && model.info.triangles == 1);
    assert(model.model.vertex_words == asset_view.vertex.stored_data);
    assert(model.model.polygon_words == asset_view.polygon.stored_data);
    {
        pvr_chunk_asset_section_t section;

        assert(pvr_chunk_asset_section_get(&asset_view, 0, &section) == 0);
        assert(section.type == PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM &&
               section.alignment == sizeof(uint32_t));
        assert(pvr_chunk_asset_section_find(
            &asset_view, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, 0,
            &section) == 0);
        assert(section.stored_data == asset_view.polygon.stored_data);
    }
}

static void test_directory_asset(void) {
    alignas(32) uint8_t asset[2048];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_asset_section_t section;
    pvr_chunk_asset_section_workspace_requirements_t section_requirements;
    pvr_chunk_model_view_t model;
    const void *decoded;
    size_t bytes = build_directory_asset(asset, sizeof(asset));

    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(view.version == PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    assert(view.model_count == 1);
    assert(view.header_bytes == PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    assert(view.section_count == 4 &&
           view.section_directory_bytes ==
               4 * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES);
    assert(view.vertex.type == PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM &&
           view.polygon.type == PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM);
    assert(pvr_chunk_asset_workspace_query(&view, &requirements) == 0);
    assert(requirements.bytes == 0);
    assert(pvr_chunk_asset_load(&view, NULL, NULL, NULL, 0, &model) == 0);
    assert(model.info.vertex_entries == 3 && model.info.triangles == 1);

    assert(pvr_chunk_asset_section_get(&view, 2, &section) == 0);
    assert(section.type == PVR_CHUNK_ASSET_SECTION_APPLICATION &&
           section.alignment == 4 && section.decoded_bytes == 4);
    assert(!memcmp(section.stored_data, "\x01\x02\x03\x04", 4));
    assert(pvr_chunk_asset_section_workspace_query(
        &view, 2, &section_requirements) == 0);
    assert(!section_requirements.copies && section_requirements.bytes == 0);
    assert(pvr_chunk_asset_section_load(
        &view, 2, NULL, NULL, NULL, 0, &decoded) == 0);
    assert(decoded == section.stored_data);
    ((uint8_t *)section.stored_data)[0] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_chunk_asset_section_load(
        &view, 2, NULL, NULL, NULL, 0, &decoded) == -1);
    assert(errno == EILSEQ && decoded == NULL);
    ((uint8_t *)section.stored_data)[0] ^= UINT8_C(0x80);
    assert(pvr_chunk_asset_section_find(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 1, &section) == 0);
    assert(section.decoded_bytes == 8 &&
           !memcmp(section.stored_data, "\x05\x06\x07\x08", 4));

    memset(&section, 0x5a, sizeof(section));
    errno = 0;
    assert(pvr_chunk_asset_section_get(&view, 4, &section) == -1);
    assert(errno == ENOENT && section.stored_data == NULL);
    errno = 0;
    assert(pvr_chunk_asset_section_find(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 2, &section) == -1);
    assert(errno == ENOENT && section.stored_data == NULL);
}

static void test_section_indices(void) {
    alignas(32) uint8_t asset[2048];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_section_workspace_requirements_t requirements;
    const void *decoded;
    size_t index;
    size_t bytes = build_directory_asset(asset, sizeof(asset));

    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 0, &index) == 0);
    assert(index == 2);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 1, &index) == 0);
    assert(index == 3);
    assert(pvr_chunk_asset_section_workspace_query(
        &view, index, &requirements) == 0 && requirements.bytes == 0);
    assert(pvr_chunk_asset_section_load(
        &view, index, NULL, NULL, NULL, 0, &decoded) == 0);
    assert(!memcmp(decoded, "\x05\x06\x07\x08\x09\x0a\x0b\x0c", 8));

    /* Adding an unrelated semantic section ahead of this one must not
       make callers mistake its ordinal within a type for a directory index. */
    write_le32(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES +
               2u * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
               PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS);
    write_le32(asset + 44, crc32_bytes(
        asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES,
        4u * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 0, &index) == 0);
    assert(index == 3);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 1, &index) == -1);
    assert(errno == ENOENT && index == SIZE_MAX);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, SIZE_MAX, &index) == -1);
    assert(errno == ENOENT && index == SIZE_MAX);
    assert(pvr_chunk_asset_section_find_index(
        &view, 0, 0, &index) == -1);
    assert(errno == EINVAL && index == SIZE_MAX);
    assert(pvr_chunk_asset_section_find_index(
        NULL, PVR_CHUNK_ASSET_SECTION_APPLICATION, 0, &index) == -1);
    assert(errno == EINVAL && index == SIZE_MAX);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 0, NULL) == -1);
    assert(errno == EINVAL);
    asset[0] ^= 1u;
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_APPLICATION, 0, &index) == -1);
    assert(errno == EILSEQ && index == SIZE_MAX);

    bytes = build_asset(asset, sizeof(asset), 0);
    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, 0, &index) == 0);
    assert(index == 0);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, 0, &index) == 0);
    assert(index == 1);
    assert(pvr_chunk_asset_section_find_index(
        &view, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, 1, &index) == -1);
    assert(errno == ENOENT && index == SIZE_MAX);
}

static void test_multi_model_asset(void) {
    alignas(32) uint8_t asset[2080];
    alignas(32) uint8_t workspace[2048];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_asset_section_t section;
    pvr_chunk_model_view_t first;
    pvr_chunk_model_view_t second;
    size_t bytes = build_multi_model_asset(asset, sizeof(asset));

    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(view.model_count == 2 && view.section_count == 4);
    assert(pvr_chunk_asset_model_workspace_query(
        &view, 1, &requirements) == 0);
    assert(!requirements.bytes && !requirements.copies_vertex &&
           !requirements.copies_polygon);
    assert(pvr_chunk_asset_load(
        &view, NULL, NULL, NULL, 0, &first) == 0);
    assert(pvr_chunk_asset_model_load(
        &view, 1, NULL, NULL, NULL, 0, &second) == 0);
    assert(first.model.vertex_words == view.vertex.stored_data);
    assert(second.model.vertex_words != first.model.vertex_words);
    assert(!memcmp(second.model.vertex_words, vertices_second,
                   sizeof(vertices_second)));
    assert(first.info.vertex_entries == 3 && second.info.vertex_entries == 3);
    assert(pvr_chunk_asset_section_find(
        &view, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, 1, &section) == 0);
    assert(section.stored_data == second.model.vertex_words);

    memset(&requirements, 0x5a, sizeof(requirements));
    errno = 0;
    assert(pvr_chunk_asset_model_workspace_query(
        &view, 2, &requirements) == -1);
    assert(errno == ENOENT && !requirements.bytes);
    memset(&second, 0x5a, sizeof(second));
    errno = 0;
    assert(pvr_chunk_asset_model_load(
        &view, 2, NULL, NULL, NULL, 0, &second) == -1);
    assert(errno == ENOENT && !second.model.vertex_words);

    memmove(asset + 1, asset, bytes);
    assert(pvr_chunk_asset_open(asset + 1, bytes, &view) == 0);
    assert(pvr_chunk_asset_model_workspace_query(
        &view, 1, &requirements) == 0);
    assert(requirements.copies_vertex && requirements.copies_polygon);
    assert(pvr_chunk_asset_model_load(
        &view, 1, NULL, NULL, workspace, sizeof(workspace), &second) == 0);
    assert(!memcmp(second.model.vertex_words, vertices_second,
                   sizeof(vertices_second)));

    bytes = build_multi_model_asset(asset, sizeof(asset));
    write_le32(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES +
               PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
               PVR_CHUNK_ASSET_SECTION_APPLICATION);
    refresh_directory_checksums(asset);
    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    assert(view.model_count == 1 && view.section_count == 4);
    assert(pvr_chunk_asset_pair_workspace_query(
        &view, 1, 0, &requirements) == 0);
    assert(!requirements.bytes);
    assert(pvr_chunk_asset_pair_load(
        &view, 1, 0, NULL, NULL, NULL, 0, &second) == 0);
    assert(!memcmp(second.model.vertex_words, vertices_second,
                   sizeof(vertices_second)));
    errno = 0;
    assert(pvr_chunk_asset_model_load(
        &view, 1, NULL, NULL, NULL, 0, &second) == -1);
    assert(errno == ENOENT);
}

static void test_unaligned_directory_asset(void) {
    alignas(32) uint8_t storage[2080];
    alignas(32) uint8_t workspace[32];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_section_workspace_requirements_t requirements;
    const void *decoded;
    size_t bytes = build_directory_asset(storage, sizeof(storage) - 1u);

    memmove(storage + 1, storage, bytes);
    assert(pvr_chunk_asset_open(storage + 1, bytes, &view) == 0);
    assert(pvr_chunk_asset_section_workspace_query(
        &view, 2, &requirements) == 0);
    assert(requirements.copies && requirements.bytes == 4 &&
           requirements.alignment == PVR_CHUNK_ASSET_ALIGNMENT);
    assert(pvr_chunk_asset_section_load(
        &view, 2, NULL, NULL, workspace, sizeof(workspace), &decoded) == 0);
    assert(decoded == workspace &&
           !memcmp(decoded, "\x01\x02\x03\x04", 4));
    errno = 0;
    assert(pvr_chunk_asset_section_load(
        &view, 2, NULL, NULL, storage, sizeof(storage), &decoded) == -1);
    assert(errno == EINVAL && decoded == NULL);
}

static void test_corrupt_directory(void) {
    alignas(32) uint8_t asset[2048];
    pvr_chunk_asset_view_t view;
    size_t bytes = build_directory_asset(asset, sizeof(asset));

    asset[PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + 1u] ^= 1u;
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_directory_asset(asset, sizeof(asset));
    write_le32(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES +
               PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
               PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM);
    refresh_directory_checksums(asset);
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_directory_asset(asset, sizeof(asset));
    write_le32(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES +
               PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES + 8u,
               PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    refresh_directory_checksums(asset);
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_directory_asset(asset, sizeof(asset));
    write_le16(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + 30u, 3);
    refresh_directory_checksums(asset);
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == EILSEQ);

    bytes = build_directory_asset(asset, sizeof(asset));
    write_le16(asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + 28u, 99);
    refresh_directory_checksums(asset);
    errno = 0;
    assert(pvr_chunk_asset_open(asset, bytes, &view) == -1);
    assert(errno == ENOTSUP);
}

static void test_lz4_vertex(void) {
    alignas(32) uint8_t asset[2048];
    alignas(32) uint8_t workspace[2048];
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t model;
    const void *decoded;
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
    assert(pvr_chunk_asset_section_load(
        &asset_view, 0, pvr_chunk_asset_lz4_decode, NULL,
        workspace, sizeof(workspace), &decoded) == 0);
    assert(decoded == workspace &&
           !memcmp(decoded, vertices, sizeof(vertices)));

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

static void test_incremental_lz4(void) {
    alignas(32) uint8_t asset[2048];
    alignas(32) uint8_t output[sizeof(vertices)];
    pvr_chunk_asset_view_t view;
    pvr_chunk_asset_lz4_state_t *state;
    pvr_chunk_asset_lz4_progress_t progress;
    size_t previous = 0;
    size_t bytes = build_asset(asset, sizeof(asset), 1);
    int result;

    assert(pvr_chunk_asset_open(asset, bytes, &view) == 0);
    state = pvr_chunk_asset_lz4_state_create(&view.vertex, output,
                                             sizeof(output), NULL);
    assert(state);
    do {
        result = pvr_chunk_asset_lz4_state_step(state, 7);
        assert(result >= 0);
        assert(pvr_chunk_asset_lz4_state_get_progress(state, &progress) == 0);
        assert(progress.output_bytes >= previous);
        assert(progress.output_bytes - previous <= 7);
        assert(progress.output_bytes <= progress.output_total);
        previous = progress.output_bytes;
    } while(result == PVR_CHUNK_ASSET_LZ4_MORE);
    assert(result == PVR_CHUNK_ASSET_LZ4_COMPLETE);
    assert(progress.complete && progress.output_bytes == sizeof(output));
    assert(progress.source_bytes == progress.source_total);
    assert(memcmp(output, vertices, sizeof(output)) == 0);
    assert(pvr_chunk_asset_lz4_state_step(state, 7) ==
           PVR_CHUNK_ASSET_LZ4_COMPLETE);
    pvr_chunk_asset_lz4_state_destroy(state);

    errno = 0;
    assert(!pvr_chunk_asset_lz4_state_create(
        &view.vertex, asset + PVR_CHUNK_ASSET_HEADER_BYTES,
        view.vertex.decoded_bytes, NULL));
    assert(errno == EINVAL);

    view.vertex.stored_data = NULL;
    errno = 0;
    assert(!pvr_chunk_asset_lz4_state_create(
        &view.vertex, output, sizeof(output), NULL));
    assert(errno == EINVAL);
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
    test_directory_asset();
    test_section_indices();
    test_multi_model_asset();
    test_unaligned_directory_asset();
    test_corrupt_directory();
    test_lz4_vertex();
    test_incremental_lz4();
    test_unaligned_container();
    test_corrupt_headers();
    puts("pvr chunk asset tests passed");
    return 0;
}
