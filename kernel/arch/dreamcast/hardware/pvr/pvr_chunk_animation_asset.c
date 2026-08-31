/* KallistiOS ##version##

   dc/pvr/pvr_chunk_animation_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_animation_asset.h>

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * PAT1 is deliberately a three-array format: fixed transform bindings,
 * fixed track spans, then fixed canonical keys. Transform records store track
 * ordinals rather than pointers; materialization resolves those ordinals into
 * the existing animation runtime after the complete byte image is admitted.
 * A canonical key reserves four value words for every kind so spans remain
 * directly indexable even when neighboring tracks use different value kinds.
 */
enum {
    HEADER_PAYLOAD_CRC_OFFSET = 52,
    HEADER_RESERVED_OFFSET = 56,
    HEADER_CRC_OFFSET = 60,
    HEADER_CRC_BYTES = 60,
    TRANSFORM_TRANSLATION_OFFSET = 0,
    TRANSFORM_ROTATION_OFFSET = 4,
    TRANSFORM_SCALE_OFFSET = 8,
    TRANSFORM_VISIBILITY_OFFSET = 12,
    TRANSFORM_FALLBACK_TRANSLATION_OFFSET = 16,
    TRANSFORM_FALLBACK_ROTATION_OFFSET = 28,
    TRANSFORM_FALLBACK_SCALE_OFFSET = 44,
    TRANSFORM_FALLBACK_VISIBLE_OFFSET = 56,
    TRANSFORM_RESERVED_OFFSET = 60,
    TRACK_KIND_OFFSET = 0,
    TRACK_INTERPOLATION_OFFSET = 2,
    TRACK_FIRST_KEY_OFFSET = 4,
    TRACK_KEY_COUNT_OFFSET = 8,
    TRACK_RESERVED_OFFSET = 12,
    KEY_TIME_OFFSET = 0,
    KEY_VALUE_OFFSET = 4,
    KEY_RESERVED_OFFSET = 20
};

_Static_assert(offsetof(pvr_chunk_animation_key_t, value) == sizeof(float),
               "canonical animation key layout");
_Static_assert((sizeof(pvr_chunk_animation_key_t) & 3u) == 0,
               "canonical animation key stride");

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

static int quaternion_valid(const anim_quaternion_t *quaternion) {
    float magnitude_squared;

    if(!isfinite(quaternion->w) || !isfinite(quaternion->x) ||
       !isfinite(quaternion->y) || !isfinite(quaternion->z))
        return 0;
    magnitude_squared = quaternion->w * quaternion->w +
                        quaternion->x * quaternion->x +
                        quaternion->y * quaternion->y +
                        quaternion->z * quaternion->z;
    return isfinite(magnitude_squared) && magnitude_squared > FLT_MIN;
}

static int transform_fallback_valid(const anim_transform_t *fallback) {
    return isfinite(fallback->translation.x) &&
           isfinite(fallback->translation.y) &&
           isfinite(fallback->translation.z) &&
           quaternion_valid(&fallback->rotation) &&
           isfinite(fallback->scale.x) && isfinite(fallback->scale.y) &&
           isfinite(fallback->scale.z);
}

static int decode_track(const uint8_t *record,
                        pvr_chunk_animation_section_track_t *track) {
    uint16_t kind = read_le16(record + TRACK_KIND_OFFSET);
    uint16_t interpolation = read_le16(
        record + TRACK_INTERPOLATION_OFFSET);

    if(kind > ANIM_VALUE_BOOLEAN ||
       interpolation > ANIM_INTERPOLATION_CATMULL_ROM ||
       (kind == ANIM_VALUE_BOOLEAN &&
        interpolation != ANIM_INTERPOLATION_STEP) ||
       ((kind == ANIM_VALUE_QUATERNION || kind == ANIM_VALUE_BOOLEAN) &&
        interpolation == ANIM_INTERPOLATION_CATMULL_ROM) ||
       read_le32(record + TRACK_RESERVED_OFFSET)) {
        errno = EILSEQ;
        return -1;
    }
    track->kind = (anim_value_kind_t)kind;
    track->interpolation = (anim_interpolation_t)interpolation;
    track->first_key = read_le32(record + TRACK_FIRST_KEY_OFFSET);
    track->key_count = read_le32(record + TRACK_KEY_COUNT_OFFSET);
    return 0;
}

static int decode_key(const uint8_t *record, anim_value_kind_t kind,
                      pvr_chunk_animation_key_t *key) {
    float time = read_float(record + KEY_TIME_OFFSET);

    if(!isfinite(time) || read_le32(record + KEY_RESERVED_OFFSET)) {
        errno = EILSEQ;
        return -1;
    }
    memset(key, 0, sizeof(*key));
    key->time = time;
    switch(kind) {
        case ANIM_VALUE_SCALAR:
            key->value.scalar = read_float(record + KEY_VALUE_OFFSET);
            if(!isfinite(key->value.scalar) ||
               read_le32(record + KEY_VALUE_OFFSET + 4) ||
               read_le32(record + KEY_VALUE_OFFSET + 8) ||
               read_le32(record + KEY_VALUE_OFFSET + 12)) {
                errno = EILSEQ;
                return -1;
            }
            break;

        case ANIM_VALUE_VECTOR:
            key->value.vector.x = read_float(record + KEY_VALUE_OFFSET);
            key->value.vector.y = read_float(record + KEY_VALUE_OFFSET + 4);
            key->value.vector.z = read_float(record + KEY_VALUE_OFFSET + 8);
            key->value.vector.w = read_float(record + KEY_VALUE_OFFSET + 12);
            if(!isfinite(key->value.vector.x) ||
               !isfinite(key->value.vector.y) ||
               !isfinite(key->value.vector.z) ||
               !isfinite(key->value.vector.w)) {
                errno = EILSEQ;
                return -1;
            }
            break;

        case ANIM_VALUE_QUATERNION:
            key->value.quaternion.w = read_float(record + KEY_VALUE_OFFSET);
            key->value.quaternion.x = read_float(
                record + KEY_VALUE_OFFSET + 4);
            key->value.quaternion.y = read_float(
                record + KEY_VALUE_OFFSET + 8);
            key->value.quaternion.z = read_float(
                record + KEY_VALUE_OFFSET + 12);
            if(!quaternion_valid(&key->value.quaternion)) {
                errno = EILSEQ;
                return -1;
            }
            break;

        case ANIM_VALUE_BOOLEAN:
            key->value.boolean = read_le32(record + KEY_VALUE_OFFSET);
            if(key->value.boolean > 1u ||
               read_le32(record + KEY_VALUE_OFFSET + 4) ||
               read_le32(record + KEY_VALUE_OFFSET + 8) ||
               read_le32(record + KEY_VALUE_OFFSET + 12)) {
                errno = EILSEQ;
                return -1;
            }
            break;

        default:
            errno = EILSEQ;
            return -1;
    }
    return 0;
}

static int decode_transform(
    const uint8_t *record,
    pvr_chunk_animation_section_transform_t *transform) {
    transform->translation_track = read_le32(
        record + TRANSFORM_TRANSLATION_OFFSET);
    transform->rotation_track = read_le32(
        record + TRANSFORM_ROTATION_OFFSET);
    transform->scale_track = read_le32(record + TRANSFORM_SCALE_OFFSET);
    transform->visibility_track = read_le32(
        record + TRANSFORM_VISIBILITY_OFFSET);
    transform->fallback.translation.x = read_float(
        record + TRANSFORM_FALLBACK_TRANSLATION_OFFSET);
    transform->fallback.translation.y = read_float(
        record + TRANSFORM_FALLBACK_TRANSLATION_OFFSET + 4);
    transform->fallback.translation.z = read_float(
        record + TRANSFORM_FALLBACK_TRANSLATION_OFFSET + 8);
    transform->fallback.translation.w = 1.0f;
    transform->fallback.rotation.w = read_float(
        record + TRANSFORM_FALLBACK_ROTATION_OFFSET);
    transform->fallback.rotation.x = read_float(
        record + TRANSFORM_FALLBACK_ROTATION_OFFSET + 4);
    transform->fallback.rotation.y = read_float(
        record + TRANSFORM_FALLBACK_ROTATION_OFFSET + 8);
    transform->fallback.rotation.z = read_float(
        record + TRANSFORM_FALLBACK_ROTATION_OFFSET + 12);
    transform->fallback.scale.x = read_float(
        record + TRANSFORM_FALLBACK_SCALE_OFFSET);
    transform->fallback.scale.y = read_float(
        record + TRANSFORM_FALLBACK_SCALE_OFFSET + 4);
    transform->fallback.scale.z = read_float(
        record + TRANSFORM_FALLBACK_SCALE_OFFSET + 8);
    transform->fallback.scale.w = 0.0f;
    transform->fallback_visible = read_le32(
        record + TRANSFORM_FALLBACK_VISIBLE_OFFSET);
    if(read_le32(record + TRANSFORM_RESERVED_OFFSET) ||
       transform->fallback_visible > 1u ||
       !transform_fallback_valid(&transform->fallback)) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int track_reference_valid(
    uint32_t ordinal, anim_value_kind_t expected,
    const pvr_chunk_animation_section_view_t *view) {
    pvr_chunk_animation_section_track_t track;

    if(ordinal == PVR_CHUNK_ANIMATION_TRACK_NONE)
        return 1;
    if(ordinal >= view->track_count ||
       decode_track((const uint8_t *)view->tracks + ordinal *
                        PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
                    &track) < 0 || track.kind != expected)
        return 0;
    if(expected == ANIM_VALUE_BOOLEAN &&
       track.interpolation != ANIM_INTERPOLATION_STEP)
        return 0;
    return 1;
}

int pvr_chunk_animation_section_open(
    const void *data, size_t size,
    pvr_chunk_animation_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_animation_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t transform_count;
    uint32_t track_count;
    uint32_t key_count;
    uint32_t transform_bytes;
    uint32_t track_bytes;
    uint32_t key_bytes;
    uint64_t encoded_payload_bytes;
    size_t payload_bytes;
    size_t next_key = 0;
    size_t index;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_ANIMATION_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_ANIMATION_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES ||
       read_le16(bytes + 24) !=
           PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES ||
       read_le16(bytes + 26) != PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES ||
       read_le16(bytes + 28) != PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES ||
       read_le16(bytes + 30) ||
       read_le32(bytes + HEADER_RESERVED_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    transform_count = read_le32(bytes + 12);
    track_count = read_le32(bytes + 16);
    key_count = read_le32(bytes + 20);
    transform_bytes = read_le32(bytes + 32);
    track_bytes = read_le32(bytes + 36);
    key_bytes = read_le32(bytes + 40);
    if(file_bytes != size || !transform_count ||
       (track_count == 0) != (key_count == 0) ||
       transform_count >
           UINT32_MAX / PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES ||
       track_count > UINT32_MAX / PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES ||
       key_count > UINT32_MAX / PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES ||
       transform_bytes != transform_count *
           PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES ||
       track_bytes != track_count * PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES ||
       key_bytes != key_count * PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    encoded_payload_bytes = (uint64_t)transform_bytes + track_bytes +
                            key_bytes;
    if(encoded_payload_bytes >
       UINT32_MAX - PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    payload_bytes = (size_t)encoded_payload_bytes;
    if(payload_bytes != file_bytes -
                            PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES,
           payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.transforms = bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES;
    parsed.transform_count = transform_count;
    parsed.tracks = bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                    transform_bytes;
    parsed.track_count = track_count;
    parsed.keys = bytes + PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES +
                  transform_bytes + track_bytes;
    parsed.key_count = key_count;
    parsed.start_time = read_float(bytes + 44);
    parsed.end_time = read_float(bytes + 48);
    parsed.version = PVR_CHUNK_ANIMATION_SECTION_VERSION;
    if(!isfinite(parsed.start_time) || !isfinite(parsed.end_time) ||
       parsed.start_time >= parsed.end_time) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < parsed.track_count; ++index) {
        pvr_chunk_animation_section_track_t track;
        float previous_time = 0.0f;
        size_t key_index;

        if(decode_track((const uint8_t *)parsed.tracks + index *
                            PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
                        &track) < 0 || !track.key_count ||
           track.first_key != next_key ||
           track.key_count > parsed.key_count - next_key) {
            errno = EILSEQ;
            return -1;
        }
        for(key_index = 0; key_index < track.key_count; ++key_index) {
            pvr_chunk_animation_key_t key;

            if(decode_key((const uint8_t *)parsed.keys +
                              (next_key + key_index) *
                                  PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES,
                          track.kind, &key) < 0 ||
               (key_index && key.time <= previous_time)) {
                errno = EILSEQ;
                return -1;
            }
            previous_time = key.time;
        }
        next_key += track.key_count;
    }
    if(next_key != parsed.key_count) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < parsed.transform_count; ++index) {
        pvr_chunk_animation_section_transform_t transform;

        if(decode_transform((const uint8_t *)parsed.transforms + index *
                                PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES,
                            &transform) < 0 ||
           !track_reference_valid(transform.translation_track,
                                  ANIM_VALUE_VECTOR, &parsed) ||
           !track_reference_valid(transform.rotation_track,
                                  ANIM_VALUE_QUATERNION, &parsed) ||
           !track_reference_valid(transform.scale_track,
                                  ANIM_VALUE_VECTOR, &parsed) ||
           !track_reference_valid(transform.visibility_track,
                                  ANIM_VALUE_BOOLEAN, &parsed)) {
            errno = EILSEQ;
            return -1;
        }
    }

    *view = parsed;
    return 0;
}

int pvr_chunk_animation_section_transform_get(
    const pvr_chunk_animation_section_view_t *view, size_t index,
    pvr_chunk_animation_section_transform_t *transform) {
    pvr_chunk_animation_section_view_t checked;

    if(!view || !transform || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.transform_count) {
        errno = ENOENT;
        return -1;
    }
    return decode_transform(
        (const uint8_t *)checked.transforms + index *
            PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES,
        transform);
}

int pvr_chunk_animation_section_track_get(
    const pvr_chunk_animation_section_view_t *view, size_t index,
    pvr_chunk_animation_section_track_t *track) {
    pvr_chunk_animation_section_view_t checked;

    if(!view || !track || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.track_count) {
        errno = ENOENT;
        return -1;
    }
    return decode_track((const uint8_t *)checked.tracks + index *
                            PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
                        track);
}

int pvr_chunk_animation_section_materialize(
    const pvr_chunk_animation_section_view_t *view,
    pvr_chunk_animation_key_t *keys, size_t key_capacity,
    anim_track_view_t *tracks, size_t track_capacity,
    anim_transform_tracks_t *transforms, size_t transform_capacity,
    anim_visibility_tracks_t *visibility, size_t visibility_capacity,
    anim_clip_view_t *clip) {
    pvr_chunk_animation_section_view_t checked;
    size_t key_bytes;
    size_t track_bytes;
    size_t transform_bytes;
    size_t visibility_bytes;
    size_t index;

    if(!view || !view->data || !clip ||
       (keys && ((uintptr_t)keys &
                 (_Alignof(pvr_chunk_animation_key_t) - 1u))) ||
       (tracks && ((uintptr_t)tracks &
                   (_Alignof(anim_track_view_t) - 1u))) ||
       (transforms && ((uintptr_t)transforms &
                       (_Alignof(anim_transform_tracks_t) - 1u))) ||
       (visibility && ((uintptr_t)visibility &
                       (_Alignof(anim_visibility_tracks_t) - 1u))) ||
       ((uintptr_t)clip & (_Alignof(anim_clip_view_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if((checked.key_count && !keys) ||
       (checked.track_count && !tracks) ||
       (checked.transform_count && (!transforms || !visibility))) {
        errno = EINVAL;
        return -1;
    }
    if(key_capacity < checked.key_count ||
       track_capacity < checked.track_count ||
       transform_capacity < checked.transform_count ||
       visibility_capacity < checked.transform_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.key_count > SIZE_MAX / sizeof(*keys) ||
       checked.track_count > SIZE_MAX / sizeof(*tracks) ||
       checked.transform_count > SIZE_MAX / sizeof(*transforms) ||
       checked.transform_count > SIZE_MAX / sizeof(*visibility)) {
        errno = EOVERFLOW;
        return -1;
    }
    key_bytes = checked.key_count * sizeof(*keys);
    track_bytes = checked.track_count * sizeof(*tracks);
    transform_bytes = checked.transform_count * sizeof(*transforms);
    visibility_bytes = checked.transform_count * sizeof(*visibility);
    if(ranges_overlap(keys, key_bytes, tracks, track_bytes) ||
       ranges_overlap(keys, key_bytes, transforms, transform_bytes) ||
       ranges_overlap(keys, key_bytes, visibility, visibility_bytes) ||
       ranges_overlap(tracks, track_bytes, transforms, transform_bytes) ||
       ranges_overlap(tracks, track_bytes, visibility, visibility_bytes) ||
       ranges_overlap(transforms, transform_bytes,
                      visibility, visibility_bytes) ||
       ranges_overlap(keys, key_bytes, checked.data, checked.size) ||
       ranges_overlap(tracks, track_bytes, checked.data, checked.size) ||
       ranges_overlap(transforms, transform_bytes,
                      checked.data, checked.size) ||
       ranges_overlap(visibility, visibility_bytes,
                      checked.data, checked.size) ||
       ranges_overlap(clip, sizeof(*clip), checked.data, checked.size) ||
       ranges_overlap(clip, sizeof(*clip), keys, key_bytes) ||
       ranges_overlap(clip, sizeof(*clip), tracks, track_bytes) ||
       ranges_overlap(clip, sizeof(*clip), transforms, transform_bytes) ||
       ranges_overlap(clip, sizeof(*clip), visibility, visibility_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(index = 0; index < checked.track_count; ++index) {
        pvr_chunk_animation_section_track_t track;
        size_t key_index;

        decode_track((const uint8_t *)checked.tracks + index *
                         PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES,
                     &track);
        for(key_index = 0; key_index < track.key_count; ++key_index)
            decode_key((const uint8_t *)checked.keys +
                           (track.first_key + key_index) *
                               PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES,
                       track.kind, keys + track.first_key + key_index);
        tracks[index].track.kind = track.kind;
        tracks[index].track.interpolation = track.interpolation;
        tracks[index].track.keys = keys + track.first_key;
        tracks[index].track.key_count = track.key_count;
        tracks[index].track.stride = sizeof(*keys);
        tracks[index].start_time = keys[track.first_key].time;
        tracks[index].end_time = keys[track.first_key +
                                      track.key_count - 1u].time;
    }

    for(index = 0; index < checked.transform_count; ++index) {
        pvr_chunk_animation_section_transform_t transform;

        decode_transform((const uint8_t *)checked.transforms + index *
                             PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES,
                         &transform);
        transforms[index].translation =
            transform.translation_track == PVR_CHUNK_ANIMATION_TRACK_NONE ?
                NULL : tracks + transform.translation_track;
        transforms[index].rotation =
            transform.rotation_track == PVR_CHUNK_ANIMATION_TRACK_NONE ?
                NULL : tracks + transform.rotation_track;
        transforms[index].scale =
            transform.scale_track == PVR_CHUNK_ANIMATION_TRACK_NONE ?
                NULL : tracks + transform.scale_track;
        transforms[index].fallback = transform.fallback;
        visibility[index].visible =
            transform.visibility_track == PVR_CHUNK_ANIMATION_TRACK_NONE ?
                NULL : tracks + transform.visibility_track;
        visibility[index].fallback = transform.fallback_visible != 0;
    }

    clip->clip.transforms = transforms;
    clip->clip.transform_count = checked.transform_count;
    clip->clip.start_time = checked.start_time;
    clip->clip.end_time = checked.end_time;
    clip->clip.visibility = visibility;
    return 0;
}
