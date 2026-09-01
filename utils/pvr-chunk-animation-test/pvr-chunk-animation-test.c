/* KallistiOS ##version##

   Host-side compact animation section tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_animation_asset.h>
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

static void refresh_morph_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 36, crc32_bytes(
        bytes + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES,
        size - PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES));
    store_le32(bytes + 40, crc32_bytes(bytes, 40));
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
    anim_scalar_key_t decoded_keys[4];
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

    corrupt = malloc(serialized_bytes);
    assert(corrupt);
    memcpy(corrupt, serialized, serialized_bytes);
    store_le16(corrupt + 4, PVR_CHUNK_ANIMATION_SECTION_VERSION_1);
    store_le32(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + 60,
               ANIM_ROTATION_QUATERNION);
    store_le16(corrupt + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                   PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES +
                   PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
               ANIM_VALUE_QUATERNION);
    refresh_crc(corrupt, serialized_bytes);
    assert(pvr_chunk_animation_section_open(
        corrupt, serialized_bytes, &section_view) == 0);
    assert(section_view.version == PVR_CHUNK_ANIMATION_SECTION_VERSION_1);

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
    puts("pvr-chunk-animation-test: PASS");
    return 0;
}
