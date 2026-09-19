/* KallistiOS ##version##

   Host-side compact texture-section contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_texture_asset.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void build_section(uint8_t bytes[224]) {
    size_t pixel;

    memset(bytes, 0, 224);
    store_le32(bytes, PVR_CHUNK_TEXTURE_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_TEXTURE_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, 224);
    store_le32(bytes + 12, 1);
    store_le16(bytes + 16, PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES);
    store_le32(bytes + 20, 96);

    store_le16(bytes + 64, 7);
    bytes[66] = PVR_TXR_SURFACE_RGB565;
    bytes[67] = PVR_TXR_SURFACE_TWIDDLED;
    store_le16(bytes + 68, 8);
    store_le16(bytes + 70, 8);
    store_le32(bytes + 76, 96);
    store_le32(bytes + 80, 128);
    for(pixel = 0; pixel < 64; ++pixel)
        store_le16(bytes + 96 + pixel * 2,
                   (uint16_t)(pixel * UINT16_C(0x0401)));
    store_le32(bytes + 84, crc32_bytes(bytes + 96, 128));
    store_le32(bytes + 24, crc32_bytes(bytes + 64, 32));
    store_le32(bytes + 28, crc32_bytes(bytes + 96, 128));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
}

int main(void) {
    uint8_t bytes[224];
    uint8_t malformed[224];
    pvr_chunk_texture_section_view_t view;
    pvr_chunk_texture_section_view_t unchanged;
    pvr_chunk_texture_image_t image;

    build_section(bytes);
    assert(pvr_chunk_texture_section_open(bytes, sizeof(bytes), &view) == 0);
    assert(view.entry_count == 1 && view.version == 1);
    assert(pvr_chunk_texture_section_entry_get(&view, 0, &image) == 0);
    assert(image.identifier == 7 && image.width == 8 && image.height == 8);
    assert(image.format == PVR_TXR_SURFACE_RGB565);
    assert(image.layout == PVR_TXR_SURFACE_TWIDDLED);
    assert(!image.mipmapped && !image.codebook_entries);
    assert(image.data == bytes + 96 && image.data_size == 128);
    assert(pvr_chunk_texture_section_find(&view, 7, &image) == 0);

    errno = 0;
    assert(pvr_chunk_texture_section_find(&view, 6, &image) == -1);
    assert(errno == ENOENT && image.data == NULL);
    errno = 0;
    assert(pvr_chunk_texture_section_entry_get(&view, 1, &image) == -1);
    assert(errno == ENOENT && image.data == NULL);

    memcpy(malformed, bytes, sizeof(malformed));
    store_le32(malformed + 76, UINT32_C(0xffffffe0));
    store_le32(malformed + 24, crc32_bytes(malformed + 64, 32));
    store_le32(malformed + 60, crc32_bytes(malformed, 60));
    errno = 0;
    assert(pvr_chunk_texture_section_open(
               malformed, sizeof(malformed), &view) == -1);
    assert(errno == EILSEQ);

    unchanged = view;
    bytes[127] ^= 1u;
    errno = 0;
    assert(pvr_chunk_texture_section_open(bytes, sizeof(bytes), &view) == -1);
    assert(errno == EILSEQ);
    assert(!memcmp(&view, &unchanged, sizeof(view)));

    puts("PVR compact texture-section tests passed");
    return 0;
}
