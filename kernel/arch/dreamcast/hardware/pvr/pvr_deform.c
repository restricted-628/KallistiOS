/* KallistiOS ##version##

   pvr_deform.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_deform.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef __DREAMCAST__
_Static_assert(sizeof(pvr_normal_matrix_t) == sizeof(shz_mat3x3_t),
               "normal matrix bridge must preserve all components");
#endif
_Static_assert(sizeof(pvr_skin_span_t) == 8u,
               "skin spans must occupy 8 bytes");
_Static_assert(sizeof(pvr_skin_weight_t) == 8u,
               "skin weights must occupy 8 bytes");

static int finite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int range_size(size_t count, size_t stride, size_t element,
                      size_t *bytes) {
    if(!count) {
        *bytes = 0;
        return 0;
    }
    if(count - 1u > (SIZE_MAX - element) / stride)
        return -1;
    *bytes = (count - 1u) * stride + element;
    return 0;
}

static int address_range(const void *pointer, size_t bytes) {
    return bytes <= UINTPTR_MAX - (uintptr_t)pointer;
}

static int ranges_overlap(const void *lhs, size_t lhs_size,
                          const void *rhs, size_t rhs_size) {
    uintptr_t left = (uintptr_t)lhs;
    uintptr_t right = (uintptr_t)rhs;

    return left < right + rhs_size && right < left + lhs_size;
}

static int vertex_finite(const pvr_deform_vertex_t *vertex) {
    return finite3(vertex->position.x, vertex->position.y,
                   vertex->position.z) &&
           finite3(vertex->normal.x, vertex->normal.y, vertex->normal.z);
}

static int normalize(float *x, float *y, float *z) {
    float length_squared = *x * *x + *y * *y + *z * *z;
    float reciprocal;

    if(!isfinite(length_squared) || length_squared <= FLT_MIN)
        return -1;
#ifdef __DREAMCAST__
    reciprocal = shz_inv_sqrtf_fsrra(length_squared);
#else
    reciprocal = 1.0f / sqrtf(length_squared);
#endif
    *x *= reciprocal;
    *y *= reciprocal;
    *z *= reciprocal;
    return finite3(*x, *y, *z) ? 0 : -1;
}

static int stream_preflight(const pvr_deform_stream_t *stream,
                            pvr_deform_vertex_t *output,
                            size_t output_capacity, size_t *input_bytes,
                            size_t *output_bytes) {
    if(!stream || !output || !stream->vertices ||
       ((uintptr_t)output & (_Alignof(pvr_deform_vertex_t) - 1u)) ||
       ((uintptr_t)stream->vertices &
        (_Alignof(pvr_deform_vertex_t) - 1u)) ||
       stream->stride < sizeof(pvr_deform_vertex_t) ||
       (stream->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity < stream->vertex_count) {
        errno = ENOSPC;
        return -1;
    }
    if(range_size(stream->vertex_count, stream->stride,
                  sizeof(pvr_deform_vertex_t), input_bytes) < 0 ||
       stream->vertex_count > SIZE_MAX / sizeof(pvr_deform_vertex_t)) {
        errno = ERANGE;
        return -1;
    }
    *output_bytes = stream->vertex_count * sizeof(pvr_deform_vertex_t);
    if(!address_range(stream->vertices, *input_bytes) ||
       !address_range(output, *output_bytes)) {
        errno = ERANGE;
        return -1;
    }
    if(!((uintptr_t)stream->vertices == (uintptr_t)output &&
         stream->stride == sizeof(pvr_deform_vertex_t)) &&
       ranges_overlap(stream->vertices, *input_bytes, output, *output_bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_morph_apply(pvr_deform_vertex_t *output, size_t output_capacity,
                    const pvr_deform_stream_t *base,
                    const pvr_morph_target_t *targets, size_t target_count,
                    pvr_deform_result_t *result) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes;
    size_t output_bytes;
    size_t target_index;
    size_t i;

    if(result)
        *result = progress;
    if(stream_preflight(base, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(target_count &&
       (!targets || ((uintptr_t)targets &
                     (_Alignof(pvr_morph_target_t) - 1u)))) {
        errno = EINVAL;
        return -1;
    }
    if(target_count > SIZE_MAX / sizeof(*targets) ||
       (target_count && target_count * sizeof(*targets) >
        UINTPTR_MAX - (uintptr_t)targets)) {
        errno = ERANGE;
        return -1;
    }
    if(target_count &&
       ranges_overlap(targets, target_count * sizeof(*targets),
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    /* Validate all target framing before an in-place output can overwrite any
       source. Delta values remain per-vertex errors with prefix accounting. */
    for(target_index = 0; target_index < target_count; ++target_index) {
        const pvr_morph_target_t *target = targets + target_index;
        size_t bytes;

        if(!target->deltas ||
           ((uintptr_t)target->deltas &
            (_Alignof(pvr_morph_delta_t) - 1u)) ||
           target->stride < sizeof(pvr_morph_delta_t) ||
           (target->stride & 3u) || !isfinite(target->weight)) {
            errno = EINVAL;
            return -1;
        }
        if(range_size(base->vertex_count, target->stride,
                      sizeof(pvr_morph_delta_t), &bytes) < 0 ||
           !address_range(target->deltas, bytes)) {
            errno = ERANGE;
            return -1;
        }
        if(ranges_overlap(target->deltas, bytes, output, output_bytes)) {
            errno = EINVAL;
            return -1;
        }
    }

    for(i = 0; i < base->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)base->vertices + i * base->stride);
        pvr_deform_vertex_t vertex;

        memcpy(&vertex, source, sizeof(vertex));
        if(!vertex_finite(&vertex)) {
            errno = EDOM;
            goto fail;
        }
        for(target_index = 0; target_index < target_count; ++target_index) {
            const pvr_morph_target_t *target = targets + target_index;
            const pvr_morph_delta_t *delta = (const pvr_morph_delta_t *)
                ((const uint8_t *)target->deltas + i * target->stride);

            if(!finite3(delta->position.x, delta->position.y,
                        delta->position.z) ||
               !finite3(delta->normal.x, delta->normal.y, delta->normal.z)) {
                errno = EDOM;
                goto fail;
            }
            vertex.position.x += delta->position.x * target->weight;
            vertex.position.y += delta->position.y * target->weight;
            vertex.position.z += delta->position.z * target->weight;
            vertex.normal.x += delta->normal.x * target->weight;
            vertex.normal.y += delta->normal.y * target->weight;
            vertex.normal.z += delta->normal.z * target->weight;
        }
        if(!finite3(vertex.position.x, vertex.position.y, vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y, &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto fail;
        }
        vertex.position.w = 1.0f;
        vertex.normal.w = 0.0f;
        memcpy(output + i, &vertex, sizeof(vertex));
        ++progress.deformed_vertices;
    }
    if(result)
        *result = progress;
    return 0;

fail:
    if(result)
        *result = progress;
    return -1;
}

static int matrix_finite(const matrix_t *matrix) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            if(!isfinite((*matrix)[column][row]))
                return 0;
        }
    }
    return 1;
}

static int normal_matrix_finite(const pvr_normal_matrix_t *matrix) {
    size_t column;
    size_t row;

    for(column = 0; column < 3; ++column) {
        for(row = 0; row < 3; ++row) {
            if(!isfinite(matrix->column[column][row]))
                return 0;
        }
    }
    return 1;
}

static void skin_accumulate(const pvr_deform_vertex_t *source,
                            const pvr_skin_palette_t *palette,
                            uint16_t joint, float weight,
                            pvr_deform_vertex_t *vertex) {
    const matrix_t *position = palette->position_matrices + joint;
    const pvr_normal_matrix_t *normal = palette->normal_matrices + joint;
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;

#ifdef __DREAMCAST__
    {
        shz_mat4x4_t position_matrix;
        shz_mat3x3_t normal_matrix;
        shz_vec3_t transformed_position;
        shz_vec3_t transformed_normal;

        shz_kos_matrix_import(&position_matrix, position);
        memcpy(&normal_matrix, normal, sizeof(normal_matrix));
        transformed_position = shz_mat4x4_transform_point3(
            &position_matrix,
            shz_vec3_init(source->position.x, source->position.y,
                          source->position.z));
        transformed_normal = shz_mat3x3_transform_vec3(
            &normal_matrix,
            shz_vec3_init(source->normal.x, source->normal.y,
                          source->normal.z));
        px = transformed_position.x;
        py = transformed_position.y;
        pz = transformed_position.z;
        nx = transformed_normal.x;
        ny = transformed_normal.y;
        nz = transformed_normal.z;
    }
#else
    px = (*position)[0][0] * source->position.x +
         (*position)[1][0] * source->position.y +
         (*position)[2][0] * source->position.z + (*position)[3][0];
    py = (*position)[0][1] * source->position.x +
         (*position)[1][1] * source->position.y +
         (*position)[2][1] * source->position.z + (*position)[3][1];
    pz = (*position)[0][2] * source->position.x +
         (*position)[1][2] * source->position.y +
         (*position)[2][2] * source->position.z + (*position)[3][2];
    nx = normal->column[0][0] * source->normal.x +
         normal->column[1][0] * source->normal.y +
         normal->column[2][0] * source->normal.z;
    ny = normal->column[0][1] * source->normal.x +
         normal->column[1][1] * source->normal.y +
         normal->column[2][1] * source->normal.z;
    nz = normal->column[0][2] * source->normal.x +
         normal->column[1][2] * source->normal.y +
         normal->column[2][2] * source->normal.z;
#endif
    vertex->position.x += px * weight;
    vertex->position.y += py * weight;
    vertex->position.z += pz * weight;
    vertex->normal.x += nx * weight;
    vertex->normal.y += ny * weight;
    vertex->normal.z += nz * weight;
}

int pvr_skin_apply(pvr_deform_vertex_t *output, size_t output_capacity,
                   const pvr_deform_stream_t *vertices,
                   const pvr_skin_stream_t *influences,
                   const pvr_skin_palette_t *palette,
                   pvr_deform_result_t *result) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes;
    size_t output_bytes;
    size_t influence_bytes;
    size_t position_bytes;
    size_t normal_bytes;
    size_t i;

    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(!influences || !palette || !influences->influences ||
       influences->vertex_count != vertices->vertex_count ||
       ((uintptr_t)influences->influences &
        (_Alignof(pvr_skin_influences_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_influences_t) ||
       (influences->stride & 3u) || !palette->position_matrices ||
       !palette->normal_matrices || !palette->joint_count ||
       ((uintptr_t)palette->position_matrices &
        (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)palette->normal_matrices &
        (_Alignof(pvr_normal_matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_influences_t), &influence_bytes) < 0 ||
       !address_range(influences->influences, influence_bytes) ||
       palette->joint_count > SIZE_MAX / sizeof(matrix_t) ||
       palette->joint_count > SIZE_MAX / sizeof(pvr_normal_matrix_t)) {
        errno = ERANGE;
        return -1;
    }
    position_bytes = palette->joint_count * sizeof(matrix_t);
    normal_bytes = palette->joint_count * sizeof(pvr_normal_matrix_t);
    if(!address_range(palette->position_matrices, position_bytes) ||
       !address_range(palette->normal_matrices, normal_bytes)) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(influences->influences, influence_bytes,
                      output, output_bytes) ||
       ranges_overlap(palette->position_matrices, position_bytes,
                      output, output_bytes) ||
       ranges_overlap(palette->normal_matrices, normal_bytes,
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < palette->joint_count; ++i) {
        if(!matrix_finite(palette->position_matrices + i) ||
           !normal_matrix_finite(palette->normal_matrices + i)) {
            errno = EDOM;
            return -1;
        }
    }
    for(i = 0; i < influences->vertex_count; ++i) {
        const pvr_skin_influences_t *influence =
            (const pvr_skin_influences_t *)
            ((const uint8_t *)influences->influences + i * influences->stride);
        float total = 0.0f;
        size_t slot;

        for(slot = 0; slot < 4; ++slot) {
            if(!isfinite(influence->weight[slot]) ||
               influence->weight[slot] < 0.0f ||
               (influence->weight[slot] > 0.0f &&
                influence->joint[slot] >= palette->joint_count)) {
                errno = EILSEQ;
                return -1;
            }
            total += influence->weight[slot];
        }
        if(!isfinite(total) || total <= FLT_MIN) {
            errno = EILSEQ;
            return -1;
        }
    }

    for(i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_influences_t *influence =
            (const pvr_skin_influences_t *)
            ((const uint8_t *)influences->influences + i * influences->stride);
        pvr_deform_vertex_t vertex = { 0 };
        float total = influence->weight[0] + influence->weight[1] +
                      influence->weight[2] + influence->weight[3];
        size_t slot;

        if(!vertex_finite(source)) {
            errno = EDOM;
            goto skin_fail;
        }
        for(slot = 0; slot < 4; ++slot) {
            float weight;

            if(influence->weight[slot] == 0.0f)
                continue;
            weight = influence->weight[slot] / total;
            skin_accumulate(source, palette, influence->joint[slot], weight,
                            &vertex);
        }
        if(!finite3(vertex.position.x, vertex.position.y, vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y, &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto skin_fail;
        }
        vertex.position.w = 1.0f;
        vertex.normal.w = 0.0f;
        memcpy(output + i, &vertex, sizeof(vertex));
        ++progress.deformed_vertices;
    }
    if(result)
        *result = progress;
    return 0;

skin_fail:
    if(result)
        *result = progress;
    return -1;
}

int pvr_skin_apply_spans(pvr_deform_vertex_t *output,
                         size_t output_capacity,
                         const pvr_deform_stream_t *vertices,
                         const pvr_skin_span_stream_t *influences,
                         const pvr_skin_palette_t *palette,
                         pvr_deform_result_t *result) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes;
    size_t output_bytes;
    size_t span_bytes;
    size_t weight_bytes;
    size_t position_bytes;
    size_t normal_bytes;
    size_t i;

    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(!influences || !palette || !influences->spans ||
       !influences->weights || !influences->weight_count ||
       influences->vertex_count != vertices->vertex_count ||
       ((uintptr_t)influences->spans &
        (_Alignof(pvr_skin_span_t) - 1u)) ||
       ((uintptr_t)influences->weights &
        (_Alignof(pvr_skin_weight_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_span_t) ||
       (influences->stride & 3u) || !palette->position_matrices ||
       !palette->normal_matrices || !palette->joint_count ||
       ((uintptr_t)palette->position_matrices &
        (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)palette->normal_matrices &
        (_Alignof(pvr_normal_matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_span_t), &span_bytes) < 0 ||
       influences->weight_count > SIZE_MAX / sizeof(pvr_skin_weight_t) ||
       palette->joint_count > SIZE_MAX / sizeof(matrix_t) ||
       palette->joint_count > SIZE_MAX / sizeof(pvr_normal_matrix_t)) {
        errno = ERANGE;
        return -1;
    }
    weight_bytes = influences->weight_count * sizeof(pvr_skin_weight_t);
    position_bytes = palette->joint_count * sizeof(matrix_t);
    normal_bytes = palette->joint_count * sizeof(pvr_normal_matrix_t);
    if(!address_range(influences->spans, span_bytes) ||
       !address_range(influences->weights, weight_bytes) ||
       !address_range(palette->position_matrices, position_bytes) ||
       !address_range(palette->normal_matrices, normal_bytes)) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(influences->spans, span_bytes, output, output_bytes) ||
       ranges_overlap(influences->weights, weight_bytes,
                      output, output_bytes) ||
       ranges_overlap(palette->position_matrices, position_bytes,
                      output, output_bytes) ||
       ranges_overlap(palette->normal_matrices, normal_bytes,
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < palette->joint_count; ++i) {
        if(!matrix_finite(palette->position_matrices + i) ||
           !normal_matrix_finite(palette->normal_matrices + i)) {
            errno = EDOM;
            return -1;
        }
    }

    /* Validate every span and referenced weight before an in-place output can
       overwrite the vertex stream. Individual source values remain bounded
       prefix errors, matching the four-influence path. */
    for(i = 0; i < influences->vertex_count; ++i) {
        const pvr_skin_span_t *span = (const pvr_skin_span_t *)
            ((const uint8_t *)influences->spans + i * influences->stride);
        float total = 0.0f;
        size_t slot;

        if(span->reserved || !span->weight_count ||
           span->first_weight > influences->weight_count ||
           span->weight_count >
           influences->weight_count - span->first_weight) {
            errno = EILSEQ;
            return -1;
        }
        for(slot = 0; slot < span->weight_count; ++slot) {
            const pvr_skin_weight_t *weight = influences->weights +
                span->first_weight + slot;

            if(weight->reserved || !isfinite(weight->weight) ||
               weight->weight < 0.0f ||
               (weight->weight > 0.0f &&
                weight->joint >= palette->joint_count)) {
                errno = EILSEQ;
                return -1;
            }
            total += weight->weight;
        }
        if(!isfinite(total) || total <= FLT_MIN) {
            errno = EILSEQ;
            return -1;
        }
    }

    for(i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_span_t *span = (const pvr_skin_span_t *)
            ((const uint8_t *)influences->spans + i * influences->stride);
        pvr_deform_vertex_t vertex = { 0 };
        float total = 0.0f;
        size_t slot;

        if(!vertex_finite(source)) {
            errno = EDOM;
            goto span_fail;
        }
        for(slot = 0; slot < span->weight_count; ++slot)
            total += influences->weights[span->first_weight + slot].weight;
        for(slot = 0; slot < span->weight_count; ++slot) {
            const pvr_skin_weight_t *weight = influences->weights +
                span->first_weight + slot;

            if(weight->weight == 0.0f)
                continue;
            skin_accumulate(source, palette, weight->joint,
                            weight->weight / total, &vertex);
        }
        if(!finite3(vertex.position.x, vertex.position.y,
                    vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y,
                     &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto span_fail;
        }
        vertex.position.w = 1.0f;
        vertex.normal.w = 0.0f;
        memcpy(output + i, &vertex, sizeof(vertex));
        ++progress.deformed_vertices;
    }
    if(result)
        *result = progress;
    return 0;

span_fail:
    if(result)
        *result = progress;
    return -1;
}
