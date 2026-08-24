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
       (track->stride & 3u)) {
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
