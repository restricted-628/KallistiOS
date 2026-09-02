/* KallistiOS ##version##

   Host-side compact animation section tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_animation_asset.h>
#include <dc/pvr_chunk_animation_catalog.h>
#include <dc/pvr_chunk_morph_animation_asset.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checked matrix apply wrappers are not exercised by this host-only suite. */
#ifndef __DREAMCAST__
void mat_apply(const matrix_t *matrix) {
    (void)matrix;
}
#endif

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

static void store_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint32_t load_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void store_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    store_le32(bytes, word);
}

static void refresh_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 52, crc32_bytes(
        bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES,
        size - PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
}

static uint8_t *animation_legacy_copy(const uint8_t *source,
                                      size_t source_size,
                                      size_t *legacy_size) {
    uint32_t transform_bytes = load_le32(source + 32);
    uint32_t track_bytes = load_le32(source + 36);
    uint32_t key_count = load_le32(source + 20);
    size_t prefix = PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                    transform_bytes + track_bytes;
    uint8_t *copy;
    size_t key;

    assert(source_size == prefix +
           (size_t)key_count * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES);
    *legacy_size = prefix +
        (size_t)key_count * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES_V2;
    copy = calloc(1, *legacy_size);
    assert(copy);
    memcpy(copy, source, prefix);
    for(key = 0; key < key_count; ++key) {
        memcpy(copy + prefix + key *
                   PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES_V2,
               source + prefix + key *
                   PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES,
               20);
    }
    store_le32(copy + 8, (uint32_t)*legacy_size);
    store_le16(copy + 28, PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES_V2);
    store_le32(copy + 40,
               key_count * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES_V2);
    refresh_crc(copy, *legacy_size);
    return copy;
}

static void refresh_morph_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 36, crc32_bytes(
        bytes + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES,
        size - PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES));
    store_le32(bytes + 40, crc32_bytes(bytes, 40));
}

static uint8_t *morph_animation_legacy_copy(
    const uint8_t *source, size_t source_size, size_t *legacy_size) {
    uint32_t binding_count = load_le32(source + 12);
    uint32_t channel_count = load_le32(source + 16);
    uint32_t track_count = load_le32(source + 20);
    uint32_t key_count = load_le32(source + 24);
    size_t prefix = PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES +
                    (size_t)binding_count *
                        PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES +
                    (size_t)channel_count *
                        PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES +
                    (size_t)track_count *
                        PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES;
    uint8_t *copy;
    size_t key;

    assert(source_size == prefix +
           (size_t)key_count * PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES);
    *legacy_size = prefix +
        (size_t)key_count * PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES_V1;
    copy = calloc(1, *legacy_size);
    assert(copy);
    memcpy(copy, source, prefix);
    for(key = 0; key < key_count; ++key) {
        memcpy(copy + prefix + key *
                   PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES_V1,
               source + prefix + key *
                   PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES,
               PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES_V1);
    }
    store_le16(copy + 4, PVR_CHUNK_MORPH_ANIMATION_VERSION_1);
    store_le32(copy + 8, (uint32_t)*legacy_size);
    refresh_morph_crc(copy, *legacy_size);
    return copy;
}

static void refresh_catalog_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 28, crc32_bytes(
        bytes + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES,
        size - PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
}

static anim_track_view_t open_track(anim_value_kind_t kind,
                                    anim_interpolation_t interpolation,
                                    const void *keys, size_t key_count,
                                    size_t stride) {
    anim_track_t source = {
        kind, interpolation, keys, key_count, stride
    };
    anim_track_view_t view;

    assert(anim_track_open(&source, &view) == 0);
    return view;
}

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00003f;
}

static void store_asset_section(uint8_t *descriptor, uint32_t type,
                                uint32_t offset, uint32_t bytes,
                                uint16_t alignment,
                                const uint8_t *payload) {
    store_le32(descriptor, type);
    store_le32(descriptor + 8, offset);
    store_le32(descriptor + 12, bytes);
    store_le32(descriptor + 16, bytes);
    store_le32(descriptor + 20, crc32_bytes(payload, bytes));
    store_le16(descriptor + 30, alignment);
}

static uint8_t *build_catalog_asset(size_t *size_out) {
    enum {
        SECTION_COUNT = 6,
        DIRECTORY_BYTES = SECTION_COUNT *
            PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        VERTEX_OFFSET = 256,
        POLYGON_OFFSET = 288,
        ANIMATION0_OFFSET = 320,
        MORPH0_OFFSET = 352,
        ANIMATION1_OFFSET = 384,
        MORPH1_OFFSET = 416,
        FILE_BYTES = 417
    };
    uint8_t *bytes = calloc(1, FILE_BYTES);
    uint8_t *directory;

    assert(bytes && size_out);
    directory = bytes + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
    store_asset_section(
        directory, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
        VERTEX_OFFSET, 4, 4, bytes + VERTEX_OFFSET);
    store_asset_section(
        directory + PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
        POLYGON_OFFSET, 2, 2, bytes + POLYGON_OFFSET);
    store_asset_section(
        directory + 2 * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_ANIMATION,
        ANIMATION0_OFFSET, 1, 4, bytes + ANIMATION0_OFFSET);
    store_asset_section(
        directory + 3 * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
        MORPH0_OFFSET, 1, 4, bytes + MORPH0_OFFSET);
    store_asset_section(
        directory + 4 * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_ANIMATION,
        ANIMATION1_OFFSET, 1, 4, bytes + ANIMATION1_OFFSET);
    store_asset_section(
        directory + 5 * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
        MORPH1_OFFSET, 1, 4, bytes + MORPH1_OFFSET);
    store_le32(bytes, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    store_le32(bytes + 8, FILE_BYTES);
    store_le32(bytes + 32, SECTION_COUNT);
    store_le32(bytes + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    store_le32(bytes + 40, DIRECTORY_BYTES);
    store_le32(bytes + 44, crc32_bytes(directory, DIRECTORY_BYTES));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
    *size_out = FILE_BYTES;
    return bytes;
}

static void test_animation_catalog(void) {
    const pvr_scene_ir_animation_clip_t source[] = {
        {
            "walk", 0,
            PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE,
            0.0f, 1.0f
        },
        {
            NULL, PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE, 0,
            0.0f, 2.0f
        },
        { "attack", 1, 1, 0.25f, 0.75f }
    };
    pvr_chunk_animation_catalog_view_t view;
    pvr_chunk_animation_catalog_clip_t clip;
    uint8_t *serialized = NULL;
    uint8_t *corrupt;
    uint8_t *asset_bytes;
    size_t serialized_bytes = 0;
    size_t asset_size = 0;
    size_t index = SIZE_MAX;
    pvr_chunk_asset_view_t asset;

    assert(pvr_scene_ir_serialize_animation_catalog(
        source, 3, &serialized, &serialized_bytes) == 0);
    assert(serialized_bytes ==
           PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES +
           3 * PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES + 10);
    assert(pvr_chunk_animation_catalog_open(
        serialized, serialized_bytes, &view) == 0);
    assert(view.clip_count == 3 && view.string_bytes == 10);
    assert(pvr_chunk_animation_catalog_clip_get(&view, 0, &clip) == 0);
    assert(clip.transform_ordinal == 0 &&
           clip.morph_ordinal ==
               PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
           clip.name_bytes == 4 && !memcmp(clip.name, "walk", 4) &&
           clip.start_time == 0.0f && clip.end_time == 1.0f);
    assert(pvr_chunk_animation_catalog_clip_get(&view, 1, &clip) == 0);
    assert(clip.transform_ordinal ==
               PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
           clip.morph_ordinal == 0 && !clip.name_bytes &&
           clip.start_time == 0.0f && clip.end_time == 2.0f);
    assert(pvr_chunk_animation_catalog_find(
        &view, "attack", 6, &index, &clip) == 0);
    assert(index == 2 && clip.transform_ordinal == 1 &&
           clip.morph_ordinal == 1 && clip.start_time == 0.25f &&
           clip.end_time == 0.75f);
    errno = 0;
    assert(pvr_chunk_animation_catalog_find(
        &view, "missing", 7, NULL, &clip) == -1);
    assert(errno == ENOENT);

    asset_bytes = build_catalog_asset(&asset_size);
    assert(pvr_chunk_asset_open(asset_bytes, asset_size, &asset) == 0);
    assert(pvr_chunk_animation_catalog_validate_asset(
        &view, &asset) == 0);
    errno = 0;
    assert(pvr_chunk_animation_catalog_clip_get(&view, 3, &clip) == -1);
    assert(errno == ENOENT);

    {
        pvr_scene_ir_animation_clip_t invalid[2] = {
            source[0], source[0]
        };
        uint8_t *unchanged = (uint8_t *)(uintptr_t)1;
        size_t unchanged_bytes = 1;

        errno = 0;
        assert(pvr_scene_ir_serialize_animation_catalog(
            invalid, 2, &unchanged, &unchanged_bytes) == -1);
        assert(errno == EEXIST && unchanged == NULL &&
               unchanged_bytes == 0);
        invalid[1].name = "idle";
        invalid[1].transform_ordinal =
            PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
        invalid[1].morph_ordinal =
            PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
        errno = 0;
        assert(pvr_scene_ir_serialize_animation_catalog(
            invalid, 2, &unchanged, &unchanged_bytes) == -1);
        assert(errno == EINVAL && unchanged == NULL &&
               unchanged_bytes == 0);
    }

    corrupt = malloc(serialized_bytes);
    assert(corrupt);
    memcpy(corrupt, serialized, serialized_bytes);
    corrupt[serialized_bytes - 1u] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_chunk_animation_catalog_open(
        corrupt, serialized_bytes, &view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES +
                   PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES + 8,
               1);
    refresh_catalog_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_animation_catalog_open(
        corrupt, serialized_bytes, &view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES, 2);
    refresh_catalog_crc(corrupt, serialized_bytes);
    assert(pvr_chunk_animation_catalog_open(
        corrupt, serialized_bytes, &view) == 0);
    errno = 0;
    assert(pvr_chunk_animation_catalog_validate_asset(
        &view, &asset) == -1);
    assert(errno == EILSEQ);

    free(asset_bytes);
    free(corrupt);
    free(serialized);
}

static void test_morph_animation(void) {
    const anim_scalar_key_t source_keys[] = {
        { 0.0f, 0.0f }, { 2.0f, 1.0f },
        { 0.0f, 0.25f }, { 2.0f, 0.75f }
    };
    anim_track_view_t source_tracks[2];
    pvr_chunk_shape_channel_t source_channels[2];
    pvr_chunk_morph_animation_binding_t source_binding;
    pvr_chunk_morph_animation_t source_animation;
    pvr_chunk_morph_animation_section_view_t section;
    pvr_chunk_morph_animation_section_binding_t decoded_binding;
    pvr_chunk_morph_animation_section_track_t decoded_track;
    anim_scalar_hermite_key_t decoded_keys[4];
    anim_track_view_t decoded_tracks[2];
    pvr_chunk_shape_channel_t decoded_channels[2];
    pvr_chunk_morph_animation_binding_t decoded_bindings[1];
    pvr_chunk_morph_animation_t decoded_animation;
    uint8_t *serialized = NULL;
    uint8_t *corrupt = NULL;
    size_t serialized_bytes = 0;
    float sampled;

    source_tracks[0] = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        source_keys, 2, sizeof(source_keys[0]));
    source_tracks[1] = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_STEP,
        source_keys + 2, 2, sizeof(source_keys[0]));
    source_channels[0].weight = source_tracks + 0;
    source_channels[0].fallback_weight = 0.0f;
    source_channels[1].weight = source_tracks + 1;
    source_channels[1].fallback_weight = 0.25f;
    source_binding.node_index = 3;
    source_binding.model_ordinal = 1;
    source_binding.channels = source_channels;
    source_binding.channel_count = 2;
    source_animation.bindings = &source_binding;
    source_animation.binding_count = 1;
    source_animation.start_time = 0.0f;
    source_animation.end_time = 2.0f;

    assert(pvr_scene_ir_serialize_morph_animation(
        &source_animation, &serialized, &serialized_bytes) == 0);
    assert(serialized_bytes ==
           PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES +
           PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES +
           2 * PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES +
           2 * PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES +
           4 * PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES);
    assert(pvr_chunk_morph_animation_section_open(
        serialized, serialized_bytes, &section) == 0);
    assert(section.binding_count == 1 && section.channel_count == 2 &&
           section.track_count == 2 && section.key_count == 4 &&
           section.start_time == 0.0f && section.end_time == 2.0f);
    assert(pvr_chunk_morph_animation_section_binding_get(
        &section, 0, &decoded_binding) == 0);
    assert(decoded_binding.node_index == 3 &&
           decoded_binding.model_ordinal == 1 &&
           decoded_binding.first_channel == 0 &&
           decoded_binding.channel_count == 2);
    assert(pvr_chunk_morph_animation_section_track_get(
        &section, 1, &decoded_track) == 0);
    assert(decoded_track.interpolation == ANIM_INTERPOLATION_STEP &&
           decoded_track.first_key == 2 && decoded_track.key_count == 2);

    memset(&decoded_animation, 0x5a, sizeof(decoded_animation));
    {
        pvr_chunk_morph_animation_t unchanged = decoded_animation;

        errno = 0;
        assert(pvr_chunk_morph_animation_section_materialize(
            &section, decoded_keys, 3, decoded_tracks, 2,
            decoded_channels, 2, decoded_bindings, 1,
            &decoded_animation) == -1);
        assert(errno == ENOSPC &&
               !memcmp(&decoded_animation, &unchanged,
                       sizeof(decoded_animation)));
    }
    assert(pvr_chunk_morph_animation_section_materialize(
        &section, decoded_keys, 4, decoded_tracks, 2,
        decoded_channels, 2, decoded_bindings, 1,
        &decoded_animation) == 0);
    assert(decoded_animation.binding_count == 1 &&
           decoded_bindings[0].channels == decoded_channels &&
           decoded_channels[1].weight == decoded_tracks + 1);
    assert(anim_track_sample_scalar(decoded_channels[0].weight, 1.0f,
                                    &sampled, NULL) == 0);
    assert(close_enough(sampled, 0.5f));

    {
        size_t legacy_bytes;
        uint8_t *legacy = morph_animation_legacy_copy(
            serialized, serialized_bytes, &legacy_bytes);

        assert(pvr_chunk_morph_animation_section_open(
            legacy, legacy_bytes, &section) == 0);
        assert(section.version == PVR_CHUNK_MORPH_ANIMATION_VERSION_1 &&
               section.key_bytes ==
                   PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES_V1);
        free(legacy);
    }

    corrupt = malloc(serialized_bytes);
    assert(corrupt);
    memcpy(corrupt, serialized, serialized_bytes);
    corrupt[serialized_bytes - 1u] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_chunk_morph_animation_section_open(
        corrupt, serialized_bytes, &section) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES +
                   PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES + 4,
               UINT32_C(0x7fc00000));
    refresh_morph_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_morph_animation_section_open(
        corrupt, serialized_bytes, &section) == -1);
    assert(errno == EILSEQ);

    free(corrupt);
    free(serialized);
}

static void test_hermite_assets(void) {
    anim_vector_hermite_key_t transform_keys[2] = { 0 };
    anim_scalar_hermite_key_t morph_keys[2] = { 0 };
    anim_track_view_t transform_track;
    anim_transform_tracks_t transform = { 0 };
    anim_visibility_tracks_t visibility = { 0 };
    anim_clip_t clip;
    anim_clip_view_t clip_view;
    pvr_chunk_animation_section_view_t transform_section;
    pvr_chunk_animation_key_t decoded_transform_keys[2];
    anim_track_view_t decoded_transform_track;
    anim_transform_tracks_t decoded_transform;
    anim_visibility_tracks_t decoded_visibility;
    anim_clip_view_t decoded_clip;
    anim_transform_t sampled_transform;
    anim_pose_result_t pose_result;
    anim_track_view_t morph_track;
    pvr_chunk_shape_channel_t morph_channel;
    pvr_chunk_morph_animation_binding_t morph_binding;
    pvr_chunk_morph_animation_t morph_animation;
    pvr_chunk_morph_animation_section_view_t morph_section;
    anim_scalar_hermite_key_t decoded_morph_keys[2];
    anim_track_view_t decoded_morph_track;
    pvr_chunk_shape_channel_t decoded_morph_channel;
    pvr_chunk_morph_animation_binding_t decoded_morph_binding;
    pvr_chunk_morph_animation_t decoded_morph_animation;
    uint8_t *transform_bytes = NULL;
    uint8_t *morph_bytes = NULL;
    size_t transform_size = 0;
    size_t morph_size = 0;
    float sampled_weight;

    transform_keys[0].time = 0.0f;
    transform_keys[0].value.w = 1.0f;
    transform_keys[0].out_tangent.x = 2.0f;
    transform_keys[1].time = 2.0f;
    transform_keys[1].value.x = 4.0f;
    transform_keys[1].value.w = 1.0f;
    transform_track = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_CUBIC_HERMITE,
        transform_keys, 2, sizeof(transform_keys[0]));
    transform.translation = &transform_track;
    transform.fallback.translation.w = 1.0f;
    transform.fallback.rotation.w = 1.0f;
    transform.fallback.scale = (vector_t){ 1.0f, 1.0f, 1.0f, 0.0f };
    transform.rotation_mode = ANIM_ROTATION_QUATERNION;
    visibility.fallback = true;
    clip.transforms = &transform;
    clip.transform_count = 1;
    clip.start_time = 0.0f;
    clip.end_time = 2.0f;
    clip.visibility = &visibility;
    assert(anim_clip_open(&clip, &clip_view) == 0);
    assert(pvr_scene_ir_serialize_animation(
        &clip_view, &transform_bytes, &transform_size) == 0);
    assert(pvr_chunk_animation_section_open(
        transform_bytes, transform_size, &transform_section) == 0);
    assert(transform_section.version ==
               PVR_CHUNK_ANIMATION_SECTION_VERSION &&
           transform_section.key_bytes ==
               PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES);
    assert(pvr_chunk_animation_section_materialize(
        &transform_section, decoded_transform_keys, 2,
        &decoded_transform_track, 1, &decoded_transform, 1,
        &decoded_visibility, 1, &decoded_clip) == 0);
    assert(anim_clip_sample(&decoded_clip, 1.0f, &sampled_transform, 1,
                            &pose_result) == 0);
    assert(pose_result.sampled_transforms == 1 &&
           close_enough(sampled_transform.translation.x, 2.5f));

    morph_keys[0].time = 0.0f;
    morph_keys[0].out_tangent = 2.0f;
    morph_keys[1].time = 2.0f;
    morph_keys[1].value = 4.0f;
    morph_track = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_CUBIC_HERMITE,
        morph_keys, 2, sizeof(morph_keys[0]));
    morph_channel.weight = &morph_track;
    morph_channel.fallback_weight = 0.0f;
    morph_binding.node_index = 0;
    morph_binding.model_ordinal = 0;
    morph_binding.channels = &morph_channel;
    morph_binding.channel_count = 1;
    morph_animation.bindings = &morph_binding;
    morph_animation.binding_count = 1;
    morph_animation.start_time = 0.0f;
    morph_animation.end_time = 2.0f;
    assert(pvr_scene_ir_serialize_morph_animation(
        &morph_animation, &morph_bytes, &morph_size) == 0);
    assert(pvr_chunk_morph_animation_section_open(
        morph_bytes, morph_size, &morph_section) == 0);
    assert(morph_section.version == PVR_CHUNK_MORPH_ANIMATION_VERSION &&
           morph_section.key_bytes ==
               PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES);
    assert(pvr_chunk_morph_animation_section_materialize(
        &morph_section, decoded_morph_keys, 2, &decoded_morph_track, 1,
        &decoded_morph_channel, 1, &decoded_morph_binding, 1,
        &decoded_morph_animation) == 0);
    assert(anim_track_sample_scalar(decoded_morph_channel.weight, 1.0f,
                                    &sampled_weight, NULL) == 0);
    assert(close_enough(sampled_weight, 2.5f));

    free(morph_bytes);
    free(transform_bytes);
}

int main(void) {
    const anim_vector_key_t translation_keys[] = {
        { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { 1.0f, { 10.0f, 2.0f, -4.0f, 1.0f } }
    };
    const anim_vector_key_t rotation_keys[] = {
        { 0.0f, { 0.0f, 0.0f, 6.10865238198f, 0.0f } },
        { 1.0f, { 0.0f, 0.0f, 0.17453292520f, 0.0f } }
    };
    const anim_vector_key_t scale_keys[] = {
        { 0.0f, { 2.0f, 3.0f, 4.0f, 0.0f } }
    };
    const anim_boolean_key_t visibility_keys[] = {
        { 0.0f, 1u }, { 1.0f, 0u }
    };
    anim_track_view_t source_tracks[4];
    anim_transform_tracks_t source_transform;
    anim_visibility_tracks_t source_visibility;
    anim_clip_t source_clip;
    anim_clip_view_t source_view;
    pvr_chunk_animation_section_view_t section_view;
    pvr_chunk_animation_section_transform_t section_transform;
    pvr_chunk_animation_section_track_t section_track;
    pvr_chunk_animation_key_t decoded_keys[7];
    anim_track_view_t decoded_tracks[4];
    anim_transform_tracks_t decoded_transform;
    anim_visibility_tracks_t decoded_visibility;
    anim_clip_view_t decoded_clip;
    anim_clip_view_t reopened_clip;
    anim_transform_t sampled;
    anim_pose_result_t result;
    uint8_t *serialized = NULL;
    uint8_t *fallback_serialized = NULL;
    uint8_t *corrupt = NULL;
    size_t serialized_bytes = 0;
    size_t fallback_serialized_bytes = 0;

    source_tracks[0] = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        translation_keys, 2, sizeof(translation_keys[0]));
    source_tracks[1] = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        rotation_keys, 2, sizeof(rotation_keys[0]));
    source_tracks[2] = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_STEP,
        scale_keys, 1, sizeof(scale_keys[0]));
    source_tracks[3] = open_track(
        ANIM_VALUE_BOOLEAN, ANIM_INTERPOLATION_STEP,
        visibility_keys, 2, sizeof(visibility_keys[0]));
    memset(&source_transform, 0, sizeof(source_transform));
    source_transform.translation = source_tracks + 0;
    source_transform.rotation = source_tracks + 1;
    source_transform.scale = source_tracks + 2;
    source_transform.rotation_mode = ANIM_ROTATION_EULER_ZXY;
    source_transform.fallback.translation.w = 1.0f;
    source_transform.fallback.rotation.w = 1.0f;
    source_transform.fallback.scale.x = 1.0f;
    source_transform.fallback.scale.y = 1.0f;
    source_transform.fallback.scale.z = 1.0f;
    source_visibility.visible = source_tracks + 3;
    source_visibility.fallback = true;
    source_clip.transforms = &source_transform;
    source_clip.transform_count = 1;
    source_clip.start_time = 0.0f;
    source_clip.end_time = 1.0f;
    source_clip.visibility = &source_visibility;
    assert(anim_clip_open(&source_clip, &source_view) == 0);

    assert(pvr_scene_ir_serialize_animation(
        &source_view, &serialized, &serialized_bytes) == 0);
    assert(serialized_bytes ==
           PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
           PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES +
           4 * PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES +
           7 * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES);
    assert(pvr_chunk_animation_section_open(
        serialized, serialized_bytes, &section_view) == 0);
    assert(section_view.version == PVR_CHUNK_ANIMATION_SECTION_VERSION &&
           section_view.transform_count == 1 &&
           section_view.track_count == 4 && section_view.key_count == 7 &&
           section_view.start_time == 0.0f && section_view.end_time == 1.0f);
    assert(pvr_chunk_animation_section_transform_get(
        &section_view, 0, &section_transform) == 0);
    assert(section_transform.translation_track == 0 &&
           section_transform.rotation_track == 1 &&
           section_transform.scale_track == 2 &&
           section_transform.visibility_track == 3 &&
           section_transform.fallback_visible == 1 &&
           section_transform.rotation_mode == ANIM_ROTATION_EULER_ZXY);
    assert(pvr_chunk_animation_section_track_get(
        &section_view, 3, &section_track) == 0);
    assert(section_track.kind == ANIM_VALUE_BOOLEAN &&
           section_track.interpolation == ANIM_INTERPOLATION_STEP &&
           section_track.first_key == 5 && section_track.key_count == 2);

    memset(decoded_keys, 0x5a, sizeof(decoded_keys));
    memset(decoded_tracks, 0x5a, sizeof(decoded_tracks));
    memset(&decoded_transform, 0x5a, sizeof(decoded_transform));
    memset(&decoded_visibility, 0x5a, sizeof(decoded_visibility));
    memset(&decoded_clip, 0x5a, sizeof(decoded_clip));
    {
        anim_clip_view_t unchanged = decoded_clip;

        errno = 0;
        assert(pvr_chunk_animation_section_materialize(
            &section_view, decoded_keys, 6, decoded_tracks, 4,
            &decoded_transform, 1, &decoded_visibility, 1,
            &decoded_clip) == -1);
        assert(errno == ENOSPC &&
               !memcmp(&decoded_clip, &unchanged, sizeof(decoded_clip)) &&
               decoded_keys[0].value.boolean == UINT32_C(0x5a5a5a5a));
    }
    assert(pvr_chunk_animation_section_materialize(
        &section_view, decoded_keys, 7, decoded_tracks, 4,
        &decoded_transform, 1, &decoded_visibility, 1,
        &decoded_clip) == 0);
    assert(anim_clip_open(&decoded_clip.clip, &reopened_clip) == 0);
    assert(anim_clip_sample(&reopened_clip, 0.5f, &sampled, 1,
                            &result) == 0);
    assert(result.sampled_transforms == 1 &&
           close_enough(sampled.translation.x, 5.0f) &&
           close_enough(sampled.translation.y, 1.0f) &&
           close_enough(sampled.translation.z, -2.0f) &&
           sampled.scale.x == 2.0f && sampled.scale.y == 3.0f &&
           sampled.scale.z == 4.0f &&
           close_enough(fabsf(sampled.rotation.w), 1.0f));

    {
        anim_transform_tracks_t fallback_transform = { 0 };
        anim_clip_view_t fallback_clip = { 0 };

        fallback_transform.fallback.translation.w = 1.0f;
        fallback_transform.fallback.rotation.w = 1.0f;
        fallback_transform.fallback.scale.x = 1.0f;
        fallback_transform.fallback.scale.y = 1.0f;
        fallback_transform.fallback.scale.z = 1.0f;
        fallback_clip.clip.transforms = &fallback_transform;
        fallback_clip.clip.transform_count = 1;
        fallback_clip.clip.start_time = 0.0f;
        fallback_clip.clip.end_time = 1.0f;
        assert(pvr_scene_ir_serialize_animation(
            &fallback_clip, &fallback_serialized,
            &fallback_serialized_bytes) == 0);
        assert(fallback_serialized_bytes ==
               PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
               PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES);
        assert(pvr_chunk_animation_section_open(
            fallback_serialized, fallback_serialized_bytes,
            &section_view) == 0);
        assert(section_view.track_count == 0 && section_view.key_count == 0);
        assert(pvr_chunk_animation_section_materialize(
            &section_view, NULL, 0, NULL, 0, &decoded_transform, 1,
            &decoded_visibility, 1, &decoded_clip) == 0);
        assert(decoded_transform.translation == NULL &&
               decoded_transform.rotation == NULL &&
               decoded_transform.scale == NULL &&
               decoded_transform.rotation_mode ==
                   ANIM_ROTATION_QUATERNION &&
               decoded_visibility.visible == NULL &&
               decoded_visibility.fallback);

        store_le16(fallback_serialized + 4,
                   PVR_CHUNK_ANIMATION_SECTION_VERSION_1);
        store_le16(fallback_serialized + 28,
                   PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES_V2);
        refresh_crc(fallback_serialized, fallback_serialized_bytes);
        assert(pvr_chunk_animation_section_open(
            fallback_serialized, fallback_serialized_bytes,
            &section_view) == 0);
        assert(section_view.version ==
               PVR_CHUNK_ANIMATION_SECTION_VERSION_1);
        store_le32(fallback_serialized +
                       PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + 60,
                   ANIM_ROTATION_EULER_XYZ);
        refresh_crc(fallback_serialized, fallback_serialized_bytes);
        errno = 0;
        assert(pvr_chunk_animation_section_open(
            fallback_serialized, fallback_serialized_bytes,
            &section_view) == -1);
        assert(errno == EILSEQ);
    }

    {
        size_t legacy_bytes;

        corrupt = animation_legacy_copy(serialized, serialized_bytes,
                                        &legacy_bytes);
        store_le16(corrupt + 4, PVR_CHUNK_ANIMATION_SECTION_VERSION_1);
        store_le32(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + 60,
                   ANIM_ROTATION_QUATERNION);
        store_le16(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                       PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES +
                       PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
                   ANIM_VALUE_QUATERNION);
        refresh_crc(corrupt, legacy_bytes);
        assert(pvr_chunk_animation_section_open(
            corrupt, legacy_bytes, &section_view) == 0);
        assert(section_view.version ==
               PVR_CHUNK_ANIMATION_SECTION_VERSION_1);
        free(corrupt);
    }

    corrupt = malloc(serialized_bytes);
    assert(corrupt);
    memcpy(corrupt, serialized, serialized_bytes);
    corrupt[serialized_bytes - 1u] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_chunk_animation_section_open(
        corrupt, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + 60,
               UINT32_C(99));
    refresh_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_animation_section_open(
        corrupt, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le32(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES,
               UINT32_C(99));
    refresh_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_animation_section_open(
        corrupt, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, serialized, serialized_bytes);
    store_float(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                    PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES +
                    4 * PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES +
                    PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES,
                -1.0f);
    refresh_crc(corrupt, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_animation_section_open(
        corrupt, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);

    free(corrupt);
    free(fallback_serialized);
    free(serialized);
    test_morph_animation();
    test_hermite_assets();
    test_animation_catalog();
    puts("pvr-chunk-animation-test: PASS");
    return 0;
}
