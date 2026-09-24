/* KallistiOS ##version##

   dc/pvr/pvr_chunk_morph_animation_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_morph_animation_asset.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_BINDING_COUNT_OFFSET = 12,
    HEADER_CHANNEL_COUNT_OFFSET = 16,
    HEADER_TRACK_COUNT_OFFSET = 20,
    HEADER_KEY_COUNT_OFFSET = 24,
    HEADER_START_TIME_OFFSET = 28,
    HEADER_END_TIME_OFFSET = 32,
    HEADER_PAYLOAD_CRC_OFFSET = 36,
    HEADER_CRC_OFFSET = 40,
    HEADER_CRC_BYTES = 40,
    HEADER_RESERVED_OFFSET = 44,
    BINDING_NODE_OFFSET = 0,
    BINDING_MODEL_OFFSET = 4,
    BINDING_FIRST_CHANNEL_OFFSET = 8,
    BINDING_CHANNEL_COUNT_OFFSET = 12,
    CHANNEL_TRACK_OFFSET = 0,
    CHANNEL_FALLBACK_OFFSET = 4,
    TRACK_INTERPOLATION_OFFSET = 0,
    TRACK_RESERVED0_OFFSET = 2,
    TRACK_FIRST_KEY_OFFSET = 4,
    TRACK_KEY_COUNT_OFFSET = 8,
    TRACK_RESERVED1_OFFSET = 12,
    KEY_TIME_OFFSET = 0,
    KEY_VALUE_OFFSET = 4,
    KEY_IN_TANGENT_OFFSET = 8,
    KEY_OUT_TANGENT_OFFSET = 12
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_float(const uint8_t *bytes) {
    uint32_t word = read_le32(bytes);
    float value;

    memcpy(&value, &word, sizeof(value));
    return value;
}

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

static int ranges_overlap(const void *left, size_t left_bytes,
                          const void *right, size_t right_bytes) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if(!left_bytes || !right_bytes)
        return 0;
    if(left_start > UINTPTR_MAX - left_bytes ||
       right_start > UINTPTR_MAX - right_bytes)
        return 1;
    return left_start < right_start + right_bytes &&
           right_start < left_start + left_bytes;
}

static void decode_binding(
    const uint8_t *record,
    pvr_chunk_morph_animation_section_binding_t *binding) {
    binding->node_index = read_le32(record + BINDING_NODE_OFFSET);
    binding->model_ordinal = read_le32(record + BINDING_MODEL_OFFSET);
    binding->first_channel = read_le32(
        record + BINDING_FIRST_CHANNEL_OFFSET);
    binding->channel_count = read_le32(
        record + BINDING_CHANNEL_COUNT_OFFSET);
}

static void decode_track(
    const uint8_t *record,
    pvr_chunk_morph_animation_section_track_t *track) {
    track->interpolation = (anim_interpolation_t)read_le16(
        record + TRACK_INTERPOLATION_OFFSET);
    track->first_key = read_le32(record + TRACK_FIRST_KEY_OFFSET);
    track->key_count = read_le32(record + TRACK_KEY_COUNT_OFFSET);
}

static void decode_key(const uint8_t *record,
                       anim_scalar_hermite_key_t *key) {
    memset(key, 0, sizeof(*key));
    key->time = read_float(record + KEY_TIME_OFFSET);
    key->value = read_float(record + KEY_VALUE_OFFSET);
    key->in_tangent = read_float(record + KEY_IN_TANGENT_OFFSET);
    key->out_tangent = read_float(record + KEY_OUT_TANGENT_OFFSET);
}

int pvr_chunk_morph_animation_section_open(
    const void *data, size_t size,
    pvr_chunk_morph_animation_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_morph_animation_section_view_t parsed;
    uint32_t binding_count;
    uint32_t channel_count;
    uint32_t track_count;
    uint32_t key_count;
    uint16_t version;
    uint16_t serialized_key_bytes;
    uint64_t encoded_binding_bytes;
    uint64_t encoded_channel_bytes;
    uint64_t encoded_track_bytes;
    uint64_t payload_bytes;
    size_t binding_bytes;
    size_t channel_bytes;
    size_t track_bytes;
    size_t next_channel = 0;
    size_t next_key = 0;
    size_t index;
    float observed_start = 0.0f;
    float observed_end = 0.0f;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_MORPH_ANIMATION_MAGIC ||
       read_le16(bytes + 6) != PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES ||
       read_le32(bytes + 8) != size ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    version = read_le16(bytes + 4);
    if(version != PVR_CHUNK_MORPH_ANIMATION_VERSION) {
        errno = EILSEQ;
        return -1;
    }
    serialized_key_bytes = PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES;
    for(index = HEADER_RESERVED_OFFSET;
        index < PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES; ++index) {
        if(bytes[index]) {
            errno = EILSEQ;
            return -1;
        }
    }
    binding_count = read_le32(bytes + HEADER_BINDING_COUNT_OFFSET);
    channel_count = read_le32(bytes + HEADER_CHANNEL_COUNT_OFFSET);
    track_count = read_le32(bytes + HEADER_TRACK_COUNT_OFFSET);
    key_count = read_le32(bytes + HEADER_KEY_COUNT_OFFSET);
    if(!binding_count || !channel_count || track_count != channel_count ||
       !key_count) {
        errno = EILSEQ;
        return -1;
    }
    encoded_binding_bytes = (uint64_t)binding_count *
                            PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES;
    encoded_channel_bytes = (uint64_t)channel_count *
                            PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;
    encoded_track_bytes = (uint64_t)track_count *
                          PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES;
    payload_bytes = encoded_binding_bytes + encoded_channel_bytes +
                    encoded_track_bytes +
                    (uint64_t)key_count *
                        serialized_key_bytes;
    if(payload_bytes > SIZE_MAX ||
       payload_bytes != size - PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES,
           (size_t)payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }
    binding_bytes = (size_t)encoded_binding_bytes;
    channel_bytes = (size_t)encoded_channel_bytes;
    track_bytes = (size_t)encoded_track_bytes;

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = size;
    parsed.bindings = bytes + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES;
    parsed.binding_count = binding_count;
    parsed.channels = (const uint8_t *)parsed.bindings + binding_bytes;
    parsed.channel_count = channel_count;
    parsed.tracks = (const uint8_t *)parsed.channels + channel_bytes;
    parsed.track_count = track_count;
    parsed.keys = (const uint8_t *)parsed.tracks + track_bytes;
    parsed.key_count = key_count;
    parsed.start_time = read_float(bytes + HEADER_START_TIME_OFFSET);
    parsed.end_time = read_float(bytes + HEADER_END_TIME_OFFSET);
    parsed.version = version;
    parsed.key_bytes = serialized_key_bytes;
    if(!isfinite(parsed.start_time) || !isfinite(parsed.end_time) ||
       parsed.start_time >= parsed.end_time) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < parsed.binding_count; ++index) {
        pvr_chunk_morph_animation_section_binding_t binding;

        decode_binding((const uint8_t *)parsed.bindings + index *
                           PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES,
                       &binding);
        if(binding.model_ordinal == UINT32_MAX || !binding.channel_count ||
           binding.first_channel != next_channel ||
           binding.channel_count > parsed.channel_count - next_channel) {
            errno = EILSEQ;
            return -1;
        }
        if(index) {
            pvr_chunk_morph_animation_section_binding_t previous;

            decode_binding((const uint8_t *)parsed.bindings + (index - 1u) *
                               PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES,
                           &previous);
            if(binding.node_index <= previous.node_index) {
                errno = EILSEQ;
                return -1;
            }
        }
        next_channel += binding.channel_count;
    }
    if(next_channel != parsed.channel_count) {
        errno = EILSEQ;
        return -1;
    }
    for(index = 0; index < parsed.channel_count; ++index) {
        const uint8_t *record = (const uint8_t *)parsed.channels + index *
            PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;

        if(read_le32(record + CHANNEL_TRACK_OFFSET) != index ||
           !isfinite(read_float(record + CHANNEL_FALLBACK_OFFSET))) {
            errno = EILSEQ;
            return -1;
        }
    }
    for(index = 0; index < parsed.track_count; ++index) {
        const uint8_t *record = (const uint8_t *)parsed.tracks + index *
            PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES;
        pvr_chunk_morph_animation_section_track_t track;
        float previous_time = 0.0f;
        size_t key;

        decode_track(record, &track);
        if(read_le16(record + TRACK_RESERVED0_OFFSET) ||
           read_le32(record + TRACK_RESERVED1_OFFSET) ||
           (track.interpolation != ANIM_INTERPOLATION_STEP &&
            track.interpolation != ANIM_INTERPOLATION_LINEAR &&
            track.interpolation != ANIM_INTERPOLATION_CUBIC_HERMITE) ||
           !track.key_count || track.first_key != next_key ||
           track.key_count > parsed.key_count - next_key) {
            errno = EILSEQ;
            return -1;
        }
        for(key = 0; key < track.key_count; ++key) {
            anim_scalar_hermite_key_t decoded;

            decode_key((const uint8_t *)parsed.keys + (next_key + key) *
                           parsed.key_bytes,
                       &decoded);
            if(!isfinite(decoded.time) || !isfinite(decoded.value) ||
               (track.interpolation ==
                    ANIM_INTERPOLATION_CUBIC_HERMITE &&
                (!isfinite(decoded.in_tangent) ||
                 !isfinite(decoded.out_tangent))) ||
               (key && decoded.time <= previous_time)) {
                errno = EILSEQ;
                return -1;
            }
            previous_time = decoded.time;
            if(!index && !key)
                observed_start = observed_end = decoded.time;
            else {
                if(decoded.time < observed_start)
                    observed_start = decoded.time;
                if(decoded.time > observed_end)
                    observed_end = decoded.time;
            }
        }
        next_key += track.key_count;
    }
    if(next_key != parsed.key_count || observed_start != parsed.start_time ||
       observed_end != parsed.end_time) {
        errno = EILSEQ;
        return -1;
    }
    *view = parsed;
    return 0;
}

int pvr_chunk_morph_animation_section_binding_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    pvr_chunk_morph_animation_section_binding_t *binding) {
    pvr_chunk_morph_animation_section_view_t checked;

    if(!view || !binding || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.binding_count) {
        errno = ENOENT;
        return -1;
    }
    decode_binding((const uint8_t *)checked.bindings + index *
                       PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES,
                   binding);
    return 0;
}

int pvr_chunk_morph_animation_section_channel_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    uint32_t *track_ordinal, float *fallback_weight) {
    pvr_chunk_morph_animation_section_view_t checked;
    const uint8_t *record;

    if(!view || !track_ordinal || !fallback_weight || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.channel_count) {
        errno = ENOENT;
        return -1;
    }
    record = (const uint8_t *)checked.channels + index *
             PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;
    *track_ordinal = read_le32(record + CHANNEL_TRACK_OFFSET);
    *fallback_weight = read_float(record + CHANNEL_FALLBACK_OFFSET);
    return 0;
}

int pvr_chunk_morph_animation_section_track_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    pvr_chunk_morph_animation_section_track_t *track) {
    pvr_chunk_morph_animation_section_view_t checked;

    if(!view || !track || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.track_count) {
        errno = ENOENT;
        return -1;
    }
    decode_track((const uint8_t *)checked.tracks + index *
                     PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES,
                 track);
    return 0;
}

int pvr_chunk_morph_animation_section_key_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    anim_scalar_hermite_key_t *key) {
    pvr_chunk_morph_animation_section_view_t checked;

    if(!view || !key || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.key_count) {
        errno = ENOENT;
        return -1;
    }
    decode_key((const uint8_t *)checked.keys + index *
                   checked.key_bytes,
               key);
    return 0;
}

int pvr_chunk_morph_animation_section_validate_scene(
    const pvr_chunk_morph_animation_section_view_t *view,
    const pvr_chunk_scene_hierarchy_view_t *hierarchy,
    const pvr_chunk_model_table_view_t *models,
    const pvr_chunk_shape_section_view_t *const *shapes,
    size_t shape_count) {
    pvr_chunk_morph_animation_section_view_t checked;
    size_t index;

    if(!view || !hierarchy || !models || !shapes || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    for(index = 0; index < checked.binding_count; ++index) {
        pvr_chunk_morph_animation_section_binding_t binding;
        pvr_chunk_scene_node_t node;
        pvr_chunk_model_table_record_t model;
        pvr_chunk_shape_section_view_t shape;

        decode_binding((const uint8_t *)checked.bindings + index *
                           PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES,
                       &binding);
        if(pvr_chunk_scene_hierarchy_node_get(
               hierarchy, binding.node_index, &node) < 0 ||
           pvr_chunk_model_table_record_get(
               models, binding.model_ordinal, &model) < 0)
            return -1;
        if(node.model_ordinal != binding.model_ordinal ||
           model.morph_ordinal == PVR_CHUNK_MODEL_SECTION_NONE ||
           model.morph_ordinal >= shape_count ||
           !shapes[model.morph_ordinal] ||
           !shapes[model.morph_ordinal]->data) {
            errno = EILSEQ;
            return -1;
        }
        if(pvr_chunk_shape_section_open(
               shapes[model.morph_ordinal]->data,
               shapes[model.morph_ordinal]->size, &shape) < 0)
            return -1;
        if(shape.target_count != binding.channel_count) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}

int pvr_chunk_morph_animation_section_materialize(
    const pvr_chunk_morph_animation_section_view_t *view,
    anim_scalar_hermite_key_t *keys, size_t key_capacity,
    anim_track_view_t *tracks, size_t track_capacity,
    pvr_chunk_shape_channel_t *channels, size_t channel_capacity,
    pvr_chunk_morph_animation_binding_t *bindings,
    size_t binding_capacity,
    pvr_chunk_morph_animation_t *animation) {
    pvr_chunk_morph_animation_section_view_t checked;
    pvr_chunk_morph_animation_t materialized;
    size_t key_bytes;
    size_t track_bytes;
    size_t channel_bytes;
    size_t binding_bytes;
    size_t index;

    if(!view || !keys || !tracks || !channels || !bindings || !animation ||
       !view->data ||
       ((uintptr_t)keys & (_Alignof(anim_scalar_hermite_key_t) - 1u)) ||
       ((uintptr_t)tracks & (_Alignof(anim_track_view_t) - 1u)) ||
       ((uintptr_t)channels &
        (_Alignof(pvr_chunk_shape_channel_t) - 1u)) ||
       ((uintptr_t)bindings &
        (_Alignof(pvr_chunk_morph_animation_binding_t) - 1u)) ||
       ((uintptr_t)animation &
        (_Alignof(pvr_chunk_morph_animation_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_morph_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(key_capacity < checked.key_count ||
       track_capacity < checked.track_count ||
       channel_capacity < checked.channel_count ||
       binding_capacity < checked.binding_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.key_count > SIZE_MAX / sizeof(*keys) ||
       checked.track_count > SIZE_MAX / sizeof(*tracks) ||
       checked.channel_count > SIZE_MAX / sizeof(*channels) ||
       checked.binding_count > SIZE_MAX / sizeof(*bindings)) {
        errno = EOVERFLOW;
        return -1;
    }
    key_bytes = checked.key_count * sizeof(*keys);
    track_bytes = checked.track_count * sizeof(*tracks);
    channel_bytes = checked.channel_count * sizeof(*channels);
    binding_bytes = checked.binding_count * sizeof(*bindings);
    if(ranges_overlap(keys, key_bytes, tracks, track_bytes) ||
       ranges_overlap(keys, key_bytes, channels, channel_bytes) ||
       ranges_overlap(keys, key_bytes, bindings, binding_bytes) ||
       ranges_overlap(tracks, track_bytes, channels, channel_bytes) ||
       ranges_overlap(tracks, track_bytes, bindings, binding_bytes) ||
       ranges_overlap(channels, channel_bytes, bindings, binding_bytes) ||
       ranges_overlap(keys, key_bytes, checked.data, checked.size) ||
       ranges_overlap(tracks, track_bytes, checked.data, checked.size) ||
       ranges_overlap(channels, channel_bytes, checked.data, checked.size) ||
       ranges_overlap(bindings, binding_bytes, checked.data, checked.size) ||
       ranges_overlap(animation, sizeof(*animation), checked.data,
                      checked.size) ||
       ranges_overlap(animation, sizeof(*animation), keys, key_bytes) ||
       ranges_overlap(animation, sizeof(*animation), tracks, track_bytes) ||
       ranges_overlap(animation, sizeof(*animation), channels,
                      channel_bytes) ||
       ranges_overlap(animation, sizeof(*animation), bindings,
                      binding_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(index = 0; index < checked.key_count; ++index)
        decode_key((const uint8_t *)checked.keys + index *
                       checked.key_bytes,
                   keys + index);
    for(index = 0; index < checked.track_count; ++index) {
        pvr_chunk_morph_animation_section_track_t track;

        decode_track((const uint8_t *)checked.tracks + index *
                         PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES,
                     &track);
        tracks[index].track.kind = ANIM_VALUE_SCALAR;
        tracks[index].track.interpolation = track.interpolation;
        tracks[index].track.keys = keys + track.first_key;
        tracks[index].track.key_count = track.key_count;
        tracks[index].track.stride = sizeof(*keys);
        tracks[index].start_time = keys[track.first_key].time;
        tracks[index].end_time =
            keys[track.first_key + track.key_count - 1u].time;
    }
    for(index = 0; index < checked.channel_count; ++index) {
        const uint8_t *record = (const uint8_t *)checked.channels + index *
            PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;
        uint32_t track = read_le32(record + CHANNEL_TRACK_OFFSET);

        channels[index].weight = tracks + track;
        channels[index].fallback_weight = read_float(
            record + CHANNEL_FALLBACK_OFFSET);
    }
    for(index = 0; index < checked.binding_count; ++index) {
        pvr_chunk_morph_animation_section_binding_t binding;

        decode_binding((const uint8_t *)checked.bindings + index *
                           PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES,
                       &binding);
        bindings[index].node_index = binding.node_index;
        bindings[index].model_ordinal = binding.model_ordinal;
        bindings[index].channels = channels + binding.first_channel;
        bindings[index].channel_count = binding.channel_count;
    }
    materialized.bindings = bindings;
    materialized.binding_count = checked.binding_count;
    materialized.start_time = checked.start_time;
    materialized.end_time = checked.end_time;
    *animation = materialized;
    return 0;
}
