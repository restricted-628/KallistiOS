/* KallistiOS ##version##
   Layer-section golden, rejection and source-model association tests.
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_layer_asset.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

/* Independently encoded byte fixture, including both fixed CRCs. */
static const uint8_t golden[96] = {
    0x50,0x4d,0x4c,0x31,0x01,0x00,0x20,0x00,0x60,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x53,0xb2,0xc2,0x40,
    0x00,0x00,0x00,0x00,0xdd,0xa5,0x4c,0xbf,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x07,0x00,
    0x01,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x56,0x34,0x12,0x00,
    0x00,0x00,0x80,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x3f,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(n >> (8 * i));
}

static uint32_t checksum(const uint8_t *p, size_t bytes) {
    uint32_t table[256], crc = UINT32_MAX;
    for(unsigned i = 0; i < 256; ++i) {
        uint32_t c = i;
        for(unsigned j = 0; j < 8; ++j)
            c = c & 1 ? (c >> 1) ^ UINT32_C(0xedb88320) : c >> 1;
        table[i] = c;
    }
    for(size_t i = 0; i < bytes; ++i)
        crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static void repair(uint8_t *p, size_t size) {
    put32(p + 20, checksum(p + 32, size - 32));
    put32(p + 28, checksum(p, 28));
}

static pvr_chunk_layer_entry_t initial(void) {
    pvr_chunk_layer_entry_t entry = { .strip_count = 1, .layer = {
        .role = PVR_MATERIAL_PASS_LIGHTMAP,
        .texture = { .identifier = 7, .filter = PVR_FILTER_BILINEAR,
                     .mipmap_adjust = PVR_MIPBIAS_NORMAL },
        .rgb = 0x123456, .uv = { {1,0,0}, {0,1,0} }
    } };
    return entry;
}

static void test_golden(void) {
    alignas(32) uint8_t bytes[128];
    pvr_chunk_layer_entry_t entry = initial(), decoded;
    pvr_chunk_layer_section_view_t view, previous;
    size_t size = 777;
    assert(pvr_chunk_layer_section_query(1, &size) == 0 && size == 96);
    assert(pvr_chunk_layer_section_write(&entry, 1, bytes + 1, 96) == 0);
    assert(!memcmp(bytes + 1, golden, 96));
    assert(pvr_chunk_layer_section_open(bytes + 1, 96, &view) == 0);
    assert(pvr_chunk_layer_section_entry_get(&view, 0, &decoded) == 0);
    assert(decoded.model == 0 && decoded.first_strip == 0 && decoded.strip_count == 1);
    assert(decoded.layer.role == PVR_MATERIAL_PASS_LIGHTMAP);
    assert(decoded.layer.texture.identifier == 7 && decoded.layer.rgb == 0x123456);
    assert(decoded.layer.uv[0][0] == 1 && decoded.layer.uv[1][1] == 1);
    assert(pvr_chunk_layer_section_entry_get(
        &view, 0, (pvr_chunk_layer_entry_t *)(void *)(bytes + 32)) == -1);
    assert(pvr_chunk_layer_section_find(
        &view, 0, 0, (pvr_chunk_layer_entry_t *)(void *)&view) == -1);
    assert(!memcmp(bytes + 1, golden, 96));
    assert(pvr_chunk_layer_section_open(bytes + 1, 96,
        (pvr_chunk_layer_section_view_t *)(void *)(bytes + 32)) == -1);
    assert(!memcmp(bytes + 1, golden, 96));
    previous = view;
    for(size_t i = 0; i < 96; ++i) {
        assert(pvr_chunk_layer_section_open(golden, i, &view) == -1);
        assert(!memcmp(&view, &previous, sizeof(view)));
        memcpy(bytes, golden, 96);
        bytes[i] ^= 1;
        assert(pvr_chunk_layer_section_open(bytes, 96, &view) == -1);
    }
    /* With fresh checksums these must still fail semantic/framing checks. */
    const unsigned invalid_offsets[] = {4, 6, 16, 18, 24,
        32 + 12, 32 + 16, 32 + 17, 32 + 18, 32 + 19, 32 + 20,
        32 + 21, 32 + 22, 32 + 27, 32 + 52, 32 + 56, 32 + 60};
    for(size_t i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        memcpy(bytes, golden, 96);
        bytes[invalid_offsets[i]] = 255;
        repair(bytes, 96);
        assert(pvr_chunk_layer_section_open(bytes, 96, &view) == -1);
        assert(!memcmp(&view, &previous, sizeof(view)));
    }
    memcpy(bytes, golden, 96);
    put32(bytes + 60, UINT32_C(0x7fc00000)); /* UV NaN, valid CRC. */
    repair(bytes, 96);
    assert(pvr_chunk_layer_section_open(bytes, 96, &view) == -1);
    size = 777;
    assert(pvr_chunk_layer_section_query(SIZE_MAX, &size) == -1 && size == 777);
    assert(pvr_chunk_layer_section_query(0, &size) == -1 && size == 777);
    memset(bytes, 0x5a, sizeof(bytes));
    assert(pvr_chunk_layer_section_write(&entry, 1, bytes, 95) == -1);
    for(size_t i = 0; i < sizeof(bytes); ++i)
        assert(bytes[i] == 0x5a);
    entry.layer.uv[0][0] = INFINITY;
    assert(pvr_chunk_layer_section_write(&entry, 1, bytes, 96) == -1);
    assert(bytes[0] == 0x5a);
    entry = initial();
    assert(pvr_chunk_layer_section_write(&entry, 1, &entry, 96) == -1);
    assert(entry.layer.rgb == 0x123456);
}

static void test_ranges(void) {
    pvr_chunk_layer_entry_t entries[3], found, sentinel;
    uint8_t bytes[32 + 3 * 64], saved[sizeof(bytes)];
    pvr_chunk_layer_section_view_t view;
    const uint32_t vertices[] = {
        PVR_CHUNK_VERTEX_XYZ | (10u << 16), 3u << 16,
        0,0,0, 0x3f800000,0,0, 0,0x3f800000,0, 0xff
    };
    const uint16_t polygons[] = {
        PVR_CHUNK_STRIP_INDEX, 17, 4,
        3,0,1,2, 3,0,1,2, 3,0,1,2, 3,0,1,2, 0xff
    };
    pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]), {0,0,0}, 2
    };
    pvr_chunk_model_view_t models[2];
    entries[0] = entries[1] = entries[2] = initial();
    entries[1].first_strip = 2;
    entries[1].strip_count = 2;
    entries[1].layer.role = PVR_MATERIAL_PASS_EMISSIVE;
    entries[1].layer.texture.identifier = 19;
    entries[1].layer.uv[0][0] = -2;
    entries[2].model = 1;
    assert(pvr_chunk_layer_section_write(entries, 3, bytes, sizeof(bytes)) == 0);
    assert(pvr_chunk_layer_section_open(bytes, sizeof(bytes), &view) == 0);
    assert(pvr_chunk_model_open(&model, &models[0]) == 0);
    models[1] = models[0];
    assert(models[0].info.strips == 4);
    assert(pvr_chunk_layer_section_validate_models(&view, models, 2) == 0);
    assert(pvr_chunk_layer_section_validate_models(&view, models, 1) == -1);
    assert(pvr_chunk_layer_section_find(&view, 0, 3, &found) == 0);
    assert(found.first_strip == 2 && found.layer.texture.identifier == 19);
    assert(found.layer.role == PVR_MATERIAL_PASS_EMISSIVE && found.layer.uv[0][0] == -2);
    assert(pvr_chunk_layer_section_find(&view, 1, 0, &found) == 0 && found.model == 1);
    sentinel = found;
    assert(pvr_chunk_layer_section_find(&view, 0, 1, &found) == -1 && errno == ENOENT);
    assert(!memcmp(&sentinel, &found, sizeof(found)));
    assert(pvr_chunk_layer_section_find(&view, 0, 4, &found) == -1);
    assert(pvr_chunk_layer_section_find(&view, UINT32_MAX, UINT32_MAX, &found) == -1);
    assert(pvr_chunk_layer_section_entry_get(&view, 3, &found) == -1);
    memcpy(saved, bytes, sizeof(bytes));
    entries[1].first_strip = 0; /* Overlapping material associations. */
    assert(pvr_chunk_layer_section_write(entries, 3, bytes, sizeof(bytes)) == -1);
    assert(!memcmp(bytes, saved, sizeof(bytes)));
    put32(bytes + 32 + 64 + 4, 0);
    repair(bytes, sizeof(bytes));
    assert(pvr_chunk_layer_section_open(bytes, sizeof(bytes), &view) == -1);
    entries[1].first_strip = UINT32_MAX;
    assert(pvr_chunk_layer_section_write(entries, 3, bytes, sizeof(bytes)) == -1);
    entries[1].strip_count = 1; /* Last representable index, no range overflow. */
    assert(pvr_chunk_layer_section_write(entries, 3, bytes, sizeof(bytes)) == 0);
    assert(pvr_chunk_layer_section_open(bytes, sizeof(bytes), &view) == 0);
    assert(pvr_chunk_layer_section_find(&view, 0, UINT32_MAX, &found) == 0);
    assert(pvr_chunk_layer_section_validate_models(&view, models, 2) == -1);
}

int main(void) {
    test_golden();
    test_ranges();
    puts("pvr-chunk-layer-test: PASS");
    return 0;
}
