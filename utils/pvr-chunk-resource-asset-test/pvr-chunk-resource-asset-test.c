/* KallistiOS ##version##

   Host-side compact resource-section contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_resource_asset.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10), UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), 0,
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), 0,
    0, UINT32_C(0x3f800000), 0, UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_TEXTURE, 2,
    PVR_CHUNK_TEXTURE, 7,
    PVR_CHUNK_STRIP_INDEX, 5, 1, 3, 0, 1, 2,
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

static void store_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void build_section(uint8_t bytes[64]) {
    memset(bytes, 0, 64);
    store_le32(bytes, PVR_CHUNK_RESOURCE_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_RESOURCE_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, 64);
    store_le32(bytes + 12, 2);
    store_le16(bytes + 16, PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES);
    store_le32(bytes + 20, 16);
    store_le16(bytes + 48, 2);
    store_le16(bytes + 50, PVR_CHUNK_RESOURCE_PRIMARY);
    store_le16(bytes + 56, 7);
    store_le16(bytes + 58, PVR_CHUNK_RESOURCE_PRIMARY);
    store_le32(bytes + 24, crc32_bytes(bytes + 48, 16));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
}

int main(void) {
    uint8_t bytes[64];
    pvr_chunk_resource_section_view_t view;
    pvr_chunk_resource_section_view_t unchanged;
    pvr_chunk_resource_entry_t entry;
    pvr_chunk_model_t model = {
        .vertex_words = vertices,
        .vertex_word_count = sizeof(vertices) / sizeof(vertices[0]),
        .polygon_words = polygons,
        .polygon_word_count = sizeof(polygons) / sizeof(polygons[0]),
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 2.0f
    };
    pvr_chunk_model_view_t model_view;

    build_section(bytes);
    assert(pvr_chunk_resource_section_open(bytes, sizeof(bytes), &view) == 0);
    assert(view.entry_count == 2 && view.version == 1);
    assert(pvr_chunk_resource_section_entry_get(&view, 0, &entry) == 0);
    assert(entry.identifier == 2 && entry.usage == PVR_CHUNK_RESOURCE_PRIMARY);
    assert(pvr_chunk_resource_section_find(&view, 7, &entry) == 0);
    assert(entry.identifier == 7);
    errno = 0;
    assert(pvr_chunk_resource_section_find(&view, 7, NULL) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_chunk_resource_section_entry_get(&view, 0, NULL) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_chunk_resource_section_find(&view, 6, &entry) == -1);
    assert(errno == ENOENT && entry.identifier == 0 && entry.usage == 0);
    assert(pvr_chunk_model_open(&model, &model_view) == 0);
    assert(pvr_chunk_resource_section_validate_model(&view, &model_view) == 0);

    store_le16(bytes + 58, PVR_CHUNK_RESOURCE_SECONDARY);
    store_le32(bytes + 24, crc32_bytes(bytes + 48, 16));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
    errno = 0;
    assert(pvr_chunk_resource_section_open(bytes, sizeof(bytes), &view) == 0);
    assert(pvr_chunk_resource_section_validate_model(&view, &model_view) == -1);
    assert(errno == EILSEQ);
    unchanged = view;
    bytes[63] ^= 1u;
    errno = 0;
    assert(pvr_chunk_resource_section_open(bytes, sizeof(bytes), &view) == -1);
    assert(errno == EILSEQ);
    assert(!memcmp(&view, &unchanged, sizeof(view)));

    puts("PVR compact resource-section tests passed");
    return 0;
}
