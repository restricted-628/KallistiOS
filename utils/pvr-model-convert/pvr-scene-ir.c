/* KallistiOS ##version##

   Host-side canonical scene representation for compact assets.
   Copyright (C) 2026 Joseph Black
*/

#include "pvr-scene-ir.h"

#include <dc/pvr_chunk_scene.h>
#include <dc/pvr_chunk_skin_asset.h>
#include <dc/pvr_chunk_shape_asset.h>
#include <dc/pvr_chunk_animation_asset.h>
#include <dc/pvr_chunk_animation_catalog.h>
#include <dc/pvr_chunk_morph_animation_asset.h>
#include <dc/pvr_chunk_volume_asset.h>

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
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

static void store_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    store_le32(bytes, word);
}

int pvr_scene_ir_serialize_morph_animation(
    const pvr_chunk_morph_animation_t *animation,
    uint8_t **bytes_out, size_t *size_out) {
    uint8_t *bytes = NULL;
    size_t channel_count = 0;
    size_t key_count = 0;
    size_t binding_bytes;
    size_t channel_bytes;
    size_t track_bytes;
    size_t key_bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t channel_cursor = 0;
    size_t key_cursor = 0;
    size_t binding_index;
    float observed_start = 0.0f;
    float observed_end = 0.0f;
    int observed_key = 0;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!animation || !bytes_out || !size_out || !animation->bindings ||
       !animation->binding_count || !isfinite(animation->start_time) ||
       !isfinite(animation->end_time) ||
       animation->start_time >= animation->end_time) {
        errno = EINVAL;
        return -1;
    }
    for(binding_index = 0; binding_index < animation->binding_count;
        ++binding_index) {
        const pvr_chunk_morph_animation_binding_t *binding =
            animation->bindings + binding_index;
        size_t channel_index;

        if(binding->node_index > UINT32_MAX ||
           binding->model_ordinal > UINT32_MAX ||
           binding->model_ordinal == UINT32_MAX || !binding->channels ||
           !binding->channel_count ||
           (binding_index && binding->node_index <=
                                animation->bindings[binding_index - 1u]
                                    .node_index) ||
           binding->channel_count > UINT32_MAX - channel_count) {
            errno = EINVAL;
            return -1;
        }
        for(channel_index = 0; channel_index < binding->channel_count;
            ++channel_index) {
            const pvr_chunk_shape_channel_t *channel =
                binding->channels + channel_index;
            const anim_track_view_t *track = channel->weight;
            const uint8_t *keys;
            size_t key;

            if(!track || !isfinite(channel->fallback_weight) ||
               track->track.kind != ANIM_VALUE_SCALAR ||
               (track->track.interpolation != ANIM_INTERPOLATION_STEP &&
                track->track.interpolation != ANIM_INTERPOLATION_LINEAR &&
                track->track.interpolation !=
                    ANIM_INTERPOLATION_CUBIC_HERMITE) ||
               !track->track.keys || !track->track.key_count ||
               (track->track.interpolation ==
                    ANIM_INTERPOLATION_CUBIC_HERMITE ?
                    track->track.stride !=
                        sizeof(anim_scalar_hermite_key_t) :
                    (track->track.stride != sizeof(anim_scalar_key_t) &&
                     track->track.stride !=
                         sizeof(anim_scalar_hermite_key_t))) ||
               track->track.key_count > UINT32_MAX - key_count) {
                errno = EINVAL;
                return -1;
            }
            keys = track->track.keys;
            for(key = 0; key < track->track.key_count; ++key) {
                anim_scalar_hermite_key_t decoded = { 0 };
                const uint8_t *source = keys + key * track->track.stride;
                size_t source_bytes = track->track.interpolation ==
                                          ANIM_INTERPOLATION_CUBIC_HERMITE ?
                                          sizeof(decoded) :
                                          sizeof(anim_scalar_key_t);
                float previous_time = 0.0f;

                memcpy(&decoded, source, source_bytes);
                if(key)
                    memcpy(&previous_time,
                           keys + (key - 1u) * track->track.stride,
                           sizeof(previous_time));
                if(!isfinite(decoded.time) ||
                   !isfinite(decoded.value) ||
                   (track->track.interpolation ==
                        ANIM_INTERPOLATION_CUBIC_HERMITE &&
                    (!isfinite(decoded.in_tangent) ||
                     !isfinite(decoded.out_tangent))) ||
                   (key && decoded.time <= previous_time)) {
                    errno = EINVAL;
                    return -1;
                }
                if(!observed_key) {
                    observed_start = decoded.time;
                    observed_end = decoded.time;
                    observed_key = 1;
                }
                else {
                    if(decoded.time < observed_start)
                        observed_start = decoded.time;
                    if(decoded.time > observed_end)
                        observed_end = decoded.time;
                }
            }
            key_count += track->track.key_count;
            ++channel_count;
        }
    }
    if(!channel_count || !key_count || channel_count > UINT32_MAX ||
       key_count > UINT32_MAX ||
       animation->binding_count > UINT32_MAX ||
       observed_start != animation->start_time ||
       observed_end != animation->end_time) {
        errno = EINVAL;
        return -1;
    }
    if(animation->binding_count >
           SIZE_MAX / PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES ||
       channel_count >
           SIZE_MAX / PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES ||
       channel_count >
           SIZE_MAX / PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES ||
       key_count > SIZE_MAX / PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    binding_bytes = animation->binding_count *
                    PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES;
    channel_bytes = channel_count *
                    PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;
    track_bytes = channel_count * PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES;
    key_bytes = key_count * PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES;
    if(binding_bytes > SIZE_MAX - channel_bytes ||
       binding_bytes + channel_bytes > SIZE_MAX - track_bytes ||
       binding_bytes + channel_bytes + track_bytes > SIZE_MAX - key_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    payload_bytes = binding_bytes + channel_bytes + track_bytes + key_bytes;
    if(payload_bytes >
       SIZE_MAX - PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    file_bytes = PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES + payload_bytes;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(binding_index = 0; binding_index < animation->binding_count;
        ++binding_index) {
        const pvr_chunk_morph_animation_binding_t *binding =
            animation->bindings + binding_index;
        uint8_t *binding_record = bytes +
            PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES + binding_index *
                PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES;
        size_t channel_index;

        store_le32(binding_record + 0, (uint32_t)binding->node_index);
        store_le32(binding_record + 4, (uint32_t)binding->model_ordinal);
        store_le32(binding_record + 8, (uint32_t)channel_cursor);
        store_le32(binding_record + 12,
                   (uint32_t)binding->channel_count);
        for(channel_index = 0; channel_index < binding->channel_count;
            ++channel_index) {
            const pvr_chunk_shape_channel_t *channel =
                binding->channels + channel_index;
            const anim_track_view_t *track = channel->weight;
            const uint8_t *keys = track->track.keys;
            uint8_t *channel_record = bytes +
                PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES + binding_bytes +
                channel_cursor * PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES;
            uint8_t *track_record = bytes +
                PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES + binding_bytes +
                channel_bytes + channel_cursor *
                    PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES;
            size_t key;

            store_le32(channel_record + 0, (uint32_t)channel_cursor);
            store_float(channel_record + 4, channel->fallback_weight);
            store_le16(track_record + 0,
                       (uint16_t)track->track.interpolation);
            store_le32(track_record + 4, (uint32_t)key_cursor);
            store_le32(track_record + 8,
                       (uint32_t)track->track.key_count);
            for(key = 0; key < track->track.key_count; ++key) {
                anim_scalar_hermite_key_t decoded = { 0 };
                uint8_t *key_record = bytes +
                    PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES + binding_bytes +
                    channel_bytes + track_bytes + key_cursor *
                        PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES;

                memcpy(&decoded, keys + key * track->track.stride,
                       track->track.interpolation ==
                           ANIM_INTERPOLATION_CUBIC_HERMITE ?
                           sizeof(decoded) : sizeof(anim_scalar_key_t));
                store_float(key_record + 0, decoded.time);
                store_float(key_record + 4, decoded.value);
                if(track->track.interpolation ==
                   ANIM_INTERPOLATION_CUBIC_HERMITE) {
                    store_float(key_record + 8, decoded.in_tangent);
                    store_float(key_record + 12, decoded.out_tangent);
                }
                ++key_cursor;
            }
            ++channel_cursor;
        }
    }
    if(channel_cursor != channel_count || key_cursor != key_count) {
        free(bytes);
        errno = EPROTO;
        return -1;
    }
    store_le32(bytes + 0, PVR_CHUNK_MORPH_ANIMATION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_MORPH_ANIMATION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)animation->binding_count);
    store_le32(bytes + 16, (uint32_t)channel_count);
    store_le32(bytes + 20, (uint32_t)channel_count);
    store_le32(bytes + 24, (uint32_t)key_count);
    store_float(bytes + 28, animation->start_time);
    store_float(bytes + 32, animation->end_time);
    store_le32(bytes + 36, crc32_bytes(
        bytes + PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 40, crc32_bytes(bytes, 40));
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}

int pvr_scene_ir_serialize_animation_catalog(
    const pvr_scene_ir_animation_clip_t *clips, size_t clip_count,
    uint8_t **bytes_out, size_t *size_out) {
    uint8_t *bytes = NULL;
    size_t records_bytes;
    size_t string_bytes = 0;
    size_t file_bytes;
    size_t string_offset = 0;
    size_t index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!clips || !clip_count || !bytes_out || !size_out ||
       clip_count > UINT32_MAX ||
       clip_count > SIZE_MAX /
           PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < clip_count; ++index) {
        size_t name_bytes = clips[index].name ?
            strlen(clips[index].name) : 0;
        size_t previous;

        if((clips[index].transform_ordinal ==
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
            clips[index].morph_ordinal ==
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE) ||
           !isfinite(clips[index].start_time) ||
           !isfinite(clips[index].end_time) ||
           clips[index].start_time >= clips[index].end_time ||
           name_bytes > UINT32_MAX - string_bytes) {
            errno = EINVAL;
            return -1;
        }
        for(previous = 0; name_bytes && previous < index; ++previous) {
            if(clips[previous].name &&
               strlen(clips[previous].name) == name_bytes &&
               !memcmp(clips[previous].name, clips[index].name,
                       name_bytes)) {
                errno = EEXIST;
                return -1;
            }
        }
        string_bytes += name_bytes;
    }
    records_bytes = clip_count *
        PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES;
    if(records_bytes > SIZE_MAX -
           PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES ||
       string_bytes > SIZE_MAX -
           PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES - records_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    file_bytes = PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES +
                 records_bytes + string_bytes;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }
    for(index = 0; index < clip_count; ++index) {
        const pvr_scene_ir_animation_clip_t *clip = clips + index;
        uint8_t *record = bytes +
            PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES + index *
                PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES;
        size_t name_bytes = clip->name ? strlen(clip->name) : 0;

        store_le32(record, clip->transform_ordinal);
        store_le32(record + 4, clip->morph_ordinal);
        store_le32(record + 8, (uint32_t)string_offset);
        store_le32(record + 12, (uint32_t)name_bytes);
        store_float(record + 16, clip->start_time);
        store_float(record + 20, clip->end_time);
        if(name_bytes) {
            memcpy(bytes + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES +
                       records_bytes + string_offset,
                   clip->name, name_bytes);
            string_offset += name_bytes;
        }
    }
    if(string_offset != string_bytes) {
        free(bytes);
        errno = EPROTO;
        return -1;
    }
    store_le32(bytes, PVR_CHUNK_ANIMATION_CATALOG_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_ANIMATION_CATALOG_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)clip_count);
    store_le16(bytes + 16, PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES);
    store_le32(bytes + 20, (uint32_t)records_bytes);
    store_le32(bytes + 24, (uint32_t)string_bytes);
    store_le32(bytes + 28, crc32_bytes(
        bytes + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES,
        file_bytes - PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}

int pvr_scene_ir_serialize_volumes(
    const pvr_chunk_model_view_t *model,
    uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_model_view_t admitted;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    uint8_t *bytes = NULL;
    size_t record_count = 0;
    size_t word_count = 0;
    size_t record_bytes;
    size_t stream_bytes;
    size_t file_bytes;
    size_t record_index = 0;
    size_t first_word = 0;
    int rv;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!model || !bytes_out || !size_out) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&model->model, &admitted) < 0 ||
       pvr_chunk_polygon_iterator_init(
           &iterator, admitted.model.polygon_words,
           admitted.model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        if(record_count == UINT32_MAX ||
           record.word_count > UINT32_MAX - word_count) {
            errno = EOVERFLOW;
            return -1;
        }
        ++record_count;
        word_count += record.word_count;
    }
    if(rv < 0)
        return -1;
    if(!record_count)
        return 0;
    if(record_count >
           (SIZE_MAX - PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES) /
               PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES ||
       word_count > SIZE_MAX / sizeof(uint16_t)) {
        errno = EOVERFLOW;
        return -1;
    }
    record_bytes = record_count * PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES;
    stream_bytes = word_count * sizeof(uint16_t);
    if(stream_bytes > SIZE_MAX - PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES -
                              record_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    file_bytes = PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES + record_bytes +
                 stream_bytes;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    if(pvr_chunk_polygon_iterator_init(
           &iterator, admitted.model.polygon_words,
           admitted.model.polygon_word_count) < 0)
        goto fail;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        uint8_t *descriptor;
        uint8_t *destination;
        const uint16_t *source;
        size_t word;

        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        descriptor = bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES +
                     record_index *
                         PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES;
        destination = bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES +
                      record_bytes + first_word * sizeof(uint16_t);
        source = record.words;
        store_le32(descriptor, (uint32_t)first_word);
        store_le32(descriptor + 4, (uint32_t)record.word_count);
        for(word = 0; word < record.word_count; ++word)
            store_le16(destination + word * sizeof(uint16_t), source[word]);
        first_word += record.word_count;
        ++record_index;
    }
    if(rv < 0)
        goto fail;
    if(record_index != record_count || first_word != word_count) {
        errno = EILSEQ;
        goto fail;
    }

    store_le32(bytes, PVR_CHUNK_VOLUME_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_VOLUME_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)record_count);
    store_le32(bytes + 16, (uint32_t)word_count);
    store_le16(bytes + 20, PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES);
    store_le16(bytes + 22, sizeof(uint16_t));
    store_le32(bytes + 24, (uint32_t)record_bytes);
    store_le32(bytes + 28, (uint32_t)stream_bytes);
    store_le32(bytes + 32, crc32_bytes(
        bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES,
        file_bytes - PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
    {
        pvr_chunk_volume_section_view_t checked;

        if(pvr_chunk_volume_section_open(bytes, file_bytes, &checked) < 0 ||
           pvr_chunk_volume_section_validate_model(&checked, &admitted) < 0)
            goto fail;
    }

    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail:
    free(bytes);
    return -1;
}

void pvr_scene_ir_free(pvr_scene_ir_t *scene) {
    if(!scene)
        return;
    free(scene->nodes);
    memset(scene, 0, sizeof(*scene));
}

int pvr_scene_ir_add_node_flags(pvr_scene_ir_t *scene,
                                uint32_t parent_index,
                                uint32_t model_ordinal, uint32_t flags,
                                const float local_transform[16]) {
    pvr_scene_ir_node_t *resized;
    size_t component;
    size_t capacity;

    if(!scene || !local_transform || scene->node_count >= UINT32_MAX ||
       (parent_index != UINT32_MAX && parent_index >= scene->node_count) ||
       (flags & ~PVR_CHUNK_NODE_FLAGS_MASK)) {
        errno = EINVAL;
        return -1;
    }
    for(component = 0; component < 16; ++component) {
        if(!isfinite(local_transform[component])) {
            errno = EDOM;
            return -1;
        }
    }
    if(scene->node_count == scene->node_capacity) {
        capacity = scene->node_capacity ? scene->node_capacity * 2u : 8u;
        if(capacity < scene->node_capacity ||
           capacity > SIZE_MAX / sizeof(*scene->nodes)) {
            errno = EOVERFLOW;
            return -1;
        }
        resized = realloc(scene->nodes, capacity * sizeof(*scene->nodes));
        if(!resized) {
            errno = ENOMEM;
            return -1;
        }
        scene->nodes = resized;
        scene->node_capacity = capacity;
    }

    scene->nodes[scene->node_count].parent_index = parent_index;
    scene->nodes[scene->node_count].model_ordinal = model_ordinal;
    scene->nodes[scene->node_count].flags = flags;
    memcpy(scene->nodes[scene->node_count].local_transform,
           local_transform, 16u * sizeof(float));
    ++scene->node_count;
    return 0;
}

int pvr_scene_ir_add_node(pvr_scene_ir_t *scene, uint32_t parent_index,
                          uint32_t model_ordinal,
                          const float local_transform[16]) {
    return pvr_scene_ir_add_node_flags(scene, parent_index, model_ordinal, 0,
                                       local_transform);
}

int pvr_scene_ir_add_root_model(pvr_scene_ir_t *scene,
                                uint32_t model_ordinal) {
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return pvr_scene_ir_add_node(scene, UINT32_MAX, model_ordinal, identity);
}

int pvr_scene_ir_validate(const pvr_scene_ir_t *scene) {
    size_t index;

    if(!scene || (scene->node_count && !scene->nodes) ||
       scene->node_count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < scene->node_count; ++index) {
        const pvr_scene_ir_node_t *node = scene->nodes + index;
        size_t component;

        if((node->parent_index != UINT32_MAX &&
            node->parent_index >= index) ||
           (node->flags & ~PVR_CHUNK_NODE_FLAGS_MASK)) {
            errno = EILSEQ;
            return -1;
        }
        for(component = 0; component < 16; ++component) {
            if(!isfinite(node->local_transform[component])) {
                errno = EILSEQ;
                return -1;
            }
        }
    }
    return 0;
}

int pvr_scene_ir_serialize_hierarchy(const pvr_scene_ir_t *scene,
                                     uint8_t **bytes_out,
                                     size_t *size_out) {
    uint8_t *bytes;
    size_t node_bytes;
    size_t file_bytes;
    size_t index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!bytes_out || !size_out) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_scene_ir_validate(scene) < 0)
        return -1;
    if(scene->node_count >
       (SIZE_MAX - PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES) /
           PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    node_bytes = scene->node_count * PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES;
    file_bytes = PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES + node_bytes;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(index = 0; index < scene->node_count; ++index) {
        const pvr_scene_ir_node_t *node = scene->nodes + index;
        uint8_t *record = bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES +
                          index * PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES;
        size_t component;

        store_le32(record, node->parent_index);
        store_le32(record + 4, node->model_ordinal);
        store_le32(record + 8, node->flags);
        for(component = 0; component < 16; ++component)
            store_float(record + 16 + component * sizeof(uint32_t),
                        node->local_transform[component]);
    }

    store_le32(bytes, PVR_CHUNK_SCENE_HIERARCHY_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_SCENE_HIERARCHY_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)scene->node_count);
    store_le16(bytes + 16, PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES);
    store_le32(bytes + 20, crc32_bytes(
        bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES, node_bytes));
    store_le32(bytes + 28, crc32_bytes(bytes, 28));

    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}

int pvr_scene_ir_serialize_general_skin(
    const pvr_chunk_skin_general_t *skin,
    uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_skin_general_section_view_t checked;
    uint8_t *bytes;
    size_t span_bytes;
    size_t weight_bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t next_weight = 0;
    size_t index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!skin || !bytes_out || !size_out || !skin->spans ||
       !skin->weights || !skin->span_count || !skin->weight_count ||
       !skin->joint_count || skin->joint_count > UINT16_MAX + 1u ||
       skin->span_count > UINT32_MAX || skin->weight_count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    if(skin->span_count > SIZE_MAX / PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES ||
       skin->weight_count >
           SIZE_MAX / PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }

    for(index = 0; index < skin->span_count; ++index) {
        const pvr_chunk_skin_span_t *span = skin->spans + index;
        uint32_t total = 0;
        size_t slot;

        if((index && span->vertex_index <=
                     skin->spans[index - 1u].vertex_index) ||
           !span->weight_count || span->first_weight != next_weight ||
           span->weight_count > skin->weight_count - next_weight) {
            errno = EILSEQ;
            return -1;
        }
        for(slot = 0; slot < span->weight_count; ++slot) {
            const pvr_chunk_skin_weight_t *weight =
                skin->weights + next_weight + slot;

            if(!weight->weight || weight->joint >= skin->joint_count) {
                errno = EILSEQ;
                return -1;
            }
            total += weight->weight;
        }
        if(total != PVR_CHUNK_SKIN_WEIGHT_SUM) {
            errno = EILSEQ;
            return -1;
        }
        next_weight += span->weight_count;
    }
    if(next_weight != skin->weight_count) {
        errno = EILSEQ;
        return -1;
    }

    span_bytes = skin->span_count * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES;
    weight_bytes = skin->weight_count *
                   PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES;
    if(span_bytes > SIZE_MAX - weight_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    payload_bytes = span_bytes + weight_bytes;
    if(payload_bytes > SIZE_MAX - PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    file_bytes = PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES + payload_bytes;
    if(file_bytes > UINT32_MAX || span_bytes > UINT32_MAX ||
       weight_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(index = 0; index < skin->span_count; ++index) {
        uint8_t *record = bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES +
                          index * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES;

        store_le16(record, skin->spans[index].vertex_index);
        store_le16(record + 2, skin->spans[index].weight_count);
        store_le32(record + 4, skin->spans[index].first_weight);
    }
    for(index = 0; index < skin->weight_count; ++index) {
        uint8_t *record = bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES +
                          span_bytes +
                          index * PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES;

        store_le16(record, skin->weights[index].joint);
        store_le16(record + 2, skin->weights[index].weight);
    }

    store_le32(bytes, PVR_CHUNK_SKIN_GENERAL_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_SKIN_GENERAL_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)skin->span_count);
    store_le32(bytes + 16, (uint32_t)skin->weight_count);
    store_le32(bytes + 20, (uint32_t)skin->joint_count);
    store_le16(bytes + 24, PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES);
    store_le16(bytes + 26, PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES);
    store_le32(bytes + 28, (uint32_t)span_bytes);
    store_le32(bytes + 32, (uint32_t)weight_bytes);
    store_le32(bytes + 36, crc32_bytes(
        bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));

    if(pvr_chunk_skin_general_section_open(
           bytes, file_bytes, &checked) < 0) {
        int saved_errno = errno;

        free(bytes);
        errno = saved_errno;
        return -1;
    }
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}

int pvr_scene_ir_serialize_shapes(
    const pvr_chunk_shape_set_t *shapes,
    uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_shape_section_view_t checked;
    uint8_t *bytes;
    size_t target_bytes;
    size_t delta_count = 0;
    size_t delta_bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t target_index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!shapes || !bytes_out || !size_out || !shapes->targets ||
       !shapes->target_count || shapes->target_count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    if(shapes->target_count >
       SIZE_MAX / PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }

    for(target_index = 0; target_index < shapes->target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            shapes->targets + target_index;
        size_t delta_index;

        if(!target->deltas || !target->delta_count ||
           target->delta_count > UINT32_MAX - delta_count) {
            errno = target->deltas && target->delta_count ? EOVERFLOW :
                                                             EINVAL;
            return -1;
        }
        for(delta_index = 0; delta_index < target->delta_count;
            ++delta_index) {
            const pvr_chunk_shape_delta_t *delta =
                target->deltas + delta_index;

            if(delta->reserved ||
               (delta_index && delta->vertex_index <=
                                target->deltas[delta_index - 1u].vertex_index) ||
               !isfinite(delta->delta.position.x) ||
               !isfinite(delta->delta.position.y) ||
               !isfinite(delta->delta.position.z) ||
               !isfinite(delta->delta.normal.x) ||
               !isfinite(delta->delta.normal.y) ||
               !isfinite(delta->delta.normal.z) ||
               delta->delta.position.w != 0.0f ||
               delta->delta.normal.w != 0.0f) {
                errno = EILSEQ;
                return -1;
            }
        }
        delta_count += target->delta_count;
    }
    if(!delta_count || delta_count >
       SIZE_MAX / PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES) {
        errno = delta_count ? EOVERFLOW : EINVAL;
        return -1;
    }

    target_bytes = shapes->target_count *
                   PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES;
    delta_bytes = delta_count * PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES;
    if(target_bytes > SIZE_MAX - delta_bytes) {
        errno = EOVERFLOW;
        return -1;
    }
    payload_bytes = target_bytes + delta_bytes;
    if(payload_bytes > SIZE_MAX - PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    file_bytes = PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES + payload_bytes;
    if(file_bytes > UINT32_MAX || target_bytes > UINT32_MAX ||
       delta_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    delta_count = 0;
    for(target_index = 0; target_index < shapes->target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            shapes->targets + target_index;
        uint8_t *target_record = bytes +
            PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES + target_index *
                PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES;
        size_t delta_index;

        store_le32(target_record, (uint32_t)delta_count);
        store_le32(target_record + 4, (uint32_t)target->delta_count);
        for(delta_index = 0; delta_index < target->delta_count;
            ++delta_index) {
            const pvr_chunk_shape_delta_t *delta =
                target->deltas + delta_index;
            uint8_t *delta_record = bytes +
                PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES + target_bytes +
                (delta_count + delta_index) *
                    PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES;

            store_le16(delta_record, delta->vertex_index);
            store_le16(delta_record + 2, 0);
            store_float(delta_record + 4, delta->delta.position.x);
            store_float(delta_record + 8, delta->delta.position.y);
            store_float(delta_record + 12, delta->delta.position.z);
            store_float(delta_record + 16, delta->delta.normal.x);
            store_float(delta_record + 20, delta->delta.normal.y);
            store_float(delta_record + 24, delta->delta.normal.z);
        }
        delta_count += target->delta_count;
    }

    store_le32(bytes, PVR_CHUNK_SHAPE_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_SHAPE_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)shapes->target_count);
    store_le32(bytes + 16, (uint32_t)delta_count);
    store_le16(bytes + 20, PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES);
    store_le16(bytes + 22, PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES);
    store_le32(bytes + 24, (uint32_t)target_bytes);
    store_le32(bytes + 28, (uint32_t)delta_bytes);
    store_le32(bytes + 32, crc32_bytes(
        bytes + PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));

    if(pvr_chunk_shape_section_open(bytes, file_bytes, &checked) < 0) {
        int saved_errno = errno;

        free(bytes);
        errno = saved_errno;
        return -1;
    }
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}

static int animation_track_append(
    const anim_track_view_t *track,
    const anim_track_view_t **tracks, size_t *track_count,
    size_t track_capacity) {
    size_t index;

    if(!track)
        return 0;
    for(index = 0; index < *track_count; ++index) {
        if(tracks[index] == track)
            return 0;
    }
    if(*track_count >= track_capacity || *track_count >= UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    tracks[(*track_count)++] = track;
    return 0;
}

static uint32_t animation_track_ordinal(
    const anim_track_view_t *track,
    const anim_track_view_t *const *tracks, size_t track_count) {
    size_t index;

    if(!track)
        return PVR_CHUNK_ANIMATION_TRACK_NONE;
    for(index = 0; index < track_count; ++index) {
        if(tracks[index] == track)
            return (uint32_t)index;
    }
    return PVR_CHUNK_ANIMATION_TRACK_NONE;
}

static size_t animation_key_size(anim_value_kind_t kind,
                                 anim_interpolation_t interpolation) {
    if(interpolation == ANIM_INTERPOLATION_CUBIC_HERMITE) {
        switch(kind) {
            case ANIM_VALUE_SCALAR:
                return sizeof(anim_scalar_hermite_key_t);
            case ANIM_VALUE_VECTOR:
                return sizeof(anim_vector_hermite_key_t);
            case ANIM_VALUE_QUATERNION:
                return sizeof(anim_quaternion_hermite_key_t);
            default:
                return 0;
        }
    }
    switch(kind) {
        case ANIM_VALUE_SCALAR:
            return sizeof(anim_scalar_key_t);
        case ANIM_VALUE_VECTOR:
            return sizeof(anim_vector_key_t);
        case ANIM_VALUE_QUATERNION:
            return sizeof(anim_quaternion_key_t);
        case ANIM_VALUE_BOOLEAN:
            return sizeof(anim_boolean_key_t);
        default:
            return 0;
    }
}

static int animation_track_valid(const anim_track_view_t *view,
                                 anim_value_kind_t expected) {
    const anim_track_t *track;
    size_t minimum_size;

    if(!view)
        return 1;
    if((uintptr_t)view & (_Alignof(anim_track_view_t) - 1u))
        return 0;
    track = &view->track;
    minimum_size = animation_key_size(expected, track->interpolation);
    return track->kind == expected && track->keys && track->key_count &&
           !((uintptr_t)track->keys & 3u) && minimum_size &&
           track->stride >= minimum_size && !(track->stride & 3u) &&
           track->interpolation >= ANIM_INTERPOLATION_STEP &&
           track->interpolation <= ANIM_INTERPOLATION_CUBIC_HERMITE &&
           !(expected == ANIM_VALUE_BOOLEAN &&
             track->interpolation != ANIM_INTERPOLATION_STEP) &&
           !(expected == ANIM_VALUE_QUATERNION &&
             track->interpolation == ANIM_INTERPOLATION_CATMULL_ROM) &&
           track->key_count - 1u <=
               (SIZE_MAX - minimum_size) / track->stride &&
           (track->key_count - 1u) * track->stride + minimum_size <=
               UINTPTR_MAX - (uintptr_t)track->keys;
}

static int animation_clip_valid(const anim_clip_t *clip) {
    size_t index;

    if(!clip || !clip->transforms || !clip->transform_count ||
       ((uintptr_t)clip->transforms &
        (_Alignof(anim_transform_tracks_t) - 1u)) ||
       (clip->visibility &&
        ((uintptr_t)clip->visibility &
         (_Alignof(anim_visibility_tracks_t) - 1u))) ||
       !isfinite(clip->start_time) || !isfinite(clip->end_time) ||
       clip->start_time >= clip->end_time ||
       clip->transform_count > SIZE_MAX / sizeof(*clip->transforms) ||
       clip->transform_count * sizeof(*clip->transforms) >
           UINTPTR_MAX - (uintptr_t)clip->transforms ||
       (clip->visibility &&
        (clip->transform_count > SIZE_MAX / sizeof(*clip->visibility) ||
         clip->transform_count * sizeof(*clip->visibility) >
             UINTPTR_MAX - (uintptr_t)clip->visibility)))
        return 0;
    for(index = 0; index < clip->transform_count; ++index) {
        const anim_transform_tracks_t *transform = clip->transforms + index;
        const anim_visibility_tracks_t *visibility =
            clip->visibility ? clip->visibility + index : NULL;
        const anim_quaternion_t *rotation = &transform->fallback.rotation;
        anim_value_kind_t rotation_kind =
            transform->rotation_mode == ANIM_ROTATION_QUATERNION ?
                ANIM_VALUE_QUATERNION : ANIM_VALUE_VECTOR;
        float magnitude_squared = rotation->w * rotation->w +
                                  rotation->x * rotation->x +
                                  rotation->y * rotation->y +
                                  rotation->z * rotation->z;

        if(transform->rotation_mode < ANIM_ROTATION_QUATERNION ||
           transform->rotation_mode > ANIM_ROTATION_EULER_ZXY ||
           !isfinite(transform->fallback.translation.x) ||
           !isfinite(transform->fallback.translation.y) ||
           !isfinite(transform->fallback.translation.z) ||
           !isfinite(transform->fallback.scale.x) ||
           !isfinite(transform->fallback.scale.y) ||
           !isfinite(transform->fallback.scale.z) ||
           !isfinite(rotation->w) || !isfinite(rotation->x) ||
           !isfinite(rotation->y) || !isfinite(rotation->z) ||
           !isfinite(magnitude_squared) || magnitude_squared <= FLT_MIN ||
           !animation_track_valid(transform->translation,
                                  ANIM_VALUE_VECTOR) ||
           !animation_track_valid(transform->rotation,
                                  rotation_kind) ||
           !animation_track_valid(transform->scale, ANIM_VALUE_VECTOR) ||
           !animation_track_valid(visibility ? visibility->visible : NULL,
                                  ANIM_VALUE_BOOLEAN))
            return 0;
    }
    return 1;
}

int pvr_scene_ir_serialize_animation(
    const anim_clip_view_t *clip,
    uint8_t **bytes_out, size_t *size_out) {
    const anim_track_view_t **track_refs = NULL;
    const anim_clip_t *source;
    pvr_chunk_animation_section_view_t section;
    uint8_t *bytes = NULL;
    size_t track_capacity;
    size_t track_count = 0;
    size_t key_count = 0;
    size_t transform_bytes;
    size_t track_bytes;
    size_t key_bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t transform_index;
    size_t track_index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!clip || !bytes_out || !size_out ||
       !animation_clip_valid(&clip->clip)) {
        errno = EINVAL;
        return -1;
    }
    source = &clip->clip;
    if(source->transform_count > SIZE_MAX / 4u) {
        errno = EOVERFLOW;
        return -1;
    }
    track_capacity = source->transform_count * 4u;
    if(track_capacity) {
        track_refs = calloc(track_capacity, sizeof(*track_refs));
        if(!track_refs) {
            errno = ENOMEM;
            return -1;
        }
    }

    for(transform_index = 0;
        transform_index < source->transform_count;
        ++transform_index) {
        const anim_transform_tracks_t *transform =
            source->transforms + transform_index;
        const anim_visibility_tracks_t *visibility =
            source->visibility ? source->visibility + transform_index : NULL;

        if(animation_track_append(transform->translation, track_refs,
                                  &track_count, track_capacity) < 0 ||
           animation_track_append(transform->rotation, track_refs,
                                  &track_count, track_capacity) < 0 ||
           animation_track_append(transform->scale, track_refs,
                                  &track_count, track_capacity) < 0 ||
           animation_track_append(visibility ? visibility->visible : NULL,
                                  track_refs, &track_count,
                                  track_capacity) < 0)
            goto fail;
    }
    for(track_index = 0; track_index < track_count; ++track_index) {
        if(track_refs[track_index]->track.key_count >
           UINT32_MAX - key_count) {
            errno = EOVERFLOW;
            goto fail;
        }
        key_count += track_refs[track_index]->track.key_count;
    }

    if(source->transform_count >
           SIZE_MAX / PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES ||
       track_count > SIZE_MAX / PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES ||
       key_count > SIZE_MAX / PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES) {
        errno = EOVERFLOW;
        goto fail;
    }
    transform_bytes = source->transform_count *
                      PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES;
    track_bytes = track_count * PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES;
    key_bytes = key_count * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES;
    if(transform_bytes > SIZE_MAX - track_bytes ||
       transform_bytes + track_bytes > SIZE_MAX - key_bytes) {
        errno = EOVERFLOW;
        goto fail;
    }
    payload_bytes = transform_bytes + track_bytes + key_bytes;
    if(payload_bytes >
       SIZE_MAX - PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES) {
        errno = EOVERFLOW;
        goto fail;
    }
    file_bytes = PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + payload_bytes;
    if(file_bytes > UINT32_MAX || transform_bytes > UINT32_MAX ||
       track_bytes > UINT32_MAX || key_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        goto fail;
    }

    for(transform_index = 0;
        transform_index < source->transform_count;
        ++transform_index) {
        const anim_transform_tracks_t *transform =
            source->transforms + transform_index;
        const anim_visibility_tracks_t *visibility =
            source->visibility ? source->visibility + transform_index : NULL;
        uint8_t *record = bytes +
            PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + transform_index *
                PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES;

        store_le32(record, animation_track_ordinal(
            transform->translation, track_refs, track_count));
        store_le32(record + 4, animation_track_ordinal(
            transform->rotation, track_refs, track_count));
        store_le32(record + 8, animation_track_ordinal(
            transform->scale, track_refs, track_count));
        store_le32(record + 12, animation_track_ordinal(
            visibility ? visibility->visible : NULL,
            track_refs, track_count));
        store_float(record + 16, transform->fallback.translation.x);
        store_float(record + 20, transform->fallback.translation.y);
        store_float(record + 24, transform->fallback.translation.z);
        store_float(record + 28, transform->fallback.rotation.w);
        store_float(record + 32, transform->fallback.rotation.x);
        store_float(record + 36, transform->fallback.rotation.y);
        store_float(record + 40, transform->fallback.rotation.z);
        store_float(record + 44, transform->fallback.scale.x);
        store_float(record + 48, transform->fallback.scale.y);
        store_float(record + 52, transform->fallback.scale.z);
        store_le32(record + 56,
                   visibility ? (uint32_t)visibility->fallback : 1u);
        store_le32(record + 60, (uint32_t)transform->rotation_mode);
    }

    key_count = 0;
    for(track_index = 0; track_index < track_count; ++track_index) {
        const anim_track_t *track = &track_refs[track_index]->track;
        uint8_t *track_record = bytes +
            PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + transform_bytes +
            track_index * PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES;
        size_t key_index;

        store_le16(track_record, (uint16_t)track->kind);
        store_le16(track_record + 2, (uint16_t)track->interpolation);
        store_le32(track_record + 4, (uint32_t)key_count);
        store_le32(track_record + 8, (uint32_t)track->key_count);
        for(key_index = 0; key_index < track->key_count; ++key_index) {
            const uint8_t *source = (const uint8_t *)track->keys +
                                    key_index * track->stride;
            uint8_t *key_record = bytes +
                PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES + transform_bytes +
                track_bytes + (key_count + key_index) *
                    PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES;
            float time;

            memcpy(&time, source, sizeof(time));
            store_float(key_record, time);
            switch(track->kind) {
                case ANIM_VALUE_SCALAR: {
                    if(track->interpolation ==
                       ANIM_INTERPOLATION_CUBIC_HERMITE) {
                        anim_scalar_hermite_key_t key;

                        memcpy(&key, source, sizeof(key));
                        store_float(key_record + 4, key.value);
                        store_float(key_record + 20, key.in_tangent);
                        store_float(key_record + 36, key.out_tangent);
                    }
                    else {
                        anim_scalar_key_t key;

                        memcpy(&key, source, sizeof(key));
                        store_float(key_record + 4, key.value);
                    }
                    break;
                }

                case ANIM_VALUE_VECTOR: {
                    anim_vector_hermite_key_t key;

                    memset(&key, 0, sizeof(key));
                    memcpy(&key, source,
                           track->interpolation ==
                               ANIM_INTERPOLATION_CUBIC_HERMITE ?
                               sizeof(key) : sizeof(anim_vector_key_t));
                    store_float(key_record + 4, key.value.x);
                    store_float(key_record + 8, key.value.y);
                    store_float(key_record + 12, key.value.z);
                    store_float(key_record + 16, key.value.w);
                    if(track->interpolation ==
                       ANIM_INTERPOLATION_CUBIC_HERMITE) {
                        store_float(key_record + 20, key.in_tangent.x);
                        store_float(key_record + 24, key.in_tangent.y);
                        store_float(key_record + 28, key.in_tangent.z);
                        store_float(key_record + 32, key.in_tangent.w);
                        store_float(key_record + 36, key.out_tangent.x);
                        store_float(key_record + 40, key.out_tangent.y);
                        store_float(key_record + 44, key.out_tangent.z);
                        store_float(key_record + 48, key.out_tangent.w);
                    }
                    break;
                }

                case ANIM_VALUE_QUATERNION: {
                    anim_quaternion_hermite_key_t key;

                    memset(&key, 0, sizeof(key));
                    memcpy(&key, source,
                           track->interpolation ==
                               ANIM_INTERPOLATION_CUBIC_HERMITE ?
                               sizeof(key) :
                               sizeof(anim_quaternion_key_t));
                    store_float(key_record + 4, key.value.w);
                    store_float(key_record + 8, key.value.x);
                    store_float(key_record + 12, key.value.y);
                    store_float(key_record + 16, key.value.z);
                    if(track->interpolation ==
                       ANIM_INTERPOLATION_CUBIC_HERMITE) {
                        store_float(key_record + 20, key.in_tangent.w);
                        store_float(key_record + 24, key.in_tangent.x);
                        store_float(key_record + 28, key.in_tangent.y);
                        store_float(key_record + 32, key.in_tangent.z);
                        store_float(key_record + 36, key.out_tangent.w);
                        store_float(key_record + 40, key.out_tangent.x);
                        store_float(key_record + 44, key.out_tangent.y);
                        store_float(key_record + 48, key.out_tangent.z);
                    }
                    break;
                }

                case ANIM_VALUE_BOOLEAN: {
                    anim_boolean_key_t key;

                    memcpy(&key, source, sizeof(key));
                    store_le32(key_record + 4, key.value);
                    break;
                }
            }
        }
        key_count += track->key_count;
    }

    store_le32(bytes, PVR_CHUNK_ANIMATION_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_ANIMATION_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)source->transform_count);
    store_le32(bytes + 16, (uint32_t)track_count);
    store_le32(bytes + 20, (uint32_t)key_count);
    store_le16(bytes + 24,
               PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES);
    store_le16(bytes + 26, PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES);
    store_le16(bytes + 28, PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES);
    store_le32(bytes + 32, (uint32_t)transform_bytes);
    store_le32(bytes + 36, (uint32_t)track_bytes);
    store_le32(bytes + 40, (uint32_t)key_bytes);
    store_float(bytes + 44, source->start_time);
    store_float(bytes + 48, source->end_time);
    store_le32(bytes + 52, crc32_bytes(
        bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));

    if(pvr_chunk_animation_section_open(bytes, file_bytes, &section) < 0)
        goto fail;
    free(track_refs);
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail: {
        int saved_errno = errno ? errno : EIO;

        free(bytes);
        free(track_refs);
        errno = saved_errno;
        return -1;
    }
}
