/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_uv_asset.h>
#include "pvr-uv-ir.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#ifndef __DREAMCAST__
void pvr_mod_compile(pvr_mod_hdr_t *out, pvr_list_t list, uint32_t mode,
                      uint32_t cull) {
    (void)out; (void)list; (void)mode; (void)cull;
    assert(0 && "no modifier submission in UV fixture");
}
int pvr_prim(const void *data, size_t bytes) {
    (void)data; (void)bytes;
    assert(0 && "no hardware submission in UV fixture");
    return -1;
}
int pvr_list_prim(pvr_list_t list, const void *data, size_t bytes) {
    (void)list;
    return pvr_prim(data, bytes);
}
#endif

/* Independent Python struct/zlib golden, including fixed CRCs. */
static const uint8_t golden[88] = {
    0x50,0x55,0x56,0x31,0x01,0x00,0x28,0x00,
    0x58,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x10,0x00,0x08,0x00,
    0x08,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
    0xba,0xb1,0x7f,0x50,0xdf,0x55,0x10,0x24,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x80,0xbf,0x00,0x00,0x00,0x40,
    0x00,0x00,0x40,0x40,0x00,0x00,0x80,0x40,
    0x00,0x00,0xa0,0x40,0x00,0x00,0xc0,0x40
};
static const pvr_chunk_uv_t coords[] = {{-1,2}, {3,4}, {5,6}};

static void put32(uint8_t *p, uint32_t x) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static uint32_t checksum(const uint8_t *p, size_t size) {
    uint32_t table[256], c = UINT32_MAX;
    for(unsigned i = 0; i < 256; ++i) {
        uint32_t v = i;
        for(unsigned j = 0; j < 8; ++j)
            v = v & 1 ? (v >> 1) ^ UINT32_C(0xedb88320) : v >> 1;
        table[i] = v;
    }
    for(size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
static void repair(uint8_t *p) {
    put32(p + 32, checksum(p + 40, 48));
    put32(p + 36, checksum(p, 36));
}

static void codec(void) {
    pvr_chunk_uv_asset_source_t input = {0, coords, 3};
    pvr_chunk_uv_asset_binding_t binding = {0, 0};
    pvr_chunk_uv_section_view_t view, saved;
    pvr_chunk_uv_asset_info_t info;
    alignas(32) uint8_t bytes[192], before[192];
    pvr_chunk_uv_t decoded[3];
    size_t size = 999;
    assert(pvr_chunk_uv_section_query(1, 1, 3, &size) == 0 && size == 88);
    assert(pvr_chunk_uv_section_query(SIZE_MAX, 1, 3, &size) < 0);
    assert(size == 88);
    assert(pvr_chunk_uv_section_query(1, 0, 3, &size) < 0 && size == 88);
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1, bytes + 1, 88) == 0);
    assert(!memcmp(bytes + 1, golden, 88));
    assert(pvr_chunk_uv_section_open(bytes + 1, 88, &view) == 0);
    assert(pvr_chunk_uv_section_source_get(&view, 0, &info) == 0);
    assert(info.model == 0 && info.uv_count == 3);
    assert(pvr_chunk_uv_section_decode(&view, 0, decoded, 3) == 0);
    assert(!memcmp(decoded, coords, sizeof(coords)));
    uint32_t source = 99;
    assert(pvr_chunk_uv_section_find(&view, 0, &source) == 0 && source == 0);
    assert(pvr_chunk_uv_section_find(&view, 1, &source) < 0 && errno == ENOENT);
    assert(source == 0);
    memset(decoded, 0x5a, sizeof(decoded));
    uint8_t untouched[sizeof(decoded)];
    memcpy(untouched, decoded, sizeof(decoded));
    assert(pvr_chunk_uv_section_decode(&view, 0, decoded, 2) < 0 && errno == ENOSPC);
    assert(!memcmp(untouched, decoded, sizeof(decoded)));
    assert(pvr_chunk_uv_section_decode(&view, 1, decoded, 3) < 0 && errno == ERANGE);
    assert(!memcmp(untouched, decoded, sizeof(decoded)));
    assert(pvr_chunk_uv_section_decode(&view, 0, (void *)(bytes + 32), 3) < 0);
    assert(pvr_chunk_uv_section_find(&view, 0, (void *)&view) < 0);
    assert(!memcmp(bytes + 1, golden, 88));
    memcpy(&saved, &view, sizeof(saved));
    for(size_t i = 0; i < 88; ++i) {
        assert(pvr_chunk_uv_section_open(golden, i, &view) < 0);
        assert(!memcmp(&view, &saved, sizeof(view)));
        memcpy(bytes, golden, 88);
        bytes[i] ^= 1;
        assert(pvr_chunk_uv_section_open(bytes, 88, &view) < 0);
        assert(!memcmp(&view, &saved, sizeof(view)));
    }
    /* Repaired CRCs must not admit bad geometry, ordering or reserved fields. */
    const unsigned bad[] = {4,6,12,16,20,22,24,26,28,44,48,52,60};
    for(size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        memcpy(bytes, golden, 88);
        bytes[bad[i]] ^= 1;
        repair(bytes);
        assert(pvr_chunk_uv_section_open(bytes, 88, &view) < 0);
    }
    memcpy(bytes, golden, 88);
    put32(bytes + 64, UINT32_C(0x7fc00000));
    repair(bytes);
    assert(pvr_chunk_uv_section_open(bytes, 88, &view) < 0);
    memcpy(bytes, golden, 88);
    assert(pvr_chunk_uv_section_open(bytes, 89, &view) < 0);
    assert(pvr_chunk_uv_section_open(bytes, 88, (void *)(bytes + 32)) < 0);
    memset(bytes, 0x5a, sizeof(bytes));
    memcpy(before, bytes, sizeof(before));
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1, bytes, 87) < 0);
    binding.source = 1;
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1, bytes, 192) < 0);
    assert(!memcmp(bytes, before, sizeof(bytes)));
    binding.source = 0;
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1,
                                      &input, 88) < 0);
    pvr_chunk_uv_asset_binding_t duplicates[] = {{1,0},{1,0}};
    assert(pvr_chunk_uv_section_write(&input, 1, duplicates, 2, bytes, 192) < 0);
    assert(!memcmp(bytes, before, sizeof(bytes)));
    pvr_chunk_uv_t bad_coords[] = {{-1,2},{3,4},{5,NAN}};
    input.uv = bad_coords;
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1, bytes, 192) < 0);
    assert(!memcmp(bytes, before, sizeof(bytes)));
    input.uv = (void *)(bytes + 32);
    assert(pvr_chunk_uv_section_write(&input, 1, &binding, 1, bytes, 192) < 0);
    assert(!memcmp(bytes, before, sizeof(bytes)));
}

static void multiple_sources(void) {
    const pvr_chunk_uv_t other[] = {{9,-10},{11,12}};
    const pvr_chunk_uv_asset_source_t inputs[] = {{7,coords,3},{8,other,2}};
    const pvr_chunk_uv_asset_binding_t bindings[] = {{2,1},{4,0},{9,1}};
    uint8_t bytes[136];
    pvr_chunk_uv_section_view_t view;
    pvr_chunk_uv_asset_info_t info;
    pvr_chunk_uv_t decoded[3];
    size_t size;
    assert(pvr_chunk_uv_section_query(2, 3, 5, &size) == 0 && size == 136);
    assert(pvr_chunk_uv_section_write(inputs, 2, bindings, 3, bytes, size) == 0);
    assert(pvr_chunk_uv_section_open(bytes, size, &view) == 0);
    assert(pvr_chunk_uv_section_source_get(&view, 1, &info) == 0);
    assert(info.model == 8 && info.uv_count == 2);
    assert(pvr_chunk_uv_section_decode(&view, 1, decoded, 3) == 0);
    assert(!memcmp(decoded, other, sizeof(other)));
    assert(pvr_chunk_uv_section_decode(&view, 0, decoded, 3) == 0);
    assert(!memcmp(decoded, coords, sizeof(coords)));
    for(uint32_t layer = 0; layer < 11; ++layer) {
        uint32_t source = 99;
        int result = pvr_chunk_uv_section_find(&view, layer, &source);
        if(layer == 2 || layer == 9)
            assert(result == 0 && source == 1);
        else if(layer == 4)
            assert(result == 0 && source == 0);
        else
            assert(result < 0 && errno == ENOENT && source == 99);
    }
    /* Even with correct CRCs, a second source cannot overlap the first. */
    put32(bytes + 60, 2);
    put32(bytes + 32, checksum(bytes + 40, size - 40));
    put32(bytes + 36, checksum(bytes, 36));
    assert(pvr_chunk_uv_section_open(bytes, size, &view) < 0);
}

static void binding_and_render(void) {
    static const uint32_t vertices[] = {
        PVR_CHUNK_VERTEX_XYZ | (10u << 16), 0x00030000,
        0xbf800000, 0, 0x3f800000, 0x3f800000, 0, 0x3f800000,
        0, 0x3f800000, 0x3f800000, 255
    };
    static const uint16_t polygons[] = {
        PVR_CHUNK_STRIP_UV10_FIXED, 21, 2,
        3, 0,0,0, 1,0,0, 2,0,0,
        0x8003, 0,0,0, 1,0,0, 2,0,0, 255
    };
    const pvr_chunk_model_t model = {vertices, 12, polygons,
        sizeof(polygons) / sizeof(*polygons), {0,0,1}, 2};
    pvr_chunk_model_view_t models[2];
    assert(pvr_chunk_model_open(&model, &models[0]) == 0);
    models[1] = models[0];
    pvr_chunk_layer_entry_t entries[2] = {
        {.strip_count = 1, .layer = {.role = PVR_MATERIAL_PASS_EMISSIVE,
         .texture = {.identifier = 7, .mipmap_adjust = 4}, .rgb = 0x123456,
         .uv = {{1,0,0},{0,1,0}}}},
        {.first_strip = 1, .strip_count = 1, .layer = {
         .role = PVR_MATERIAL_PASS_LIGHTMAP,
         .texture = {.identifier = 8, .mipmap_adjust = 4}, .rgb = 0xabcdef,
         .uv = {{1,0,0},{0,1,0}}}}
    };
    uint8_t layer_bytes[160], uv_bytes[256];
    pvr_chunk_layer_section_view_t layers;
    /* Host compiler decision -> actual PML1/PUV1 writers -> actual renderer.
       The base mapping collapsed the authored coordinates to zero. Repeated
       canonical vertex indices still have distinct per-reference layer UVs. */
    pvr_uv_ir_transform_t base = {.source_set = 0, .row = {{0,0,0},{0,0,0}}};
    pvr_uv_ir_transform_t auxiliary = {
        .source_set = 0, .row = {{-2,0,.25},{0,.5,-1}}
    };
    pvr_uv_ir_sample_t samples[] = {
        {{0,0},{0,0}}, {{0,0},{1,0}}, {{0,0},{0,1}},
        {{0,0},{-2,3}}, {{0,0},{4,5}}, {{0,0},{6,7}}
    };
    pvr_uv_ir_selection_t selection;
    assert(pvr_uv_ir_select(&base, &auxiliary, samples, 6, 0, &selection) == 0);
    assert(selection.storage == PVR_UV_IR_INDEPENDENT);
    pvr_chunk_uv_t input[6];
    const pvr_chunk_uv_t expected[] = {
        {.25f,-1}, {-1.75f,-1}, {.25f,-.5f},
        {4.25f,.5f}, {-7.75f,1.5f}, {-11.75f,2.5f}
    };
    for(size_t i = 0; i < 6; ++i) {
        float pair[2];
        assert(pvr_uv_ir_apply(&auxiliary, samples[i].auxiliary, pair) == 0);
        input[i] = (pvr_chunk_uv_t){pair[0], pair[1]};
        assert(input[i].u == expected[i].u && input[i].v == expected[i].v);
    }
    for(size_t i = 0; i < 2; ++i) {
        memcpy(entries[i].layer.uv, selection.mapping, sizeof(selection.mapping));
        /* Independent coordinates are already transformed, never twice. */
        assert(entries[i].layer.uv[0][0] == 1 && entries[i].layer.uv[1][1] == 1);
        assert(entries[i].layer.uv[0][2] == 0 && entries[i].layer.uv[1][2] == 0);
    }
    assert(pvr_chunk_layer_section_write(entries, 2, layer_bytes, 160) == 0);
    assert(pvr_chunk_layer_section_open(layer_bytes, 160, &layers) == 0);
    pvr_chunk_uv_asset_source_t source = {0, input, 6};
    pvr_chunk_uv_asset_binding_t bindings[] = {{0,0},{1,0}};
    pvr_chunk_uv_section_view_t view;
    size_t bytes;
    assert(pvr_chunk_uv_section_query(1, 2, 6, &bytes) == 0);
    assert(pvr_chunk_uv_section_write(&source, 1, bindings, 2, uv_bytes, 256) == 0);
    assert(pvr_chunk_uv_section_open(uv_bytes, bytes, &view) == 0);
    assert(pvr_chunk_uv_section_validate_layers(&view, &layers, models, 2) == 0);
    uint32_t selected = 999;
    assert(pvr_chunk_uv_section_find(&view, 1, &selected) == 0 && selected == 0);
    pvr_chunk_uv_t decoded[6];
    pvr_chunk_uv_strip_t index[2];
    pvr_chunk_uv_source_t runtime;
    assert(pvr_chunk_uv_section_decode(&view, selected, decoded, 6) == 0);
    assert(pvr_chunk_uv_source_init(&models[0], decoded, 6, index, 2, &runtime) == 0);
    const matrix_t matrix = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    pvr_frustum_t frustum;
    assert(pvr_frustum_init(&frustum, &matrix, -2,-2,2,2,.5f,2) == 0);
    alignas(32) pvr_vertex_t output[6], workspace[3];
    pvr_geometry_sink_t sink;
    assert(pvr_geometry_sink_init_memory(&sink, output, 6) == 0);
    assert(pvr_chunk_model_emit_uv(&runtime, NULL, &frustum,
        PVR_CHUNK_CLIP_ASSUME_VISIBLE, &sink, workspace, 3, NULL, 0,
        NULL, NULL, NULL, NULL, NULL) == 0);
    const unsigned order[] = {0,1,2,4,3,5};
    for(unsigned i = 0; i < 6; ++i)
        assert(output[i].u == input[order[i]].u && output[i].v == input[order[i]].v);
    /* Structurally valid bytes can still name the wrong model/count/layer. */
    source.model = 1;
    assert(pvr_chunk_uv_section_write(&source, 1, bindings, 2, uv_bytes, 256) == 0);
    assert(pvr_chunk_uv_section_open(uv_bytes, bytes, &view) == 0);
    assert(pvr_chunk_uv_section_validate_layers(&view, &layers, models, 2) < 0);
    source.model = 2;
    assert(pvr_chunk_uv_section_write(&source, 1, bindings, 2, uv_bytes, 256) == 0);
    assert(pvr_chunk_uv_section_open(uv_bytes, bytes, &view) == 0);
    assert(pvr_chunk_uv_section_validate_layers(&view, &layers, models, 2) < 0);
    source.model = 0;
    source.uv_count = 3;
    assert(pvr_chunk_uv_section_query(1, 2, 3, &bytes) == 0);
    assert(pvr_chunk_uv_section_write(&source, 1, bindings, 2, uv_bytes, 256) == 0);
    assert(pvr_chunk_uv_section_open(uv_bytes, bytes, &view) == 0);
    assert(pvr_chunk_uv_section_validate_layers(&view, &layers, models, 2) < 0);
    source.uv_count = 6;
    bindings[1].layer = 2;
    assert(pvr_chunk_uv_section_query(1, 2, 6, &bytes) == 0);
    assert(pvr_chunk_uv_section_write(&source, 1, bindings, 2, uv_bytes, 256) == 0);
    assert(pvr_chunk_uv_section_open(uv_bytes, bytes, &view) == 0);
    assert(pvr_chunk_uv_section_validate_layers(&view, &layers, models, 2) < 0);
}

int main(void) {
    codec();
    multiple_sources();
    binding_and_render();
    puts("pvr-chunk-uv-asset-test: PASS");
    return 0;
}
