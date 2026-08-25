/* KallistiOS ##version##

   animation.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/animation.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int finite4(float x, float y, float z, float w) {
    return isfinite(x) && isfinite(y) && isfinite(z) && isfinite(w);
}

static size_t key_size(anim_value_kind_t kind) {
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

static const void *key_at(const anim_track_t *track, size_t index) {
    return (const uint8_t *)track->keys + index * track->stride;
}

static float key_time(const anim_track_t *track, size_t index) {
    float time;

    memcpy(&time, key_at(track, index), sizeof(time));
    return time;
}

static int quaternion_valid(const anim_quaternion_t *quaternion) {
    float magnitude_squared;

    if(!quaternion || !finite4(quaternion->w, quaternion->x,
                               quaternion->y, quaternion->z))
        return 0;
    magnitude_squared = quaternion->w * quaternion->w +
                        quaternion->x * quaternion->x +
                        quaternion->y * quaternion->y +
                        quaternion->z * quaternion->z;
    return isfinite(magnitude_squared) && magnitude_squared > FLT_MIN;
}

static int key_value_valid(const anim_track_t *track, size_t index) {
    const void *key = key_at(track, index);

    switch(track->kind) {
        case ANIM_VALUE_SCALAR: {
            const anim_scalar_key_t *scalar = key;
            return isfinite(scalar->value);
        }

        case ANIM_VALUE_VECTOR: {
            const anim_vector_key_t *vector = key;
            return finite4(vector->value.x, vector->value.y,
                           vector->value.z, vector->value.w);
        }

        case ANIM_VALUE_QUATERNION: {
            const anim_quaternion_key_t *quaternion = key;
            return quaternion_valid(&quaternion->value);
        }

        case ANIM_VALUE_BOOLEAN: {
            const anim_boolean_key_t *boolean = key;
            return boolean->value <= 1u;
        }

        default:
            return 0;
    }
}

int anim_track_open(const anim_track_t *track, anim_track_view_t *output) {
    anim_track_view_t view;
    size_t minimum_size;
    float previous_time = 0.0f;
    size_t i;

    if(!track || !output || !track->keys || !track->key_count ||
       ((uintptr_t)track->keys & 3u) ||
       (track->interpolation != ANIM_INTERPOLATION_STEP &&
        track->interpolation != ANIM_INTERPOLATION_LINEAR)) {
        errno = EINVAL;
        return -1;
    }

    minimum_size = key_size(track->kind);
    if(!minimum_size || track->stride < minimum_size ||
       (track->stride & 3u) ||
       (track->kind == ANIM_VALUE_BOOLEAN &&
        track->interpolation != ANIM_INTERPOLATION_STEP)) {
        errno = EINVAL;
        return -1;
    }

    if(track->key_count - 1u >
       (SIZE_MAX - minimum_size) / track->stride ||
       (track->key_count - 1u) * track->stride + minimum_size >
       UINTPTR_MAX - (uintptr_t)track->keys) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < track->key_count; ++i) {
        float time = key_time(track, i);

        if(!isfinite(time) || !key_value_valid(track, i)) {
            errno = EDOM;
            return -1;
        }
        if(i && time <= previous_time) {
            errno = EILSEQ;
            return -1;
        }
        previous_time = time;
    }

    view.track = *track;
    view.start_time = key_time(track, 0);
    view.end_time = previous_time;
    memcpy(output, &view, sizeof(view));
    return 0;
}

static int view_valid(const anim_track_view_t *view,
                      anim_value_kind_t expected_kind) {
    const anim_track_t *track;
    size_t minimum_size;

    if(!view)
        return 0;
    track = &view->track;
    minimum_size = key_size(expected_kind);
    if(track->kind != expected_kind || !track->keys || !track->key_count ||
       ((uintptr_t)track->keys & 3u) ||
       (track->interpolation != ANIM_INTERPOLATION_STEP &&
        track->interpolation != ANIM_INTERPOLATION_LINEAR) ||
       track->stride < minimum_size || (track->stride & 3u) ||
       !isfinite(view->start_time) || !isfinite(view->end_time) ||
       view->start_time > view->end_time ||
       track->key_count - 1u >
       (SIZE_MAX - minimum_size) / track->stride ||
       (track->key_count - 1u) * track->stride + minimum_size >
       UINTPTR_MAX - (uintptr_t)track->keys)
        return 0;

    return key_time(track, 0) == view->start_time &&
           key_time(track, track->key_count - 1u) == view->end_time;
}

static int sample_interval(const anim_track_view_t *view, float time,
                           size_t *lower, size_t *upper, float *factor,
                           anim_sample_info_t *info) {
    const anim_track_t *track = &view->track;
    anim_sample_info_t interval = { 0, 0, 0.0f };
    size_t low;
    size_t high;

    if(info)
        *info = interval;

    if(!isfinite(time)) {
        errno = EDOM;
        return -1;
    }

    if(time <= view->start_time || track->key_count == 1) {
        *lower = 0;
        *upper = 0;
        *factor = 0.0f;
        if(info)
            *info = interval;
        return 0;
    }

    if(time >= view->end_time) {
        interval.lower_key = track->key_count - 1u;
        interval.upper_key = interval.lower_key;
        *lower = interval.lower_key;
        *upper = interval.upper_key;
        *factor = 0.0f;
        if(info)
            *info = interval;
        return 0;
    }

    low = 0;
    high = track->key_count - 1u;
    while(high - low > 1u) {
        size_t middle = low + (high - low) / 2u;

        if(time < key_time(track, middle))
            high = middle;
        else
            low = middle;
    }

    if(time == key_time(track, low)) {
        high = low;
        *factor = 0.0f;
    }
    else if(track->interpolation == ANIM_INTERPOLATION_STEP) {
        high = low;
        *factor = 0.0f;
    }
    else {
        float low_time = key_time(track, low);
        float high_time = key_time(track, high);

        *factor = (time - low_time) / (high_time - low_time);
        if(!isfinite(*factor) || *factor < 0.0f || *factor > 1.0f) {
            errno = EILSEQ;
            return -1;
        }
    }

    *lower = low;
    *upper = high;
    interval.lower_key = low;
    interval.upper_key = high;
    interval.factor = *factor;
    if(info)
        *info = interval;
    return 0;
}

int anim_track_sample_scalar(const anim_track_view_t *view, float time,
                             float *output, anim_sample_info_t *info) {
    size_t lower;
    size_t upper;
    float factor;
    float value;

    if(!output || !view_valid(view, ANIM_VALUE_SCALAR)) {
        errno = EINVAL;
        return -1;
    }
    if(sample_interval(view, time, &lower, &upper, &factor, info) < 0)
        return -1;

    value = ((const anim_scalar_key_t *)key_at(&view->track, lower))->value;
    if(upper != lower) {
        float next = ((const anim_scalar_key_t *)
                      key_at(&view->track, upper))->value;
        value += (next - value) * factor;
    }
    if(!isfinite(value)) {
        errno = ERANGE;
        return -1;
    }
    *output = value;
    return 0;
}

int anim_track_sample_vector(const anim_track_view_t *view, float time,
                             vector_t *output, anim_sample_info_t *info) {
    size_t lower;
    size_t upper;
    float factor;
    vector_t value;

    if(!output || !view_valid(view, ANIM_VALUE_VECTOR)) {
        errno = EINVAL;
        return -1;
    }
    if(sample_interval(view, time, &lower, &upper, &factor, info) < 0)
        return -1;

    value = ((const anim_vector_key_t *)key_at(&view->track, lower))->value;
    if(upper != lower) {
        const vector_t *next = &((const anim_vector_key_t *)
                                 key_at(&view->track, upper))->value;
#ifdef __DREAMCAST__
        shz_vec4_t interpolated = shz_vec4_lerp(
            shz_vec4_init(value.x, value.y, value.z, value.w),
            shz_vec4_init(next->x, next->y, next->z, next->w), factor);

        value.x = interpolated.x;
        value.y = interpolated.y;
        value.z = interpolated.z;
        value.w = interpolated.w;
#else
        value.x += (next->x - value.x) * factor;
        value.y += (next->y - value.y) * factor;
        value.z += (next->z - value.z) * factor;
        value.w += (next->w - value.w) * factor;
#endif
    }
    if(!finite4(value.x, value.y, value.z, value.w)) {
        errno = ERANGE;
        return -1;
    }
    memcpy(output, &value, sizeof(value));
    return 0;
}

int anim_track_sample_boolean(const anim_track_view_t *view, float time,
                              bool *output, anim_sample_info_t *info) {
    const anim_boolean_key_t *key;
    size_t lower;
    size_t upper;
    float factor;

    if(!output || !view_valid(view, ANIM_VALUE_BOOLEAN) ||
       view->track.interpolation != ANIM_INTERPOLATION_STEP) {
        errno = EINVAL;
        return -1;
    }
    if(sample_interval(view, time, &lower, &upper, &factor, info) < 0)
        return -1;

    (void)upper;
    (void)factor;
    key = key_at(&view->track, lower);
    if(key->value > 1u) {
        errno = EILSEQ;
        return -1;
    }
    *output = key->value != 0;
    return 0;
}

static int event_track_valid(const anim_event_track_view_t *view) {
    const anim_event_track_t *track;
    size_t bytes;

    if(!view)
        return 0;
    track = &view->track;
    if(!track->events || !track->event_count ||
       ((uintptr_t)track->events &
        (_Alignof(anim_event_key_t) - 1u)) ||
       !isfinite(view->start_time) || !isfinite(view->end_time) ||
       view->start_time > view->end_time ||
       track->event_count > SIZE_MAX / sizeof(*track->events))
        return 0;
    bytes = track->event_count * sizeof(*track->events);
    if(bytes > UINTPTR_MAX - (uintptr_t)track->events)
        return 0;
    return track->events[0].time == view->start_time &&
           track->events[track->event_count - 1u].time == view->end_time;
}

int anim_event_track_open(const anim_event_track_t *track,
                          anim_event_track_view_t *output) {
    anim_event_track_view_t view;
    float previous = 0.0f;
    size_t i;

    if(!track || !output || !track->events || !track->event_count ||
       ((uintptr_t)track->events &
        (_Alignof(anim_event_key_t) - 1u)) ||
       track->event_count > SIZE_MAX / sizeof(*track->events) ||
       track->event_count * sizeof(*track->events) >
       UINTPTR_MAX - (uintptr_t)track->events) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < track->event_count; ++i) {
        float time = track->events[i].time;

        if(!isfinite(time)) {
            errno = EDOM;
            return -1;
        }
        if(i && time <= previous) {
            errno = EILSEQ;
            return -1;
        }
        previous = time;
    }

    view.track = *track;
    view.start_time = track->events[0].time;
    view.end_time = previous;
    memcpy(output, &view, sizeof(view));
    return 0;
}

static int quaternion_normalize(const anim_quaternion_t *source,
                                anim_quaternion_t *output) {
    anim_quaternion_t normalized;

    if(!quaternion_valid(source)) {
        errno = EDOM;
        return -1;
    }

#ifdef __DREAMCAST__
    {
        shz_quat_t value = shz_quat_normalize(shz_quat_init(
            source->w, source->x, source->y, source->z));

        normalized.w = value.w;
        normalized.x = value.x;
        normalized.y = value.y;
        normalized.z = value.z;
    }
#else
    {
        float magnitude_squared = source->w * source->w +
                                  source->x * source->x +
                                  source->y * source->y +
                                  source->z * source->z;
        float reciprocal_magnitude = 1.0f / sqrtf(magnitude_squared);

        normalized.w = source->w * reciprocal_magnitude;
        normalized.x = source->x * reciprocal_magnitude;
        normalized.y = source->y * reciprocal_magnitude;
        normalized.z = source->z * reciprocal_magnitude;
    }
#endif

    if(!finite4(normalized.w, normalized.x, normalized.y, normalized.z)) {
        errno = ERANGE;
        return -1;
    }
    *output = normalized;
    return 0;
}

static int quaternion_slerp(const anim_quaternion_t *from,
                            const anim_quaternion_t *to, float factor,
                            anim_quaternion_t *output) {
    anim_quaternion_t lhs;
    anim_quaternion_t rhs;
    anim_quaternion_t result;

    if(quaternion_normalize(from, &lhs) < 0 ||
       quaternion_normalize(to, &rhs) < 0)
        return -1;

#ifdef __DREAMCAST__
    {
        shz_quat_t value = shz_quat_slerp(
            shz_quat_init(lhs.w, lhs.x, lhs.y, lhs.z),
            shz_quat_init(rhs.w, rhs.x, rhs.y, rhs.z), factor);

        result.w = value.w;
        result.x = value.x;
        result.y = value.y;
        result.z = value.z;
    }
#else
    {
        float dot = lhs.w * rhs.w + lhs.x * rhs.x +
                    lhs.y * rhs.y + lhs.z * rhs.z;

        if(dot < 0.0f) {
            lhs.w = -lhs.w;
            lhs.x = -lhs.x;
            lhs.y = -lhs.y;
            lhs.z = -lhs.z;
            dot = -dot;
        }
        if(dot > 1.0f)
            dot = 1.0f;

        if(dot > 0.9995f) {
            result.w = lhs.w + (rhs.w - lhs.w) * factor;
            result.x = lhs.x + (rhs.x - lhs.x) * factor;
            result.y = lhs.y + (rhs.y - lhs.y) * factor;
            result.z = lhs.z + (rhs.z - lhs.z) * factor;
        }
        else {
            float angle = acosf(dot);
            float sine = sinf(angle);
            float lhs_weight;
            float rhs_weight;

            if(!isfinite(angle) || !isfinite(sine) ||
               fabsf(sine) <= FLT_MIN) {
                errno = ERANGE;
                return -1;
            }
            lhs_weight = sinf((1.0f - factor) * angle) / sine;
            rhs_weight = sinf(factor * angle) / sine;
            result.w = lhs.w * lhs_weight + rhs.w * rhs_weight;
            result.x = lhs.x * lhs_weight + rhs.x * rhs_weight;
            result.y = lhs.y * lhs_weight + rhs.y * rhs_weight;
            result.z = lhs.z * lhs_weight + rhs.z * rhs_weight;
        }
    }
#endif

    return quaternion_normalize(&result, output);
}

int anim_track_sample_quaternion(const anim_track_view_t *view, float time,
                                 anim_quaternion_t *output,
                                 anim_sample_info_t *info) {
    const anim_quaternion_t *lower_value;
    const anim_quaternion_t *upper_value;
    anim_quaternion_t sampled;
    size_t lower;
    size_t upper;
    float factor;

    if(!output || !view_valid(view, ANIM_VALUE_QUATERNION)) {
        errno = EINVAL;
        return -1;
    }
    if(sample_interval(view, time, &lower, &upper, &factor, info) < 0)
        return -1;

    lower_value = &((const anim_quaternion_key_t *)
                    key_at(&view->track, lower))->value;
    if(lower == upper) {
        if(quaternion_normalize(lower_value, &sampled) < 0)
            return -1;
    }
    else {
        upper_value = &((const anim_quaternion_key_t *)
                        key_at(&view->track, upper))->value;
        if(quaternion_slerp(lower_value, upper_value, factor, &sampled) < 0)
            return -1;
    }
    *output = sampled;
    return 0;
}

static int transform_valid(const anim_transform_t *transform) {
    return transform &&
           finite4(transform->translation.x, transform->translation.y,
                   transform->translation.z, 0.0f) &&
           finite4(transform->scale.x, transform->scale.y,
                   transform->scale.z, 0.0f) &&
           quaternion_valid(&transform->rotation);
}

int anim_transform_sample(const anim_transform_tracks_t *tracks, float time,
                          anim_transform_t *output) {
    anim_transform_t sampled;

    if(!tracks || !output || !isfinite(time) ||
       !transform_valid(&tracks->fallback)) {
        errno = EINVAL;
        return -1;
    }

    sampled = tracks->fallback;
    if(tracks->translation && anim_track_sample_vector(
           tracks->translation, time, &sampled.translation, NULL) < 0)
        return -1;
    if(tracks->rotation && anim_track_sample_quaternion(
           tracks->rotation, time, &sampled.rotation, NULL) < 0)
        return -1;
    if(tracks->scale && anim_track_sample_vector(
           tracks->scale, time, &sampled.scale, NULL) < 0)
        return -1;
    if(quaternion_normalize(&sampled.rotation, &sampled.rotation) < 0)
        return -1;
    sampled.translation.w = 1.0f;
    sampled.scale.w = 0.0f;
    *output = sampled;
    return 0;
}

int anim_transform_blend(const anim_transform_t *from,
                         const anim_transform_t *to, float weight,
                         anim_transform_t *output) {
    anim_transform_t blended;

    if(!output || !transform_valid(from) || !transform_valid(to) ||
       !isfinite(weight) || weight < 0.0f || weight > 1.0f) {
        errno = EINVAL;
        return -1;
    }

#ifdef __DREAMCAST__
    {
        shz_vec3_t translation = shz_vec3_lerp(
            shz_vec3_init(from->translation.x, from->translation.y,
                          from->translation.z),
            shz_vec3_init(to->translation.x, to->translation.y,
                          to->translation.z), weight);
        shz_vec3_t scale = shz_vec3_lerp(
            shz_vec3_init(from->scale.x, from->scale.y, from->scale.z),
            shz_vec3_init(to->scale.x, to->scale.y, to->scale.z), weight);

        blended.translation.x = translation.x;
        blended.translation.y = translation.y;
        blended.translation.z = translation.z;
        blended.scale.x = scale.x;
        blended.scale.y = scale.y;
        blended.scale.z = scale.z;
    }
#else
    blended.translation.x = from->translation.x +
        (to->translation.x - from->translation.x) * weight;
    blended.translation.y = from->translation.y +
        (to->translation.y - from->translation.y) * weight;
    blended.translation.z = from->translation.z +
        (to->translation.z - from->translation.z) * weight;
    blended.scale.x = from->scale.x + (to->scale.x - from->scale.x) * weight;
    blended.scale.y = from->scale.y + (to->scale.y - from->scale.y) * weight;
    blended.scale.z = from->scale.z + (to->scale.z - from->scale.z) * weight;
#endif
    blended.translation.w = 1.0f;
    blended.scale.w = 0.0f;
    if(quaternion_slerp(&from->rotation, &to->rotation, weight,
                        &blended.rotation) < 0)
        return -1;
    if(!transform_valid(&blended)) {
        errno = ERANGE;
        return -1;
    }
    *output = blended;
    return 0;
}

int anim_transform_matrix_build(const anim_transform_t *transform,
                                matrix_t *output) {
    anim_quaternion_t rotation;
    matrix_t matrix;

    if(!output || ((uintptr_t)output & (_Alignof(matrix_t) - 1u)) ||
       !transform_valid(transform)) {
        errno = EINVAL;
        return -1;
    }
    if(quaternion_normalize(&transform->rotation, &rotation) < 0)
        return -1;

#ifdef __DREAMCAST__
    {
        shz_mat4x4_t shz_matrix;

        shz_mat4x4_init_rotation_quat(
            &shz_matrix, shz_quat_init(rotation.w, rotation.x,
                                       rotation.y, rotation.z));
        shz_matrix.col[0].x *= transform->scale.x;
        shz_matrix.col[0].y *= transform->scale.x;
        shz_matrix.col[0].z *= transform->scale.x;
        shz_matrix.col[1].x *= transform->scale.y;
        shz_matrix.col[1].y *= transform->scale.y;
        shz_matrix.col[1].z *= transform->scale.y;
        shz_matrix.col[2].x *= transform->scale.z;
        shz_matrix.col[2].y *= transform->scale.z;
        shz_matrix.col[2].z *= transform->scale.z;
        shz_matrix.pos.x = transform->translation.x;
        shz_matrix.pos.y = transform->translation.y;
        shz_matrix.pos.z = transform->translation.z;
        shz_matrix.pos.w = 1.0f;
        shz_kos_matrix_export(&matrix, &shz_matrix);
    }
#else
    {
        float w = rotation.w;
        float x = rotation.x;
        float y = rotation.y;
        float z = rotation.z;

        matrix[0][0] = (1.0f - 2.0f * (y * y + z * z)) * transform->scale.x;
        matrix[0][1] = (2.0f * (x * y + w * z)) * transform->scale.x;
        matrix[0][2] = (2.0f * (x * z - w * y)) * transform->scale.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = (2.0f * (x * y - w * z)) * transform->scale.y;
        matrix[1][1] = (1.0f - 2.0f * (x * x + z * z)) * transform->scale.y;
        matrix[1][2] = (2.0f * (y * z + w * x)) * transform->scale.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = (2.0f * (x * z + w * y)) * transform->scale.z;
        matrix[2][1] = (2.0f * (y * z - w * x)) * transform->scale.z;
        matrix[2][2] = (1.0f - 2.0f * (x * x + y * y)) * transform->scale.z;
        matrix[2][3] = 0.0f;
        matrix[3][0] = transform->translation.x;
        matrix[3][1] = transform->translation.y;
        matrix[3][2] = transform->translation.z;
        matrix[3][3] = 1.0f;
    }
#endif

    if(!finite4(matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]) ||
       !finite4(matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]) ||
       !finite4(matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]) ||
       !finite4(matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3])) {
        errno = ERANGE;
        return -1;
    }
    memcpy(output, &matrix, sizeof(matrix));
    return 0;
}

static int clip_shallow_valid(const anim_clip_view_t *view) {
    const anim_clip_t *clip;

    if(!view)
        return 0;
    clip = &view->clip;
    if(!clip->transforms || !clip->transform_count ||
       ((uintptr_t)clip->transforms &
        (_Alignof(anim_transform_tracks_t) - 1u)) ||
       !isfinite(clip->start_time) || !isfinite(clip->end_time) ||
       clip->start_time >= clip->end_time ||
       clip->transform_count > SIZE_MAX / sizeof(*clip->transforms) ||
       clip->transform_count * sizeof(*clip->transforms) >
       UINTPTR_MAX - (uintptr_t)clip->transforms)
        return 0;
    if(clip->visibility &&
       (((uintptr_t)clip->visibility &
         (_Alignof(anim_visibility_tracks_t) - 1u)) ||
        clip->transform_count > SIZE_MAX / sizeof(*clip->visibility) ||
        clip->transform_count * sizeof(*clip->visibility) >
        UINTPTR_MAX - (uintptr_t)clip->visibility))
        return 0;
    return 1;
}

static int transform_tracks_valid(const anim_transform_tracks_t *tracks) {
    return tracks && transform_valid(&tracks->fallback) &&
           (!tracks->translation ||
            view_valid(tracks->translation, ANIM_VALUE_VECTOR)) &&
           (!tracks->rotation ||
            view_valid(tracks->rotation, ANIM_VALUE_QUATERNION)) &&
           (!tracks->scale ||
            view_valid(tracks->scale, ANIM_VALUE_VECTOR));
}

static int visibility_tracks_valid(const anim_visibility_tracks_t *tracks) {
    return tracks && (!tracks->visible ||
           (view_valid(tracks->visible, ANIM_VALUE_BOOLEAN) &&
            tracks->visible->track.interpolation == ANIM_INTERPOLATION_STEP));
}

int anim_clip_open(const anim_clip_t *clip, anim_clip_view_t *output) {
    anim_clip_view_t view;
    size_t i;

    if(!clip || !output) {
        errno = EINVAL;
        return -1;
    }
    view.clip = *clip;
    if(!clip_shallow_valid(&view)) {
        errno = EINVAL;
        return -1;
    }
    for(i = 0; i < clip->transform_count; ++i) {
        if(!transform_tracks_valid(&clip->transforms[i]) ||
           (clip->visibility &&
            !visibility_tracks_valid(&clip->visibility[i]))) {
            errno = EINVAL;
            return -1;
        }
    }

    memcpy(output, &view, sizeof(view));
    return 0;
}

static float clip_time_clamp(const anim_clip_view_t *clip, float time) {
    if(time < clip->clip.start_time)
        return clip->clip.start_time;
    if(time > clip->clip.end_time)
        return clip->clip.end_time;
    return time;
}

int anim_clip_sample(const anim_clip_view_t *clip, float time,
                     anim_transform_t *output, size_t output_capacity,
                     anim_pose_result_t *result) {
    anim_pose_result_t progress = { 0 };
    float sample_time;
    size_t i;

    if(result)
        *result = progress;
    if(!clip_shallow_valid(clip) || !output ||
       ((uintptr_t)output & (_Alignof(anim_transform_t) - 1u)) ||
       !isfinite(time)) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < clip->clip.transform_count) {
        errno = ENOSPC;
        return -1;
    }
    if(clip->clip.transform_count >
       (UINTPTR_MAX - (uintptr_t)output) / sizeof(*output)) {
        errno = ERANGE;
        return -1;
    }

    sample_time = clip_time_clamp(clip, time);
    for(i = 0; i < clip->clip.transform_count; ++i) {
        if(anim_transform_sample(&clip->clip.transforms[i], sample_time,
                                 &output[i]) < 0)
            return -1;
        progress.sampled_transforms = i + 1u;
        if(result)
            *result = progress;
    }
    return 0;
}

int anim_clip_sample_matrices(const anim_clip_view_t *clip, float time,
                              matrix_t *output, size_t output_capacity,
                              anim_pose_result_t *result) {
    anim_pose_result_t progress = { 0 };
    float sample_time;
    size_t i;

    if(result)
        *result = progress;
    if(!clip_shallow_valid(clip) || !output ||
       ((uintptr_t)output & (_Alignof(matrix_t) - 1u)) ||
       !isfinite(time)) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < clip->clip.transform_count) {
        errno = ENOSPC;
        return -1;
    }
    if(clip->clip.transform_count >
       (UINTPTR_MAX - (uintptr_t)output) / sizeof(*output)) {
        errno = ERANGE;
        return -1;
    }

    sample_time = clip_time_clamp(clip, time);
    for(i = 0; i < clip->clip.transform_count; ++i) {
        anim_transform_t transform;

        if(anim_transform_sample(&clip->clip.transforms[i], sample_time,
                                 &transform) < 0 ||
           anim_transform_matrix_build(&transform, &output[i]) < 0)
            return -1;
        progress.sampled_transforms = i + 1u;
        if(result)
            *result = progress;
    }
    return 0;
}

int anim_clip_sample_visibility(const anim_clip_view_t *clip, float time,
                                bool *output, size_t output_capacity,
                                anim_pose_result_t *result) {
    anim_pose_result_t progress = { 0 };
    float sample_time;
    size_t i;

    if(result)
        *result = progress;
    if(!clip_shallow_valid(clip) || !output || !isfinite(time)) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < clip->clip.transform_count) {
        errno = ENOSPC;
        return -1;
    }
    if(clip->clip.transform_count >
       (UINTPTR_MAX - (uintptr_t)output) / sizeof(*output)) {
        errno = ERANGE;
        return -1;
    }

    sample_time = clip_time_clamp(clip, time);
    for(i = 0; i < clip->clip.transform_count; ++i) {
        bool visible = true;

        if(clip->clip.visibility) {
            const anim_visibility_tracks_t *tracks =
                &clip->clip.visibility[i];

            if(!visibility_tracks_valid(tracks)) {
                errno = EINVAL;
                return -1;
            }
            visible = tracks->fallback;
            if(tracks->visible && anim_track_sample_boolean(
                   tracks->visible, sample_time, &visible, NULL) < 0)
                return -1;
        }
        output[i] = visible;
        progress.sampled_transforms = i + 1u;
        if(result)
            *result = progress;
    }
    return 0;
}

int anim_clip_sample_blend(const anim_clip_view_t *from, float from_time,
                           const anim_clip_view_t *to, float to_time,
                           float weight, anim_transform_t *output,
                           size_t output_capacity,
                           anim_pose_result_t *result) {
    anim_pose_result_t progress = { 0 };
    float first_time;
    float second_time;
    size_t i;

    if(result)
        *result = progress;
    if(!clip_shallow_valid(from) || !clip_shallow_valid(to) || !output ||
       ((uintptr_t)output & (_Alignof(anim_transform_t) - 1u)) ||
       from->clip.transform_count != to->clip.transform_count ||
       !isfinite(from_time) || !isfinite(to_time) || !isfinite(weight) ||
       weight < 0.0f || weight > 1.0f) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < from->clip.transform_count) {
        errno = ENOSPC;
        return -1;
    }
    if(from->clip.transform_count >
       (UINTPTR_MAX - (uintptr_t)output) / sizeof(*output)) {
        errno = ERANGE;
        return -1;
    }

    first_time = clip_time_clamp(from, from_time);
    second_time = clip_time_clamp(to, to_time);
    for(i = 0; i < from->clip.transform_count; ++i) {
        anim_transform_t first;
        anim_transform_t second;

        if(anim_transform_sample(&from->clip.transforms[i], first_time,
                                 &first) < 0 ||
           anim_transform_sample(&to->clip.transforms[i], second_time,
                                 &second) < 0 ||
           anim_transform_blend(&first, &second, weight, &output[i]) < 0)
            return -1;
        progress.sampled_transforms = i + 1u;
        if(result)
            *result = progress;
    }
    return 0;
}

static int playback_valid(const anim_playback_t *playback) {
    if(!playback || !clip_shallow_valid(playback->clip) ||
       !isfinite(playback->time) || !isfinite(playback->rate) ||
       playback->rate <= 0.0f ||
       playback->time < playback->clip->clip.start_time ||
       playback->time > playback->clip->clip.end_time ||
       (playback->mode != ANIM_PLAYBACK_ONCE &&
        playback->mode != ANIM_PLAYBACK_LOOP &&
        playback->mode != ANIM_PLAYBACK_PING_PONG) ||
       (playback->direction != ANIM_PLAYBACK_FORWARD &&
        playback->direction != ANIM_PLAYBACK_BACKWARD) ||
       (playback->state != ANIM_PLAYBACK_STOPPED &&
        playback->state != ANIM_PLAYBACK_PLAYING &&
        playback->state != ANIM_PLAYBACK_PAUSED &&
        playback->state != ANIM_PLAYBACK_COMPLETE))
        return 0;
    return 1;
}

int anim_playback_init(anim_playback_t *playback,
                       const anim_clip_view_t *clip,
                       anim_playback_mode_t mode) {
    anim_playback_t initialized;

    if(!playback || !clip_shallow_valid(clip) ||
       (mode != ANIM_PLAYBACK_ONCE && mode != ANIM_PLAYBACK_LOOP &&
        mode != ANIM_PLAYBACK_PING_PONG)) {
        errno = EINVAL;
        return -1;
    }

    initialized.clip = clip;
    initialized.time = clip->clip.start_time;
    initialized.rate = 1.0f;
    initialized.boundary_count = 0;
    initialized.mode = mode;
    initialized.direction = ANIM_PLAYBACK_FORWARD;
    initialized.state = ANIM_PLAYBACK_STOPPED;
    *playback = initialized;
    return 0;
}

int anim_playback_play(anim_playback_t *playback) {
    if(!playback_valid(playback)) {
        errno = EINVAL;
        return -1;
    }
    if(playback->state == ANIM_PLAYBACK_COMPLETE) {
        playback->time = playback->direction == ANIM_PLAYBACK_FORWARD ?
            playback->clip->clip.start_time : playback->clip->clip.end_time;
    }
    playback->state = ANIM_PLAYBACK_PLAYING;
    return 0;
}

int anim_playback_pause(anim_playback_t *playback) {
    if(!playback_valid(playback)) {
        errno = EINVAL;
        return -1;
    }
    if(playback->state == ANIM_PLAYBACK_PLAYING)
        playback->state = ANIM_PLAYBACK_PAUSED;
    return 0;
}

int anim_playback_stop(anim_playback_t *playback) {
    if(!playback_valid(playback)) {
        errno = EINVAL;
        return -1;
    }
    playback->time = playback->clip->clip.start_time;
    playback->boundary_count = 0;
    playback->direction = ANIM_PLAYBACK_FORWARD;
    playback->state = ANIM_PLAYBACK_STOPPED;
    return 0;
}

int anim_playback_seek(anim_playback_t *playback, float time) {
    if(!playback_valid(playback) || !isfinite(time) ||
       time < playback->clip->clip.start_time ||
       time > playback->clip->clip.end_time) {
        errno = EINVAL;
        return -1;
    }
    playback->time = time;
    if(playback->state == ANIM_PLAYBACK_COMPLETE)
        playback->state = ANIM_PLAYBACK_PAUSED;
    return 0;
}

int anim_playback_set_rate(anim_playback_t *playback, float rate) {
    if(!playback_valid(playback) || !isfinite(rate) || rate <= 0.0f) {
        errno = EINVAL;
        return -1;
    }
    playback->rate = rate;
    return 0;
}

int anim_playback_set_direction(anim_playback_t *playback,
                                anim_playback_direction_t direction) {
    if(!playback_valid(playback) ||
       (direction != ANIM_PLAYBACK_FORWARD &&
        direction != ANIM_PLAYBACK_BACKWARD)) {
        errno = EINVAL;
        return -1;
    }
    playback->direction = direction;
    return 0;
}

static int boundary_count_from_double(double value, uint64_t *output) {
    double integral;

    /* UINT64_MAX rounds upward to 2^64 in binary64, so compare against the
       first unrepresentable count rather than a converted UINT64_MAX. */
    if(!isfinite(value) || value < 0.0 ||
       value >= 18446744073709551616.0) {
        errno = ERANGE;
        return -1;
    }
    integral = floor(value);
    *output = (uint64_t)integral;
    return 0;
}

int anim_playback_advance(anim_playback_t *playback, float elapsed,
                          anim_playback_result_t *result) {
    anim_playback_result_t advanced;
    anim_playback_t next;
    double start;
    double end;
    double duration;
    double travel;
    uint64_t crossed = 0;

    if(!playback_valid(playback) || !isfinite(elapsed) || elapsed < 0.0f) {
        errno = EINVAL;
        return -1;
    }

    start = playback->clip->clip.start_time;
    end = playback->clip->clip.end_time;
    next = *playback;
    advanced.previous_time = playback->time;
    advanced.current_time = playback->time;
    advanced.crossed_boundaries = 0;
    advanced.previous_direction = playback->direction;
    advanced.current_direction = playback->direction;
    advanced.state = playback->state;

    if(playback->state != ANIM_PLAYBACK_PLAYING || elapsed == 0.0f) {
        if(result)
            *result = advanced;
        return 0;
    }

    duration = end - start;
    travel = (double)elapsed * playback->rate;
    if(!isfinite(travel)) {
        errno = ERANGE;
        return -1;
    }

    if(playback->mode == ANIM_PLAYBACK_ONCE) {
        double remaining = playback->direction == ANIM_PLAYBACK_FORWARD ?
            end - playback->time : playback->time - start;

        if(travel >= remaining) {
            next.time = playback->direction == ANIM_PLAYBACK_FORWARD ?
                (float)end : (float)start;
            next.state = ANIM_PLAYBACK_COMPLETE;
            crossed = remaining > 0.0 ? 1u : 0u;
        }
        else if(playback->direction == ANIM_PLAYBACK_FORWARD) {
            next.time = (float)(playback->time + travel);
        }
        else {
            next.time = (float)(playback->time - travel);
        }
    }
    else if(playback->mode == ANIM_PLAYBACK_LOOP) {
        double phase;
        double total;
        double remainder;

        if(playback->direction == ANIM_PLAYBACK_FORWARD)
            phase = playback->time - start;
        else
            phase = end - playback->time;
        total = phase + travel;
        if(boundary_count_from_double(total / duration, &crossed) < 0)
            return -1;
        remainder = fmod(total, duration);
        if(playback->direction == ANIM_PLAYBACK_FORWARD)
            next.time = (float)(start + remainder);
        else
            next.time = (float)(end - remainder);
    }
    else {
        double phase;
        double total;
        double period = duration * 2.0;
        double remainder;
        uint64_t initial_boundaries;
        uint64_t final_boundaries;

        if(playback->time <= start &&
           playback->direction == ANIM_PLAYBACK_BACKWARD)
            next.direction = ANIM_PLAYBACK_FORWARD;
        else if(playback->time >= end &&
                playback->direction == ANIM_PLAYBACK_FORWARD)
            next.direction = ANIM_PLAYBACK_BACKWARD;

        phase = next.direction == ANIM_PLAYBACK_FORWARD ?
            playback->time - start : period - (playback->time - start);
        total = phase + travel;
        if(boundary_count_from_double(phase / duration,
                                      &initial_boundaries) < 0 ||
           boundary_count_from_double(total / duration,
                                      &final_boundaries) < 0)
            return -1;
        crossed = final_boundaries - initial_boundaries;
        remainder = fmod(total, period);
        if(remainder < duration) {
            next.time = (float)(start + remainder);
            next.direction = ANIM_PLAYBACK_FORWARD;
        }
        else {
            next.time = (float)(end - (remainder - duration));
            next.direction = ANIM_PLAYBACK_BACKWARD;
        }
    }

    if(UINT64_MAX - next.boundary_count < crossed) {
        errno = ERANGE;
        return -1;
    }
    next.boundary_count += crossed;
    advanced.current_time = next.time;
    advanced.crossed_boundaries = crossed;
    advanced.current_direction = next.direction;
    advanced.state = next.state;
    *playback = next;
    if(result)
        *result = advanced;
    return 0;
}

static size_t event_lower_bound(const anim_event_track_view_t *view,
                                float time) {
    size_t low = 0;
    size_t high = view->track.event_count;

    while(low < high) {
        size_t middle = low + (high - low) / 2u;

        if(view->track.events[middle].time < time)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static size_t event_upper_bound(const anim_event_track_view_t *view,
                                float time) {
    size_t low = 0;
    size_t high = view->track.event_count;

    while(low < high) {
        size_t middle = low + (high - low) / 2u;

        if(view->track.events[middle].time <= time)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

/* Forward traversal owns (from, to]; backward traversal owns [to, from).
   These half-open rules fire a reflected endpoint once and avoid replaying a
   marker merely because the next advance begins on that endpoint. */
static uint64_t event_count_forward(const anim_event_track_view_t *view,
                                    float from, float to) {
    return (uint64_t)(event_upper_bound(view, to) -
                      event_upper_bound(view, from));
}

static uint64_t event_count_backward(const anim_event_track_view_t *view,
                                     float from, float to) {
    return (uint64_t)(event_lower_bound(view, from) -
                      event_lower_bound(view, to));
}

static uint64_t event_count_at(const anim_event_track_view_t *view,
                               float time) {
    return (uint64_t)(event_upper_bound(view, time) -
                      event_lower_bound(view, time));
}

static int event_count_add(uint64_t *total, uint64_t count,
                           uint64_t repetitions) {
    uint64_t addition;

    if(count && repetitions > UINT64_MAX / count) {
        errno = ERANGE;
        return -1;
    }
    addition = count * repetitions;
    if(UINT64_MAX - *total < addition) {
        errno = ERANGE;
        return -1;
    }
    *total += addition;
    return 0;
}

static void event_emit_forward(const anim_event_track_view_t *view,
                               float from, float to,
                               anim_playback_direction_t direction,
                               anim_event_occurrence_t *output,
                               size_t output_capacity, size_t *published) {
    size_t i = event_upper_bound(view, from);
    size_t end = event_upper_bound(view, to);

    while(i < end && *published < output_capacity) {
        output[*published].event = view->track.events[i++];
        output[*published].direction = direction;
        ++*published;
    }
}

static void event_emit_backward(const anim_event_track_view_t *view,
                                float from, float to,
                                anim_playback_direction_t direction,
                                anim_event_occurrence_t *output,
                                size_t output_capacity, size_t *published) {
    size_t i = event_lower_bound(view, from);
    size_t end = event_lower_bound(view, to);

    while(i > end && *published < output_capacity) {
        output[*published].event = view->track.events[--i];
        output[*published].direction = direction;
        ++*published;
    }
}

static void event_emit_at(const anim_event_track_view_t *view, float time,
                          anim_playback_direction_t direction,
                          anim_event_occurrence_t *output,
                          size_t output_capacity, size_t *published) {
    size_t i = event_lower_bound(view, time);

    if(i < view->track.event_count &&
       view->track.events[i].time == time &&
       *published < output_capacity) {
        output[*published].event = view->track.events[i];
        output[*published].direction = direction;
        ++*published;
    }
}

static anim_playback_direction_t playback_effective_direction(
        const anim_playback_t *playback,
        const anim_playback_result_t *advance) {
    float start = playback->clip->clip.start_time;
    float end = playback->clip->clip.end_time;
    anim_playback_direction_t direction = advance->previous_direction;

    if(playback->mode == ANIM_PLAYBACK_PING_PONG) {
        if(advance->previous_time <= start &&
           direction == ANIM_PLAYBACK_BACKWARD)
            direction = ANIM_PLAYBACK_FORWARD;
        else if(advance->previous_time >= end &&
                direction == ANIM_PLAYBACK_FORWARD)
            direction = ANIM_PLAYBACK_BACKWARD;
    }
    return direction;
}

int anim_playback_collect_events(const anim_playback_t *playback,
                                 const anim_playback_result_t *advance,
                                 const anim_event_track_view_t *events,
                                 anim_event_occurrence_t *output,
                                 size_t output_capacity,
                                 anim_event_result_t *result) {
    anim_event_result_t collected = { 0, 0, false };
    anim_playback_direction_t direction;
    float start;
    float end;
    uint64_t crossed;
    uint64_t full_forward;
    uint64_t full_backward;
    size_t published = 0;

    if(result)
        *result = collected;
    if(!playback_valid(playback) || !advance || !event_track_valid(events) ||
       (output_capacity && !output) ||
       (output && ((uintptr_t)output &
                   (_Alignof(anim_event_occurrence_t) - 1u))) ||
       advance->current_time != playback->time ||
       advance->current_direction != playback->direction ||
       advance->state != playback->state ||
       !isfinite(advance->previous_time) ||
       advance->previous_time < playback->clip->clip.start_time ||
       advance->previous_time > playback->clip->clip.end_time ||
       (advance->previous_direction != ANIM_PLAYBACK_FORWARD &&
        advance->previous_direction != ANIM_PLAYBACK_BACKWARD) ||
       advance->crossed_boundaries > playback->boundary_count ||
       (playback->mode == ANIM_PLAYBACK_ONCE &&
        advance->crossed_boundaries > 1u)) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity > SIZE_MAX / sizeof(*output) ||
       (output_capacity && output_capacity * sizeof(*output) >
        UINTPTR_MAX - (uintptr_t)output)) {
        errno = ERANGE;
        return -1;
    }

    start = playback->clip->clip.start_time;
    end = playback->clip->clip.end_time;
    crossed = advance->crossed_boundaries;
    direction = playback_effective_direction(playback, advance);
    full_forward = event_count_forward(events, start, end);
    full_backward = event_count_backward(events, end, start);

    if(playback->mode == ANIM_PLAYBACK_ONCE || !crossed) {
        if(direction == ANIM_PLAYBACK_FORWARD) {
            collected.matching_events = event_count_forward(
                events, advance->previous_time, advance->current_time);
            event_emit_forward(events, advance->previous_time,
                               advance->current_time, direction,
                               output, output_capacity, &published);
        }
        else {
            collected.matching_events = event_count_backward(
                events, advance->previous_time, advance->current_time);
            event_emit_backward(events, advance->previous_time,
                                advance->current_time, direction,
                                output, output_capacity, &published);
        }
    }
    else if(playback->mode == ANIM_PLAYBACK_LOOP) {
        uint64_t middle = crossed - 1u;
        uint64_t boundary;
        uint64_t i;

        if(direction == ANIM_PLAYBACK_FORWARD) {
            collected.matching_events = event_count_forward(
                events, advance->previous_time, end);
            boundary = event_count_at(events, start);
            if(event_count_add(&collected.matching_events, boundary,
                               crossed) < 0 ||
               event_count_add(&collected.matching_events, full_forward,
                               middle) < 0 ||
               event_count_add(&collected.matching_events,
                               event_count_forward(events, start,
                                                   advance->current_time),
                               1u) < 0)
                return -1;

            event_emit_forward(events, advance->previous_time, end, direction,
                               output, output_capacity, &published);
            if(boundary || full_forward) {
                for(i = 0; i < crossed && published < output_capacity; ++i) {
                    event_emit_at(events, start, direction, output,
                                  output_capacity, &published);
                    if(i + 1u < crossed)
                        event_emit_forward(events, start, end, direction,
                                           output, output_capacity, &published);
                }
            }
            if(published < output_capacity)
                event_emit_forward(events, start, advance->current_time,
                                   direction, output, output_capacity,
                                   &published);
        }
        else {
            collected.matching_events = event_count_backward(
                events, advance->previous_time, start);
            boundary = event_count_at(events, end);
            if(event_count_add(&collected.matching_events, boundary,
                               crossed) < 0 ||
               event_count_add(&collected.matching_events, full_backward,
                               middle) < 0 ||
               event_count_add(&collected.matching_events,
                               event_count_backward(events, end,
                                                    advance->current_time),
                               1u) < 0)
                return -1;

            event_emit_backward(events, advance->previous_time, start,
                                direction, output, output_capacity, &published);
            if(boundary || full_backward) {
                for(i = 0; i < crossed && published < output_capacity; ++i) {
                    event_emit_at(events, end, direction, output,
                                  output_capacity, &published);
                    if(i + 1u < crossed)
                        event_emit_backward(events, end, start, direction,
                                            output, output_capacity,
                                            &published);
                }
            }
            if(published < output_capacity)
                event_emit_backward(events, end, advance->current_time,
                                    direction, output, output_capacity,
                                    &published);
        }
    }
    else {
        uint64_t middle = crossed - 1u;
        uint64_t forward_repetitions;
        uint64_t backward_repetitions;
        uint64_t i;
        anim_playback_direction_t middle_direction;

        if(direction == ANIM_PLAYBACK_FORWARD) {
            collected.matching_events = event_count_forward(
                events, advance->previous_time, end);
            backward_repetitions = (middle + 1u) / 2u;
            forward_repetitions = middle / 2u;
            middle_direction = ANIM_PLAYBACK_BACKWARD;
        }
        else {
            collected.matching_events = event_count_backward(
                events, advance->previous_time, start);
            forward_repetitions = (middle + 1u) / 2u;
            backward_repetitions = middle / 2u;
            middle_direction = ANIM_PLAYBACK_FORWARD;
        }
        if(event_count_add(&collected.matching_events, full_forward,
                           forward_repetitions) < 0 ||
           event_count_add(&collected.matching_events, full_backward,
                           backward_repetitions) < 0)
            return -1;
        if(advance->current_direction == ANIM_PLAYBACK_FORWARD) {
            if(event_count_add(&collected.matching_events,
                               event_count_forward(events, start,
                                                   advance->current_time),
                               1u) < 0)
                return -1;
        }
        else if(event_count_add(&collected.matching_events,
                                event_count_backward(events, end,
                                                     advance->current_time),
                                1u) < 0) {
            return -1;
        }

        if(direction == ANIM_PLAYBACK_FORWARD)
            event_emit_forward(events, advance->previous_time, end, direction,
                               output, output_capacity, &published);
        else
            event_emit_backward(events, advance->previous_time, start,
                                direction, output, output_capacity, &published);

        if(full_forward || full_backward) {
            for(i = 0; i < middle && published < output_capacity; ++i) {
                if(middle_direction == ANIM_PLAYBACK_FORWARD)
                    event_emit_forward(events, start, end, middle_direction,
                                       output, output_capacity, &published);
                else
                    event_emit_backward(events, end, start, middle_direction,
                                        output, output_capacity, &published);
                middle_direction = middle_direction == ANIM_PLAYBACK_FORWARD ?
                    ANIM_PLAYBACK_BACKWARD : ANIM_PLAYBACK_FORWARD;
            }
        }
        if(published < output_capacity) {
            if(advance->current_direction == ANIM_PLAYBACK_FORWARD)
                event_emit_forward(events, start, advance->current_time,
                                   ANIM_PLAYBACK_FORWARD, output,
                                   output_capacity, &published);
            else
                event_emit_backward(events, end, advance->current_time,
                                    ANIM_PLAYBACK_BACKWARD, output,
                                    output_capacity, &published);
        }
    }

    collected.published_events = published;
    collected.truncated = (uint64_t)published < collected.matching_events;
    if(result)
        *result = collected;
    return 0;
}

int anim_playback_sample(const anim_playback_t *playback,
                         anim_transform_t *output, size_t output_capacity,
                         anim_pose_result_t *result) {
    if(!playback_valid(playback)) {
        if(result)
            result->sampled_transforms = 0;
        errno = EINVAL;
        return -1;
    }
    return anim_clip_sample(playback->clip, playback->time, output,
                            output_capacity, result);
}

static int camera_pose_valid(const anim_camera_pose_t *camera) {
    float forward_x;
    float forward_y;
    float forward_z;
    float forward_length;
    float up_length;

    if(!camera ||
       !finite4(camera->eye.x, camera->eye.y, camera->eye.z, 0.0f) ||
       !finite4(camera->target.x, camera->target.y, camera->target.z, 0.0f) ||
       !finite4(camera->up.x, camera->up.y, camera->up.z, 0.0f) ||
       !isfinite(camera->roll) || !isfinite(camera->vertical_fov) ||
       camera->vertical_fov <= 0.0f ||
       camera->vertical_fov >= 3.14159265358979323846f)
        return 0;

    forward_x = camera->target.x - camera->eye.x;
    forward_y = camera->target.y - camera->eye.y;
    forward_z = camera->target.z - camera->eye.z;
    forward_length = forward_x * forward_x + forward_y * forward_y +
                     forward_z * forward_z;
    up_length = camera->up.x * camera->up.x +
                camera->up.y * camera->up.y +
                camera->up.z * camera->up.z;
    return isfinite(forward_length) && forward_length > FLT_MIN &&
           isfinite(up_length) && up_length > FLT_MIN;
}

static int camera_tracks_valid(const anim_camera_tracks_t *tracks) {
    return tracks && camera_pose_valid(&tracks->fallback) &&
           (!tracks->eye || view_valid(tracks->eye, ANIM_VALUE_VECTOR)) &&
           (!tracks->target ||
            view_valid(tracks->target, ANIM_VALUE_VECTOR)) &&
           (!tracks->up || view_valid(tracks->up, ANIM_VALUE_VECTOR)) &&
           (!tracks->roll || view_valid(tracks->roll, ANIM_VALUE_SCALAR)) &&
           (!tracks->vertical_fov ||
            view_valid(tracks->vertical_fov, ANIM_VALUE_SCALAR));
}

int anim_camera_sample(const anim_camera_tracks_t *tracks, float time,
                       anim_camera_pose_t *output) {
    anim_camera_pose_t sampled;

    if(!camera_tracks_valid(tracks) || !output || !isfinite(time)) {
        errno = EINVAL;
        return -1;
    }

    sampled = tracks->fallback;
    if(tracks->eye && anim_track_sample_vector(
           tracks->eye, time, &sampled.eye, NULL) < 0)
        return -1;
    if(tracks->target && anim_track_sample_vector(
           tracks->target, time, &sampled.target, NULL) < 0)
        return -1;
    if(tracks->up && anim_track_sample_vector(
           tracks->up, time, &sampled.up, NULL) < 0)
        return -1;
    if(tracks->roll && anim_track_sample_scalar(
           tracks->roll, time, &sampled.roll, NULL) < 0)
        return -1;
    if(tracks->vertical_fov && anim_track_sample_scalar(
           tracks->vertical_fov, time, &sampled.vertical_fov, NULL) < 0)
        return -1;

    sampled.eye.w = 1.0f;
    sampled.target.w = 1.0f;
    sampled.up.w = 0.0f;
    if(!camera_pose_valid(&sampled)) {
        errno = EDOM;
        return -1;
    }
    *output = sampled;
    return 0;
}

int anim_camera_view_matrix_build(const anim_camera_pose_t *camera,
                                  matrix_t *output) {
    anim_camera_pose_t rolled;
    mat_lookat_desc_t look_at;
    float axis_x;
    float axis_y;
    float axis_z;
    float length_squared;
    float reciprocal_length;
    float sine;
    float cosine;
    float cross_x;
    float cross_y;
    float cross_z;
    float dot;

    if(!camera_pose_valid(camera) || !output ||
       ((uintptr_t)output & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    rolled = *camera;
    axis_x = camera->target.x - camera->eye.x;
    axis_y = camera->target.y - camera->eye.y;
    axis_z = camera->target.z - camera->eye.z;
    length_squared = axis_x * axis_x + axis_y * axis_y + axis_z * axis_z;
#ifdef __DREAMCAST__
    reciprocal_length = shz_inv_sqrtf_fsrra(length_squared);
    {
        shz_sincos_t rotation = shz_sincosf(camera->roll);

        sine = rotation.sin;
        cosine = rotation.cos;
    }
#else
    reciprocal_length = 1.0f / sqrtf(length_squared);
    sine = sinf(camera->roll);
    cosine = cosf(camera->roll);
#endif
    axis_x *= reciprocal_length;
    axis_y *= reciprocal_length;
    axis_z *= reciprocal_length;
    cross_x = axis_y * camera->up.z - axis_z * camera->up.y;
    cross_y = axis_z * camera->up.x - axis_x * camera->up.z;
    cross_z = axis_x * camera->up.y - axis_y * camera->up.x;
    dot = axis_x * camera->up.x + axis_y * camera->up.y +
          axis_z * camera->up.z;
    rolled.up.x = camera->up.x * cosine + cross_x * sine +
                  axis_x * dot * (1.0f - cosine);
    rolled.up.y = camera->up.y * cosine + cross_y * sine +
                  axis_y * dot * (1.0f - cosine);
    rolled.up.z = camera->up.z * cosine + cross_z * sine +
                  axis_z * dot * (1.0f - cosine);
    rolled.up.w = 0.0f;
    if(!camera_pose_valid(&rolled) ||
       !finite4(reciprocal_length, sine, cosine, dot)) {
        errno = ERANGE;
        return -1;
    }

    look_at.eye = rolled.eye;
    look_at.center = rolled.target;
    look_at.up = rolled.up;
    return mat_lookat_build(output, &look_at);
}

int anim_camera_projection_matrix_build(const anim_camera_pose_t *camera,
                                        float x_center, float y_center,
                                        float z_near, float z_far,
                                        matrix_t *output) {
    mat_perspective_desc_t perspective;
    float tangent;

    if(!camera_pose_valid(camera) || !output ||
       ((uintptr_t)output & (_Alignof(matrix_t) - 1u)) ||
       !finite4(x_center, y_center, z_near, z_far)) {
        errno = EINVAL;
        return -1;
    }
#ifdef __DREAMCAST__
    tangent = shz_tanf(camera->vertical_fov * 0.5f);
    perspective.cot_half_fov = shz_invf(tangent);
#else
    tangent = tanf(camera->vertical_fov * 0.5f);
    perspective.cot_half_fov = 1.0f / tangent;
#endif
    if(!isfinite(tangent) || tangent <= FLT_MIN ||
       !isfinite(perspective.cot_half_fov)) {
        errno = ERANGE;
        return -1;
    }
    perspective.x_center = x_center;
    perspective.y_center = y_center;
    perspective.z_near = z_near;
    perspective.z_far = z_far;
    return mat_perspective_build(output, &perspective);
}

int anim_playback_sample_camera(const anim_playback_t *playback,
                                const anim_camera_tracks_t *tracks,
                                anim_camera_pose_t *output) {
    if(!playback_valid(playback)) {
        errno = EINVAL;
        return -1;
    }
    return anim_camera_sample(tracks, playback->time, output);
}

static int light_pose_valid(const pvr_light_t *light) {
    float length_squared;

    if(!light || (light->kind != PVR_LIGHT_DIRECTIONAL &&
                  light->kind != PVR_LIGHT_POINT) ||
       !finite4(light->color.x, light->color.y, light->color.z, 0.0f) ||
       light->color.x < 0.0f || light->color.y < 0.0f ||
       light->color.z < 0.0f || !isfinite(light->intensity) ||
       light->intensity < 0.0f ||
       !isfinite(light->attenuation_constant) ||
       !isfinite(light->attenuation_linear) ||
       !isfinite(light->attenuation_quadratic) ||
       !isfinite(light->range) || light->range < 0.0f)
        return 0;

    if(light->kind == PVR_LIGHT_POINT)
        return finite4(light->source.position.x, light->source.position.y,
                       light->source.position.z, 0.0f) &&
               light->attenuation_constant > 0.0f &&
               light->attenuation_linear >= 0.0f &&
               light->attenuation_quadratic >= 0.0f;

    if(!finite4(light->source.direction.x, light->source.direction.y,
                light->source.direction.z, 0.0f))
        return 0;
    length_squared = light->source.direction.x * light->source.direction.x +
                     light->source.direction.y * light->source.direction.y +
                     light->source.direction.z * light->source.direction.z;
    return isfinite(length_squared) && length_squared > FLT_MIN;
}

static int light_tracks_valid(const anim_light_tracks_t *tracks) {
    return tracks && light_pose_valid(&tracks->fallback) &&
           (!tracks->source ||
            view_valid(tracks->source, ANIM_VALUE_VECTOR)) &&
           (!tracks->color || view_valid(tracks->color, ANIM_VALUE_VECTOR)) &&
           (!tracks->intensity ||
            view_valid(tracks->intensity, ANIM_VALUE_SCALAR)) &&
           (!tracks->range || view_valid(tracks->range, ANIM_VALUE_SCALAR));
}

int anim_light_sample(const anim_light_tracks_t *tracks, float time,
                      pvr_light_t *output) {
    pvr_light_t sampled;
    vector_t source;

    if(!light_tracks_valid(tracks) || !output || !isfinite(time)) {
        errno = EINVAL;
        return -1;
    }

    sampled = tracks->fallback;
    source = sampled.kind == PVR_LIGHT_POINT ?
        sampled.source.position : sampled.source.direction;
    if(tracks->source && anim_track_sample_vector(
           tracks->source, time, &source, NULL) < 0)
        return -1;
    if(sampled.kind == PVR_LIGHT_POINT) {
        source.w = 1.0f;
        sampled.source.position = source;
    }
    else {
        source.w = 0.0f;
        sampled.source.direction = source;
    }
    if(tracks->color && anim_track_sample_vector(
           tracks->color, time, &sampled.color, NULL) < 0)
        return -1;
    if(tracks->intensity && anim_track_sample_scalar(
           tracks->intensity, time, &sampled.intensity, NULL) < 0)
        return -1;
    if(tracks->range && anim_track_sample_scalar(
           tracks->range, time, &sampled.range, NULL) < 0)
        return -1;
    sampled.color.w = 0.0f;
    if(!light_pose_valid(&sampled)) {
        errno = EDOM;
        return -1;
    }
    *output = sampled;
    return 0;
}

int anim_playback_sample_light(const anim_playback_t *playback,
                               const anim_light_tracks_t *tracks,
                               pvr_light_t *output) {
    if(!playback_valid(playback)) {
        errno = EINVAL;
        return -1;
    }
    return anim_light_sample(tracks, playback->time, output);
}

int anim_morph_targets_sample(const anim_morph_target_tracks_t *tracks,
                              size_t target_count, float time,
                              pvr_morph_target_t *output,
                              size_t output_capacity,
                              anim_morph_result_t *result) {
    anim_morph_result_t progress = { 0 };
    size_t i;

    if(result)
        *result = progress;
    if((target_count && (!tracks || !output)) || !isfinite(time) ||
       (tracks && ((uintptr_t)tracks &
                   (_Alignof(anim_morph_target_tracks_t) - 1u))) ||
       (output && ((uintptr_t)output &
                   (_Alignof(pvr_morph_target_t) - 1u)))) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < target_count) {
        errno = ENOSPC;
        return -1;
    }
    if(target_count > SIZE_MAX / sizeof(*tracks) ||
       target_count > SIZE_MAX / sizeof(*output) ||
       (target_count &&
        (target_count * sizeof(*tracks) >
         UINTPTR_MAX - (uintptr_t)tracks ||
         target_count * sizeof(*output) >
         UINTPTR_MAX - (uintptr_t)output))) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < target_count; ++i) {
        pvr_morph_target_t sampled = tracks[i].fallback;

        if(!isfinite(sampled.weight) ||
           (tracks[i].weight &&
            !view_valid(tracks[i].weight, ANIM_VALUE_SCALAR))) {
            errno = EINVAL;
            return -1;
        }
        if(tracks[i].weight && anim_track_sample_scalar(
               tracks[i].weight, time, &sampled.weight, NULL) < 0)
            return -1;
        output[i] = sampled;
        progress.sampled_targets = i + 1u;
        if(result)
            *result = progress;
    }
    return 0;
}
