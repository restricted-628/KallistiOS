/* KallistiOS ##version##

   pvr_lighting.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_lighting.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int finite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static float saturate(float value) {
    if(value <= 0.0f)
        return 0.0f;
    if(value >= 1.0f)
        return 1.0f;
    return value;
}

static float saturated_add_scaled(float accumulated, float scale,
                                  float component) {
    if(accumulated >= 1.0f || scale <= 0.0f || component <= 0.0f)
        return accumulated;
    if(scale >= (1.0f - accumulated) / component)
        return 1.0f;
    return accumulated + scale * component;
}

static uint32_t pack_channel(float value) {
    return (uint32_t)(saturate(value) * 255.0f + 0.5f);
}

int pvr_normal_matrix_build(pvr_normal_matrix_t *output,
                            const matrix_t *object_to_world) {
    pvr_normal_matrix_t normal;
    float a00;
    float a01;
    float a02;
    float a10;
    float a11;
    float a12;
    float a20;
    float a21;
    float a22;
    float scale = 0.0f;
    float determinant;
    float inverse_scale_det;
    size_t column;
    size_t row;

    if(!output || !object_to_world || ((uintptr_t)output & 3u) ||
       ((uintptr_t)object_to_world & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            float magnitude;

            if(!isfinite((*object_to_world)[column][row])) {
                errno = EDOM;
                return -1;
            }

            if(column >= 3 || row >= 3)
                continue;
            magnitude = fabsf((*object_to_world)[column][row]);
            if(magnitude > scale)
                scale = magnitude;
        }
    }

    if(scale <= FLT_MIN) {
        errno = ERANGE;
        return -1;
    }

    /* Scale the 3x3 before taking cofactors. This avoids overflowing on a
       valid large transform and makes the singularity test relative to the
       transform rather than tied to one world-unit convention. */
    a00 = (*object_to_world)[0][0] / scale;
    a01 = (*object_to_world)[1][0] / scale;
    a02 = (*object_to_world)[2][0] / scale;
    a10 = (*object_to_world)[0][1] / scale;
    a11 = (*object_to_world)[1][1] / scale;
    a12 = (*object_to_world)[2][1] / scale;
    a20 = (*object_to_world)[0][2] / scale;
    a21 = (*object_to_world)[1][2] / scale;
    a22 = (*object_to_world)[2][2] / scale;

    determinant = a00 * (a11 * a22 - a12 * a21) -
                  a01 * (a10 * a22 - a12 * a20) +
                  a02 * (a10 * a21 - a11 * a20);
    if(!isfinite(determinant) ||
       fabsf(determinant) <= 16.0f * FLT_EPSILON) {
        errno = ERANGE;
        return -1;
    }

    inverse_scale_det = 1.0f / (scale * determinant);
    if(!isfinite(inverse_scale_det)) {
        errno = ERANGE;
        return -1;
    }

    /* The cofactor matrix is inverse(A) transposed. Values are stored in the
       same column-major form as matrix_t and SH4ZAM's 3x3 type. */
    normal.column[0][0] = (a11 * a22 - a12 * a21) * inverse_scale_det;
    normal.column[1][0] = (a12 * a20 - a10 * a22) * inverse_scale_det;
    normal.column[2][0] = (a10 * a21 - a11 * a20) * inverse_scale_det;
    normal.column[0][1] = (a02 * a21 - a01 * a22) * inverse_scale_det;
    normal.column[1][1] = (a00 * a22 - a02 * a20) * inverse_scale_det;
    normal.column[2][1] = (a01 * a20 - a00 * a21) * inverse_scale_det;
    normal.column[0][2] = (a01 * a12 - a02 * a11) * inverse_scale_det;
    normal.column[1][2] = (a02 * a10 - a00 * a12) * inverse_scale_det;
    normal.column[2][2] = (a00 * a11 - a01 * a10) * inverse_scale_det;

    for(column = 0; column < 3; ++column) {
        for(row = 0; row < 3; ++row) {
            if(!isfinite(normal.column[column][row])) {
                errno = ERANGE;
                return -1;
            }
        }
    }

    memcpy(output, &normal, sizeof(normal));
    return 0;
}

int pvr_normal_transform(vector_t *output, size_t output_capacity,
                         const pvr_normal_stream_t *stream,
                         const pvr_normal_matrix_t *matrix,
                         pvr_normal_result_t *result) {
    pvr_normal_result_t progress = { 0 };
    uintptr_t input_start;
    uintptr_t output_start;
    size_t input_bytes;
    size_t output_bytes;
    size_t column;
    size_t row;
    size_t i;
#ifdef __DREAMCAST__
    shz_mat3x3_t transform;
#endif

    if(result)
        *result = progress;

    if(!output || !stream || !matrix || !stream->normals ||
       ((uintptr_t)output & 3u) || ((uintptr_t)stream->normals & 3u) ||
       stream->stride < sizeof(vector_t) || (stream->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }

    if(output_capacity < stream->normal_count) {
        errno = ENOSPC;
        return -1;
    }

    for(column = 0; column < 3; ++column) {
        for(row = 0; row < 3; ++row) {
            if(!isfinite(matrix->column[column][row])) {
                errno = EDOM;
                return -1;
            }
        }
    }

    if(!stream->normal_count)
        return 0;

    if(stream->normal_count - 1u >
       (SIZE_MAX - sizeof(vector_t)) / stream->stride ||
       stream->normal_count > SIZE_MAX / sizeof(vector_t)) {
        errno = ERANGE;
        return -1;
    }

    input_bytes = (stream->normal_count - 1u) * stream->stride +
                  sizeof(vector_t);
    output_bytes = stream->normal_count * sizeof(vector_t);
    input_start = (uintptr_t)stream->normals;
    output_start = (uintptr_t)output;
    if(input_bytes > UINTPTR_MAX - input_start ||
       output_bytes > UINTPTR_MAX - output_start) {
        errno = ERANGE;
        return -1;
    }

    if(!(input_start == output_start && stream->stride == sizeof(vector_t)) &&
       ranges_overlap(input_start, input_bytes, output_start, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

#ifdef __DREAMCAST__
    memcpy(&transform, matrix, sizeof(transform));
#endif

    for(i = 0; i < stream->normal_count; ++i) {
        const vector_t *source = (const vector_t *)
            ((const uint8_t *)stream->normals + i * stream->stride);
        vector_t normal;
        float length_squared;
#ifdef __DREAMCAST__
        shz_vec3_t transformed;

        transformed = shz_mat3x3_transform_vec3(
            &transform, shz_vec3_init(source->x, source->y, source->z));
        length_squared = shz_vec3_dot(transformed, transformed);
        if(!finite3(transformed.x, transformed.y, transformed.z) ||
           !isfinite(length_squared) || length_squared <= FLT_MIN) {
            errno = EDOM;
            goto fail;
        }
        transformed = shz_vec3_normalize(transformed);
        normal.x = transformed.x;
        normal.y = transformed.y;
        normal.z = transformed.z;
#else
        float reciprocal_length;

        normal.x = matrix->column[0][0] * source->x +
                   matrix->column[1][0] * source->y +
                   matrix->column[2][0] * source->z;
        normal.y = matrix->column[0][1] * source->x +
                   matrix->column[1][1] * source->y +
                   matrix->column[2][1] * source->z;
        normal.z = matrix->column[0][2] * source->x +
                   matrix->column[1][2] * source->y +
                   matrix->column[2][2] * source->z;
        length_squared = normal.x * normal.x + normal.y * normal.y +
                         normal.z * normal.z;
        if(!finite3(normal.x, normal.y, normal.z) ||
           !isfinite(length_squared) || length_squared <= FLT_MIN) {
            errno = EDOM;
            goto fail;
        }
        reciprocal_length = 1.0f / sqrtf(length_squared);
        normal.x *= reciprocal_length;
        normal.y *= reciprocal_length;
        normal.z *= reciprocal_length;
#endif
        normal.w = 0.0f;
        if(!finite3(normal.x, normal.y, normal.z)) {
            errno = ERANGE;
            goto fail;
        }
        memcpy(output + i, &normal, sizeof(normal));
        ++progress.transformed_normals;
    }

    if(result)
        *result = progress;
    return 0;

fail:
    if(result)
        *result = progress;
    return -1;
}

int pvr_color_pack_argb(uint32_t *output, float alpha, float red,
                        float green, float blue) {
    uint32_t packed;

    if(!output || ((uintptr_t)output & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(!finite3(red, green, blue) || !isfinite(alpha)) {
        errno = EDOM;
        return -1;
    }

    packed = pack_channel(alpha) << 24;
    packed |= pack_channel(red) << 16;
    packed |= pack_channel(green) << 8;
    packed |= pack_channel(blue);
    *output = packed;
    return 0;
}

static int light_valid(const pvr_light_t *light) {
    float length_squared;

    if(!light || (light->kind != PVR_LIGHT_DIRECTIONAL &&
                  light->kind != PVR_LIGHT_POINT) ||
       !finite3(light->color.x, light->color.y, light->color.z) ||
       light->color.x < 0.0f || light->color.y < 0.0f ||
       light->color.z < 0.0f || !isfinite(light->intensity) ||
       light->intensity < 0.0f ||
       !isfinite(light->attenuation_constant) ||
       !isfinite(light->attenuation_linear) ||
       !isfinite(light->attenuation_quadratic) ||
       !isfinite(light->range) || light->range < 0.0f)
        return 0;

    if(light->kind == PVR_LIGHT_POINT)
        return finite3(light->source.position.x, light->source.position.y,
                       light->source.position.z) &&
               light->attenuation_constant > 0.0f &&
               light->attenuation_linear >= 0.0f &&
               light->attenuation_quadratic >= 0.0f;

    if(!finite3(light->source.direction.x, light->source.direction.y,
                light->source.direction.z))
        return 0;
    length_squared = light->source.direction.x * light->source.direction.x +
                     light->source.direction.y * light->source.direction.y +
                     light->source.direction.z * light->source.direction.z;
    return isfinite(length_squared) && length_squared > FLT_MIN;
}

static int context_valid(const pvr_lighting_context_t *context) {
    size_t i;

    if(!context || !finite3(context->ambient[0], context->ambient[1],
                            context->ambient[2]) ||
       context->ambient[0] < 0.0f || context->ambient[1] < 0.0f ||
       context->ambient[2] < 0.0f ||
       (context->light_count && !context->lights) ||
       (context->light_count && ((uintptr_t)context->lights & 3u)) ||
       context->light_count > SIZE_MAX / sizeof(*context->lights) ||
       (context->light_count &&
        context->light_count * sizeof(*context->lights) >
        UINTPTR_MAX - (uintptr_t)context->lights))
        return 0;

    for(i = 0; i < context->light_count; ++i) {
        if(!light_valid(context->lights + i))
            return 0;
    }

    return 1;
}

static float dot3(const vector_t *lhs, float x, float y, float z) {
#ifdef __DREAMCAST__
    return shz_vec3_dot(shz_vec3_init(lhs->x, lhs->y, lhs->z),
                        shz_vec3_init(x, y, z));
#else
    return lhs->x * x + lhs->y * y + lhs->z * z;
#endif
}

int pvr_lighting_apply(uint32_t *output, size_t output_capacity,
                       const pvr_lighting_stream_t *stream,
                       const pvr_lighting_context_t *context,
                       pvr_lighting_result_t *result) {
    pvr_lighting_result_t progress = { 0 };
    uintptr_t input_start;
    uintptr_t output_start;
    size_t input_bytes;
    size_t output_bytes;
    size_t i;

    if(result)
        *result = progress;

    if(!output || !stream || !stream->samples ||
       ((uintptr_t)output & 3u) || ((uintptr_t)stream->samples & 3u) ||
       stream->stride < sizeof(pvr_lighting_sample_t) ||
       (stream->stride & 3u) || !context_valid(context)) {
        errno = EINVAL;
        return -1;
    }

    if(output_capacity < stream->sample_count) {
        errno = ENOSPC;
        return -1;
    }

    if(!stream->sample_count)
        return 0;

    if(stream->sample_count - 1u >
       (SIZE_MAX - sizeof(pvr_lighting_sample_t)) / stream->stride ||
       stream->sample_count > SIZE_MAX / sizeof(uint32_t)) {
        errno = ERANGE;
        return -1;
    }

    input_bytes = (stream->sample_count - 1u) * stream->stride +
                  sizeof(pvr_lighting_sample_t);
    output_bytes = stream->sample_count * sizeof(uint32_t);
    input_start = (uintptr_t)stream->samples;
    output_start = (uintptr_t)output;
    if(input_bytes > UINTPTR_MAX - input_start ||
       output_bytes > UINTPTR_MAX - output_start) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(input_start, input_bytes, output_start, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < stream->sample_count; ++i) {
        const pvr_lighting_sample_t *sample =
            (const pvr_lighting_sample_t *)
            ((const uint8_t *)stream->samples + i * stream->stride);
        float accumulated[3];
        float normal_length_squared;
        size_t light_index;

        if(!finite3(sample->position.x, sample->position.y,
                    sample->position.z) ||
           !finite3(sample->normal.x, sample->normal.y, sample->normal.z) ||
           !finite3(sample->color[0], sample->color[1], sample->color[2]) ||
           !isfinite(sample->color[3])) {
            errno = EDOM;
            goto fail;
        }

        normal_length_squared = dot3(&sample->normal, sample->normal.x,
                                     sample->normal.y, sample->normal.z);
        if(!isfinite(normal_length_squared) ||
           fabsf(normal_length_squared - 1.0f) > 0.002001f) {
            errno = EDOM;
            goto fail;
        }

        accumulated[0] = saturate(context->ambient[0]);
        accumulated[1] = saturate(context->ambient[1]);
        accumulated[2] = saturate(context->ambient[2]);

        for(light_index = 0; light_index < context->light_count;
            ++light_index) {
            const pvr_light_t *light = context->lights + light_index;
            float direction_x;
            float direction_y;
            float direction_z;
            float length_squared;
            float reciprocal_length;
            float attenuation = 1.0f;
            float diffuse;

            if(light->kind == PVR_LIGHT_DIRECTIONAL) {
                direction_x = light->source.direction.x;
                direction_y = light->source.direction.y;
                direction_z = light->source.direction.z;
            }
            else {
                direction_x = light->source.position.x - sample->position.x;
                direction_y = light->source.position.y - sample->position.y;
                direction_z = light->source.position.z - sample->position.z;
            }

            length_squared = direction_x * direction_x +
                             direction_y * direction_y +
                             direction_z * direction_z;
            if(!isfinite(length_squared)) {
                errno = ERANGE;
                goto fail;
            }
            if(length_squared <= FLT_MIN)
                continue;

#ifdef __DREAMCAST__
            reciprocal_length = shz_inv_sqrtf_fsrra(length_squared);
#else
            reciprocal_length = 1.0f / sqrtf(length_squared);
#endif
            if(light->kind == PVR_LIGHT_POINT) {
                float distance = length_squared * reciprocal_length;
                float denominator;

                if(light->range > 0.0f && distance > light->range)
                    continue;
                denominator = light->attenuation_constant +
                    light->attenuation_linear * distance +
                    light->attenuation_quadratic * length_squared;
                if(!isfinite(denominator) || denominator <= FLT_MIN)
                    continue;
                attenuation = 1.0f / denominator;
            }

            direction_x *= reciprocal_length;
            direction_y *= reciprocal_length;
            direction_z *= reciprocal_length;
            diffuse = dot3(&sample->normal, direction_x, direction_y,
                           direction_z);
            if(diffuse <= 0.0f)
                continue;
            diffuse *= light->intensity * attenuation;
            accumulated[0] = saturated_add_scaled(
                accumulated[0], diffuse, light->color.x);
            accumulated[1] = saturated_add_scaled(
                accumulated[1], diffuse, light->color.y);
            accumulated[2] = saturated_add_scaled(
                accumulated[2], diffuse, light->color.z);
        }

        if(pvr_color_pack_argb(output + i, sample->color[3],
                               sample->color[0] * accumulated[0],
                               sample->color[1] * accumulated[1],
                               sample->color[2] * accumulated[2]) < 0)
            goto fail;
        ++progress.shaded_samples;
    }

    if(result)
        *result = progress;
    return 0;

fail:
    if(result)
        *result = progress;
    return -1;
}
