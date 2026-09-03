/* KallistiOS ##version##

   Host-side compact scene hierarchy tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_scene.h>
#include <dc/pvr_chunk_animation_asset.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERTEX_HEADER(type, size) \
    ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t scene_vertices0[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint32_t scene_vertices1[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xc0000000), UINT32_C(0xc0000000), UINT32_C(0),
    UINT32_C(0x40000000), UINT32_C(0xc0000000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x40000000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint16_t scene_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const uint16_t deferred_scene_polygons[] = {
    (UINT16_C(1) << 8) | PVR_CHUNK_CONTROL_CACHE_POLYGONS,
    (UINT16_C(1) << 8) | PVR_CHUNK_CONTROL_DRAW_CACHED_POLYGONS,
    UINT16_C(0), UINT16_C(0), UINT16_C(0), UINT16_C(0), UINT16_C(0),
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

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
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
                          size_t offset, const void *stored,
                          size_t stored_bytes, size_t decoded_bytes,
                          uint16_t codec, uint16_t alignment) {
    write_le32(descriptor, type);
    write_le32(descriptor + 8, (uint32_t)offset);
    write_le32(descriptor + 12, (uint32_t)stored_bytes);
    write_le32(descriptor + 16, (uint32_t)decoded_bytes);
    write_le32(descriptor + 20, crc32_bytes(stored, decoded_bytes));
    write_le16(descriptor + 28, codec);
    write_le16(descriptor + 30, alignment);
}

static void init_model_record(pvr_chunk_model_table_record_t *record,
                              size_t ordinal, float center,
                              float radius) {
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

static size_t build_scene_asset(uint8_t *asset, size_t capacity,
                                const void *table, size_t table_bytes,
                                const void *hierarchy,
                                size_t hierarchy_bytes,
                                int compress_first_vertex) {
    const size_t section_count = 6;
    const size_t directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    size_t vertex0_offset = align32(
        PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + directory_bytes);
    size_t polygon0_offset = align32(
        vertex0_offset + sizeof(scene_vertices0));
    size_t vertex1_offset = align32(
        polygon0_offset + sizeof(scene_polygons));
    size_t polygon1_offset = align32(
        vertex1_offset + sizeof(scene_vertices1));
    size_t table_offset = align32(
        polygon1_offset + sizeof(scene_polygons));
    size_t hierarchy_offset = align32(table_offset + table_bytes);
    size_t file_bytes = hierarchy_offset + hierarchy_bytes;
    uint8_t *directory = asset + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;

    assert(file_bytes <= capacity);
    memset(asset, 0, capacity);
    memcpy(asset + vertex0_offset, scene_vertices0,
           sizeof(scene_vertices0));
    memcpy(asset + polygon0_offset, scene_polygons,
           sizeof(scene_polygons));
    memcpy(asset + vertex1_offset, scene_vertices1,
           sizeof(scene_vertices1));
    memcpy(asset + polygon1_offset, scene_polygons,
           sizeof(scene_polygons));
    memcpy(asset + table_offset, table, table_bytes);
    memcpy(asset + hierarchy_offset, hierarchy, hierarchy_bytes);

    write_section(directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
                  vertex0_offset, scene_vertices0, sizeof(scene_vertices0),
                  sizeof(scene_vertices0), compress_first_vertex ?
                      PVR_CHUNK_ASSET_CODEC_LZ4_FRAME :
                      PVR_CHUNK_ASSET_CODEC_RAW, 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon0_offset, scene_polygons, sizeof(scene_polygons),
                  sizeof(scene_polygons), PVR_CHUNK_ASSET_CODEC_RAW, 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 2u,
                  PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
                  vertex1_offset, scene_vertices1, sizeof(scene_vertices1),
                  sizeof(scene_vertices1),
                  PVR_CHUNK_ASSET_CODEC_LZ4_FRAME, 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 3u,
                  PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
                  polygon1_offset, scene_polygons, sizeof(scene_polygons),
                  sizeof(scene_polygons), PVR_CHUNK_ASSET_CODEC_RAW, 2);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 4u,
                  PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
                  table_offset, table, table_bytes, table_bytes,
                  PVR_CHUNK_ASSET_CODEC_RAW, 4);
    write_section(directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES * 5u,
                  PVR_CHUNK_ASSET_SECTION_HIERARCHY,
                  hierarchy_offset, hierarchy, hierarchy_bytes,
                  hierarchy_bytes, PVR_CHUNK_ASSET_CODEC_RAW, 4);

    write_le32(asset, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    write_le16(asset + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    write_le16(asset + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 8, (uint32_t)file_bytes);
    write_float(asset + 28, 4.0f);
    write_le32(asset + 32, (uint32_t)section_count);
    write_le32(asset + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    write_le32(asset + 40, (uint32_t)directory_bytes);
    write_le32(asset + 44, crc32_bytes(directory, directory_bytes));
    write_le32(asset + 60, crc32_bytes(asset, 60));
    return file_bytes;
}

static size_t decoder_calls;

static int copy_decoder(const pvr_chunk_asset_section_t *section,
                        void *destination, size_t destination_bytes,
                        void *data) {
    (void)data;
    assert(destination_bytes == section->decoded_bytes);
    ++decoder_calls;
    memcpy(destination, section->stored_data, destination_bytes);
    return 0;
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
    pvr_chunk_model_view_t model_views[2] = { 0 };
    const pvr_chunk_model_view_t *models[] = {
        &model_views[0], &model_views[1]
    };
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
    assert(nodes[0].model == &model_views[0] &&
           nodes[1].model == NULL && nodes[2].model == &model_views[1]);
    assert(nodes[1].flags == PVR_CHUNK_NODE_HIDDEN &&
           nodes[2].parent_index == 1 && nodes[2].user_data == NULL &&
           nodes[2].flags == (PVR_CHUNK_NODE_SUPPRESS_TRANSLATION |
                              PVR_CHUNK_NODE_PRUNE_CHILDREN));
    memset(nodes, 0, sizeof(nodes));
    assert(pvr_chunk_scene_hierarchy_bind_models(
               &view, model_views, 2, nodes, 3, &hierarchy) == 0);
    assert(nodes[0].model == &model_views[0] &&
           nodes[1].model == NULL && nodes[2].model == &model_views[1]);

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

static void test_draw_schedule_canonicalization(void) {
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    static const float translated[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        9.0f, 8.0f, 7.0f, 1.0f
    };
    const pvr_scene_ir_draw_command_t commands[] = {
        { PVR_SCENE_IR_CAPTURE_MODEL, 0, 5, 3 },
        { PVR_SCENE_IR_DRAW_MODEL, 1, 6,
          PVR_SCENE_IR_CAPTURE_NONE },
        { PVR_SCENE_IR_DRAW_CAPTURED_MODEL, UINT32_MAX, UINT32_MAX, 3 },
        { PVR_SCENE_IR_DRAW_MODEL, 2, 2,
          PVR_SCENE_IR_CAPTURE_NONE },
        { PVR_SCENE_IR_CAPTURE_MODEL, 2, 2, 3 },
        { PVR_SCENE_IR_DRAW_CAPTURED_MODEL, UINT32_MAX, UINT32_MAX, 3 }
    };
    const pvr_scene_ir_draw_command_t invalid = {
        PVR_SCENE_IR_DRAW_CAPTURED_MODEL, UINT32_MAX, UINT32_MAX, 99
    };
    pvr_scene_ir_t source = { 0 };
    pvr_scene_ir_t canonical = { 0 };
    pvr_scene_ir_t rejected = { 0 };
    pvr_chunk_scene_hierarchy_view_t hierarchy;
    pvr_chunk_animation_section_view_t animation_view;
    pvr_chunk_animation_section_transform_t animation_transform;
    pvr_chunk_morph_animation_section_view_t morph_view;
    pvr_chunk_morph_animation_section_binding_t morph_decoded;
    static const anim_scalar_key_t morph_keys[] = {
        { 0.0f, 0.0f }, { 1.0f, 1.0f }
    };
    const anim_track_view_t morph_track = {
        {
            ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
            morph_keys, 2, sizeof(morph_keys[0])
        },
        0.0f, 1.0f
    };
    const pvr_chunk_shape_channel_t morph_channel = {
        &morph_track, 0.0f
    };
    pvr_chunk_morph_animation_binding_t morph_binding = {
        2, 2, &morph_channel, 1
    };
    const pvr_chunk_morph_animation_t morph_animation = {
        &morph_binding, 1, 0.0f, 1.0f
    };
    anim_transform_tracks_t transforms[3] = { 0 };
    anim_visibility_tracks_t visibility[3] = { 0 };
    anim_clip_view_t clip = { 0 };
    uint8_t *serialized = NULL;
    uint8_t *animation = NULL;
    uint8_t *morph = NULL;
    size_t serialized_bytes = 0;
    size_t animation_bytes = 0;
    size_t morph_bytes = 0;
    size_t index;

    assert(pvr_scene_ir_add_node_flags(
        &source, UINT32_MAX, 0, PVR_CHUNK_NODE_PRUNE_CHILDREN,
        identity) == 0);
    assert(pvr_scene_ir_add_node(&source, 0, 1, translated) == 0);
    assert(pvr_scene_ir_add_node_flags(
        &source, UINT32_MAX, 2, PVR_CHUNK_NODE_HIDDEN,
        translated) == 0);
    assert(pvr_scene_ir_canonicalize_draw_schedule(
        &source, commands, sizeof(commands) / sizeof(commands[0]),
        &canonical) == 0);

    assert(canonical.node_count == 6);
    assert(canonical.nodes[0].model_ordinal ==
               PVR_CHUNK_SCENE_MODEL_NONE &&
           canonical.nodes[1].model_ordinal ==
               PVR_CHUNK_SCENE_MODEL_NONE &&
           canonical.nodes[2].model_ordinal ==
               PVR_CHUNK_SCENE_MODEL_NONE);
    assert(!(canonical.nodes[0].flags &
             PVR_CHUNK_NODE_PRUNE_CHILDREN));
    assert(canonical.nodes[3].parent_index == 0 &&
           canonical.nodes[3].model_ordinal == 5 &&
           canonical.nodes[3].local_transform[0] == 1.0f &&
           canonical.nodes[3].local_transform[12] == 0.0f);
    assert(canonical.nodes[4].parent_index == 2 &&
           canonical.nodes[4].model_ordinal == 2 &&
           canonical.nodes[4].flags == PVR_CHUNK_NODE_HIDDEN);
    assert(canonical.nodes[5].parent_index == 2 &&
           canonical.nodes[5].model_ordinal == 2 &&
           canonical.nodes[5].flags == PVR_CHUNK_NODE_HIDDEN);

    assert(pvr_scene_ir_serialize_hierarchy(
        &canonical, &serialized, &serialized_bytes) == 0);
    assert(pvr_chunk_scene_hierarchy_open(
        serialized, serialized_bytes, &hierarchy) == 0);
    assert(hierarchy.node_count == canonical.node_count);

    for(index = 0; index < 3; ++index) {
        transforms[index].fallback.rotation.w = 1.0f;
        transforms[index].fallback.scale.x = 1.0f;
        transforms[index].fallback.scale.y = 1.0f;
        transforms[index].fallback.scale.z = 1.0f;
        transforms[index].rotation_mode = ANIM_ROTATION_QUATERNION;
        visibility[index].fallback = index == 1;
    }
    transforms[1].fallback.translation.x = 4.0f;
    clip.clip.transforms = transforms;
    clip.clip.transform_count = 3;
    clip.clip.start_time = 0.0f;
    clip.clip.end_time = 1.0f;
    clip.clip.visibility = visibility;
    assert(pvr_scene_ir_serialize_animation_for_scene(
        &clip, &canonical, 3, &animation, &animation_bytes) == 0);
    assert(pvr_chunk_animation_section_open(
        animation, animation_bytes, &animation_view) == 0);
    assert(animation_view.transform_count == canonical.node_count);
    assert(pvr_chunk_animation_section_transform_get(
        &animation_view, 3, &animation_transform) == 0);
    assert(animation_transform.fallback.rotation.w == 1.0f &&
           animation_transform.fallback.scale.x == 1.0f &&
           animation_transform.fallback.translation.x == 0.0f &&
           animation_transform.fallback_visible == 0);
    assert(pvr_chunk_animation_section_transform_get(
        &animation_view, 4, &animation_transform) == 0);
    assert(animation_transform.fallback_visible == 0);
    assert(pvr_chunk_animation_section_transform_get(
        &animation_view, 5, &animation_transform) == 0);
    assert(animation_transform.fallback_visible == 0);

    assert(pvr_scene_ir_serialize_morph_animation_for_scene(
        &morph_animation, &canonical, 3, &morph, &morph_bytes) == 0);
    assert(pvr_chunk_morph_animation_section_open(
        morph, morph_bytes, &morph_view) == 0);
    assert(morph_view.binding_count == 2);
    assert(pvr_chunk_morph_animation_section_binding_get(
        &morph_view, 0, &morph_decoded) == 0);
    assert(morph_decoded.node_index == 4 &&
           morph_decoded.model_ordinal == 2 &&
           morph_decoded.channel_count == 1);
    assert(pvr_chunk_morph_animation_section_binding_get(
        &morph_view, 1, &morph_decoded) == 0);
    assert(morph_decoded.node_index == 5 &&
           morph_decoded.model_ordinal == 2 &&
           morph_decoded.channel_count == 1);
    free(morph);
    morph = NULL;

    morph_binding.model_ordinal = 3;
    morph_bytes = 99;
    assert(pvr_scene_ir_serialize_morph_animation_for_scene(
        &morph_animation, &canonical, 3, &morph, &morph_bytes) == 0);
    assert(morph == NULL && morph_bytes == 0);
    morph_binding.model_ordinal = 2;
    morph_binding.node_index = 1;
    morph_bytes = 99;
    assert(pvr_scene_ir_serialize_morph_animation_for_scene(
        &morph_animation, &canonical, 3, &morph, &morph_bytes) == 0);
    assert(morph == NULL && morph_bytes == 0);
    morph_binding.channels = NULL;
    errno = 0;
    assert(pvr_scene_ir_serialize_morph_animation_for_scene(
        &morph_animation, &canonical, 3, &morph, &morph_bytes) == -1);
    assert(errno == EINVAL && morph == NULL && morph_bytes == 0);
    morph_binding.channels = &morph_channel;
    morph_binding.node_index = 3;
    errno = 0;
    assert(pvr_scene_ir_serialize_morph_animation_for_scene(
        &morph_animation, &canonical, 3, &morph, &morph_bytes) == -1);
    assert(errno == EILSEQ && morph == NULL && morph_bytes == 0);
    morph_binding.node_index = 2;

    free(animation);
    animation = NULL;
    canonical.nodes[4].parent_index = 3;
    errno = 0;
    assert(pvr_scene_ir_serialize_animation_for_scene(
        &clip, &canonical, 3, &animation, &animation_bytes) == -1);
    assert(errno == EILSEQ && animation == NULL && animation_bytes == 0);
    canonical.nodes[4].parent_index = 2;

    errno = 0;
    assert(pvr_scene_ir_canonicalize_draw_schedule(
        &source, &invalid, 1, &rejected) == -1);
    assert(errno == EILSEQ && rejected.nodes == NULL &&
           rejected.node_count == 0 && rejected.node_capacity == 0);
    errno = 0;
    assert(pvr_scene_ir_canonicalize_draw_schedule(
        &source, commands, 1, &canonical) == -1);
    assert(errno == EBUSY && canonical.node_count == 6);

    free(animation);
    free(morph);
    free(serialized);
    pvr_scene_ir_free(&rejected);
    pvr_scene_ir_free(&canonical);
    pvr_scene_ir_free(&source);
}

static void test_scene_asset(void) {
    static const float child_transform[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        3.0f, 0.0f, 0.0f, 1.0f
    };
    pvr_chunk_model_table_record_t records[2];
    pvr_scene_ir_t scene = { 0 };
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_scene_asset_view_t scene_view;
    pvr_chunk_scene_asset_workspace_requirements_t requirements;
    pvr_chunk_model_view_t models[2];
    pvr_chunk_hierarchy_node_t nodes[2];
    pvr_chunk_hierarchy_t hierarchy;
    alignas(32) uint8_t asset[4096];
    alignas(32) uint8_t workspace[sizeof(scene_vertices1)];
    uint8_t *table = NULL;
    uint8_t *hierarchy_bytes = NULL;
    size_t table_bytes = 0;
    size_t hierarchy_size = 0;
    size_t asset_bytes;

    init_model_record(&records[0], 0, 1.0f, 2.0f);
    init_model_record(&records[1], 1, 3.0f, 4.0f);
    records[1].vertex_ordinal = 0;
    assert(pvr_scene_ir_serialize_model_table(
               records, 2, &table, &table_bytes) == 0);
    assert(pvr_scene_ir_add_root_model(&scene, 0) == 0);
    assert(pvr_scene_ir_add_node(
               &scene, 0, 1, child_transform) == 0);
    assert(pvr_scene_ir_serialize_hierarchy(
               &scene, &hierarchy_bytes, &hierarchy_size) == 0);
    asset_bytes = build_scene_asset(
        asset, sizeof(asset), table, table_bytes,
        hierarchy_bytes, hierarchy_size, 1);

    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_scene_asset_open(&asset_view, &scene_view) == 0);
    assert(scene_view.model_count == 2 && scene_view.node_count == 2);
    assert(pvr_chunk_scene_asset_workspace_query(
               &scene_view, &requirements) == 0);
    assert(requirements.alignment == PVR_CHUNK_ASSET_ALIGNMENT &&
           requirements.bytes == sizeof(scene_vertices1));
    decoder_calls = 0;
    assert(pvr_chunk_scene_asset_load(
               &scene_view, copy_decoder, NULL,
               workspace, sizeof(workspace), models, 2, nodes, 2,
               &hierarchy) == 0);
    assert(hierarchy.nodes == nodes && hierarchy.node_count == 2);
    assert(nodes[0].model == &models[0] &&
           nodes[1].model == &models[1] && nodes[1].parent_index == 0);
    assert(models[0].model.center[0] == 1.0f &&
           models[0].model.radius == 2.0f &&
           models[1].model.center[0] == 3.0f &&
           models[1].model.radius == 4.0f);
    assert(decoder_calls == 1);
    assert(models[0].model.vertex_words == models[1].model.vertex_words);
    memset(models, 0xa5, sizeof(models));
    memset(nodes, 0xa5, sizeof(nodes));
    memset(&hierarchy, 0xa5, sizeof(hierarchy));
    assert(pvr_chunk_scene_asset_load(
               &scene_view, NULL, NULL, workspace, sizeof(workspace),
               models, 2, nodes, 2, &hierarchy) == -1);
    assert(errno == ENOTSUP && hierarchy.nodes == NULL &&
           hierarchy.node_count == 0);
    {
        const uint8_t *model_bytes = (const uint8_t *)models;
        const uint8_t *node_bytes = (const uint8_t *)nodes;
        size_t index;

        for(index = 0; index < sizeof(models); ++index)
            assert(model_bytes[index] == 0);
        for(index = 0; index < sizeof(nodes); ++index)
            assert(node_bytes[index] == 0);
    }

    {
        uint8_t *directory = asset +
            PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
        uint8_t *polygon_descriptor = directory +
            PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
        size_t polygon_offset = read_le32(polygon_descriptor + 8);

        assert(sizeof(deferred_scene_polygons) == sizeof(scene_polygons));
        memcpy(asset + polygon_offset, deferred_scene_polygons,
               sizeof(deferred_scene_polygons));
        write_le32(polygon_descriptor + 20,
                   crc32_bytes(deferred_scene_polygons,
                               sizeof(deferred_scene_polygons)));
        write_le32(asset + 44,
                   crc32_bytes(directory,
                               6u * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES));
        write_le32(asset + 60, crc32_bytes(asset, 60));
    }
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_scene_asset_open(&asset_view, &scene_view) == 0);
    memset(models, 0xa5, sizeof(models));
    memset(nodes, 0xa5, sizeof(nodes));
    memset(&hierarchy, 0xa5, sizeof(hierarchy));
    assert(pvr_chunk_scene_asset_load(
               &scene_view, copy_decoder, NULL,
               workspace, sizeof(workspace), models, 2, nodes, 2,
               &hierarchy) == -1);
    assert(errno == ENOTSUP && hierarchy.nodes == NULL &&
           hierarchy.node_count == 0);
    {
        const uint8_t *model_bytes = (const uint8_t *)models;
        const uint8_t *node_bytes = (const uint8_t *)nodes;
        size_t index;

        for(index = 0; index < sizeof(models); ++index)
            assert(model_bytes[index] == 0);
        for(index = 0; index < sizeof(nodes); ++index)
            assert(node_bytes[index] == 0);
    }

    scene.nodes[1].model_ordinal = 2;
    free(hierarchy_bytes);
    hierarchy_bytes = NULL;
    assert(pvr_scene_ir_serialize_hierarchy(
               &scene, &hierarchy_bytes, &hierarchy_size) == 0);
    asset_bytes = build_scene_asset(
        asset, sizeof(asset), table, table_bytes,
        hierarchy_bytes, hierarchy_size, 1);
    assert(pvr_chunk_asset_open(asset, asset_bytes, &asset_view) == 0);
    assert(pvr_chunk_scene_asset_open(&asset_view, &scene_view) == -1);
    assert(errno == EILSEQ);

    free(table);
    free(hierarchy_bytes);
    pvr_scene_ir_free(&scene);
}

int main(void) {
    test_round_trip();
    test_rejections();
    test_ir_rejections();
    test_draw_schedule_canonicalization();
    test_scene_asset();
    puts("pvr chunk scene tests passed");
    return 0;
}
