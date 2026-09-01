/* KallistiOS ##version##

   Host-side compact scene hierarchy tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_scene.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void refresh_checksums(uint8_t *bytes, size_t size) {
    write_le32(bytes + 20, crc32_bytes(
        bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES,
        size - PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES));
    write_le32(bytes + 28, crc32_bytes(bytes, 28));
}

static void test_round_trip(void) {
    static const float child_transform[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        2.0f, 3.0f, 4.0f, 1.0f
    };
    pvr_scene_ir_t scene = { 0 };
    pvr_chunk_scene_hierarchy_view_t view;
    pvr_chunk_scene_node_t decoded;
    pvr_chunk_model_view_t model0 = { 0 };
    pvr_chunk_model_view_t model1 = { 0 };
    const pvr_chunk_model_view_t *models[] = { &model0, &model1 };
    pvr_chunk_hierarchy_node_t nodes[3];
    pvr_chunk_hierarchy_t hierarchy;
    uint8_t *bytes = NULL;
    size_t size = 0;

    assert(pvr_scene_ir_add_root_model(&scene, 0) == 0);
    assert(pvr_scene_ir_add_node_flags(
               &scene, 0, UINT32_MAX, PVR_CHUNK_NODE_HIDDEN,
               child_transform) == 0);
    assert(pvr_scene_ir_add_node_flags(
               &scene, 1, 1,
               PVR_CHUNK_NODE_SUPPRESS_TRANSLATION |
               PVR_CHUNK_NODE_PRUNE_CHILDREN,
               child_transform) == 0);
    assert(pvr_scene_ir_serialize_hierarchy(&scene, &bytes, &size) == 0);
    assert(size == PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES +
                   3u * PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == 0);
    assert(view.version == PVR_CHUNK_SCENE_HIERARCHY_VERSION &&
           view.node_count == 3 &&
           view.node_stride == PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES);
    assert(pvr_chunk_scene_hierarchy_node_get(&view, 1, &decoded) == 0);
    assert(decoded.parent_index == 0 &&
           decoded.model_ordinal == PVR_CHUNK_NODE_NONE &&
           decoded.flags == PVR_CHUNK_NODE_HIDDEN &&
           ((float *)decoded.local_transform)[12] == 2.0f);
    assert(pvr_chunk_scene_hierarchy_bind(
               &view, models, 2, nodes, 3, &hierarchy) == 0);
    assert(hierarchy.nodes == nodes && hierarchy.node_count == 3);
    assert(nodes[0].model == &model0 &&
           nodes[1].model == NULL && nodes[2].model == &model1);
    assert(nodes[1].flags == PVR_CHUNK_NODE_HIDDEN &&
           nodes[2].parent_index == 1 && nodes[2].user_data == NULL &&
           nodes[2].flags == (PVR_CHUNK_NODE_SUPPRESS_TRANSLATION |
                              PVR_CHUNK_NODE_PRUNE_CHILDREN));

    free(bytes);
    pvr_scene_ir_free(&scene);
}

static void test_rejections(void) {
    pvr_scene_ir_t scene = { 0 };
    pvr_chunk_scene_hierarchy_view_t view;
    pvr_chunk_model_view_t model = { 0 };
    const pvr_chunk_model_view_t *models[] = { &model };
    pvr_chunk_hierarchy_node_t node;
    pvr_chunk_hierarchy_t hierarchy;
    uint8_t *bytes = NULL;
    size_t size = 0;

    assert(pvr_scene_ir_add_root_model(&scene, 0) == 0);
    assert(pvr_scene_ir_serialize_hierarchy(&scene, &bytes, &size) == 0);

    write_le16(bytes + 4, PVR_CHUNK_SCENE_HIERARCHY_VERSION_1);
    refresh_checksums(bytes, size);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == 0);
    assert(view.version == PVR_CHUNK_SCENE_HIERARCHY_VERSION_1);
    write_le32(bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES + 8,
               PVR_CHUNK_NODE_HIDDEN);
    refresh_checksums(bytes, size);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == -1);
    assert(errno == EILSEQ);
    write_le32(bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES + 8, 0);
    write_le16(bytes + 4, PVR_CHUNK_SCENE_HIERARCHY_VERSION);
    refresh_checksums(bytes, size);

    bytes[32] ^= 1u;
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == -1);
    assert(errno == EILSEQ);
    bytes[32] ^= 1u;
    refresh_checksums(bytes, size);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == 0);

    write_le32(bytes + 32, 0);
    refresh_checksums(bytes, size);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == -1);
    assert(errno == EILSEQ);

    write_le32(bytes + 32, UINT32_MAX);
    write_le32(bytes + 36, 7);
    refresh_checksums(bytes, size);
    assert(pvr_chunk_scene_hierarchy_open(bytes, size, &view) == 0);
    memset(&node, 0xa5, sizeof(node));
    assert(pvr_chunk_scene_hierarchy_bind(
               &view, models, 1, &node, 1, &hierarchy) == -1);
    assert(errno == EILSEQ && hierarchy.nodes == NULL &&
           hierarchy.node_count == 0);
    {
        const unsigned char *unchanged = (const unsigned char *)&node;
        size_t index;

        for(index = 0; index < sizeof(node); ++index)
            assert(unchanged[index] == 0xa5);
    }

    free(bytes);
    pvr_scene_ir_free(&scene);
}

static void test_ir_rejections(void) {
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    float invalid[16];
    pvr_scene_ir_t scene = { 0 };

    assert(pvr_scene_ir_add_node(&scene, 0, 0, identity) == -1);
    assert(errno == EINVAL);
    memcpy(invalid, identity, sizeof(invalid));
    invalid[5] = NAN;
    assert(pvr_scene_ir_add_node(
               &scene, UINT32_MAX, 0, invalid) == -1);
    assert(errno == EDOM);
    assert(pvr_scene_ir_add_node_flags(
               &scene, UINT32_MAX, 0, UINT32_C(0x80000000),
               identity) == -1);
    assert(errno == EINVAL);
    pvr_scene_ir_free(&scene);
}

int main(void) {
    test_round_trip();
    test_rejections();
    test_ir_rejections();
    puts("pvr chunk scene tests passed");
    return 0;
}
