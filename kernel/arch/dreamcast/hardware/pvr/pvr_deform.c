/* KallistiOS ##version##

   pvr_deform.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_skin_prepared.h>
#include "pvr_skin_internal.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(pvr_normal_matrix_t) == sizeof(shz_mat3x3_t),
               "normal matrix bridge must preserve all components");
_Static_assert(sizeof(pvr_skin_span_t) == 8u,
               "skin spans must occupy 8 bytes");
_Static_assert(sizeof(pvr_skin_weight_t) == 8u,
               "skin weights must occupy 8 bytes");
_Static_assert(sizeof(pvr_skin_compact_joint_t) == 21u * sizeof(float),
               "compact joints must retain only 12 position and 9 normal floats");

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

int pvr_deform_bounds_calculate(const pvr_deform_stream_t *vertices,
                                pvr_deform_bounds_t *bounds) {
    pvr_deform_bounds_t calculated;
    const pvr_deform_vertex_t *vertex;
    size_t bytes;
    size_t index;
    size_t component;
    double radius_squared = 0.0;

    if(!vertices || !bounds || !vertices->vertices ||
       !vertices->vertex_count ||
       ((uintptr_t)vertices->vertices &
        (_Alignof(pvr_deform_vertex_t) - 1u)) ||
       vertices->stride < sizeof(pvr_deform_vertex_t) ||
       (vertices->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(vertices->vertex_count, vertices->stride,
                  sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       !address_range(vertices->vertices, bytes)) {
        errno = ERANGE;
        return -1;
    }
    vertex = vertices->vertices;
    if(!finite3(vertex->position.x, vertex->position.y,
                vertex->position.z)) {
        errno = EDOM;
        return -1;
    }
    calculated.minimum = vertex->position;
    calculated.maximum = vertex->position;
    for(index = 1; index < vertices->vertex_count; ++index) {
        const float *position;
        float *minimum = &calculated.minimum.x;
        float *maximum = &calculated.maximum.x;

        vertex = (const pvr_deform_vertex_t *)((const uint8_t *)
            vertices->vertices + index * vertices->stride);
        if(!finite3(vertex->position.x, vertex->position.y,
                    vertex->position.z)) {
            errno = EDOM;
            return -1;
        }
        position = &vertex->position.x;
        for(component = 0; component < 3; ++component) {
            if(position[component] < minimum[component])
                minimum[component] = position[component];
            if(position[component] > maximum[component])
                maximum[component] = position[component];
        }
    }
    calculated.minimum.w = 1.0f;
    calculated.maximum.w = 1.0f;
    for(component = 0; component < 3; ++component) {
        const float *minimum = &calculated.minimum.x;
        const float *maximum = &calculated.maximum.x;
        float *center = &calculated.center.x;
        double half = ((double)maximum[component] -
                       (double)minimum[component]) * 0.5;
        double midpoint = (double)minimum[component] + half;

        if(!isfinite(half) || !isfinite(midpoint) ||
           fabs(midpoint) > FLT_MAX) {
            errno = ERANGE;
            return -1;
        }
        center[component] = (float)midpoint;
        radius_squared += half * half;
    }
    if(!isfinite(radius_squared) || sqrt(radius_squared) > FLT_MAX) {
        errno = ERANGE;
        return -1;
    }
    calculated.center.w = 1.0f;
    calculated.radius = (float)sqrt(radius_squared);
    *bounds = calculated;
    return 0;
}

static int normalize(float *x, float *y, float *z) {
    float length_squared = *x * *x + *y * *y + *z * *z;
    float reciprocal;

    if(!isfinite(length_squared) || length_squared <= FLT_MIN)
        return -1;
    reciprocal = shz_inv_sqrtf_fsrra(length_squared);
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

#define SKIN_INFLUENCES_VERSION UINT32_C(0x53494e01)
#define SKIN_SPANS_VERSION UINT32_C(0x53535001)

static int palette_ranges(const pvr_skin_palette_t *palette,
                          size_t *position_bytes, size_t *normal_bytes) {
    if(!palette || !palette->position_matrices || !palette->normal_matrices ||
       !palette->joint_count ||
       ((uintptr_t)palette->position_matrices & (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)palette->normal_matrices &
        (_Alignof(pvr_normal_matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(palette->joint_count > SIZE_MAX / sizeof(matrix_t) ||
       palette->joint_count > SIZE_MAX / sizeof(pvr_normal_matrix_t)) {
        errno = ERANGE;
        return -1;
    }
    *position_bytes = palette->joint_count * sizeof(matrix_t);
    *normal_bytes = palette->joint_count * sizeof(pvr_normal_matrix_t);
    if(!address_range(palette->position_matrices, *position_bytes) ||
       !address_range(palette->normal_matrices, *normal_bytes)) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

static int palette_values(const pvr_skin_palette_t *palette) {
    for(size_t i = 0; i < palette->joint_count; ++i) {
        if(!matrix_finite(palette->position_matrices + i) ||
           !normal_matrix_finite(palette->normal_matrices + i)) {
            errno = EDOM;
            return -1;
        }
    }
    return 0;
}

static int skin_palette_prepare(const pvr_skin_palette_t *palette,
    void *storage, size_t joint_capacity, void *prepared, int compact) {
    size_t position_bytes, normal_bytes, storage_bytes;
    const size_t joint_size = compact ? sizeof(pvr_skin_compact_joint_t) :
                                       sizeof(pvr_skin_prepared_joint_t);
    const size_t joint_align = compact ? _Alignof(pvr_skin_compact_joint_t) :
                                        _Alignof(pvr_skin_prepared_joint_t);
    const size_t descriptor_size = compact ? sizeof(pvr_skin_compact_palette_t) :
                                            sizeof(pvr_skin_prepared_palette_t);
    const size_t descriptor_align = compact ? _Alignof(pvr_skin_compact_palette_t) :
                                             _Alignof(pvr_skin_prepared_palette_t);

    if(!storage || !prepared ||
       ((uintptr_t)storage & (joint_align - 1u)) ||
       ((uintptr_t)prepared & (descriptor_align - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(palette_ranges(palette, &position_bytes, &normal_bytes) < 0)
        return -1;
    if(joint_capacity < palette->joint_count) {
        errno = ENOSPC;
        return -1;
    }
    if(palette->joint_count > SIZE_MAX / joint_size) {
        errno = ERANGE;
        return -1;
    }
    storage_bytes = palette->joint_count * joint_size;
    if(!address_range(storage, storage_bytes) ||
       !address_range(prepared, descriptor_size) ||
       !address_range(palette, sizeof(*palette))) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(storage, storage_bytes, prepared, descriptor_size) ||
       ranges_overlap(storage, storage_bytes, palette, sizeof(*palette)) ||
       ranges_overlap(prepared, descriptor_size, palette, sizeof(*palette)) ||
       ranges_overlap(storage, storage_bytes,
                      palette->position_matrices, position_bytes) ||
       ranges_overlap(storage, storage_bytes,
                      palette->normal_matrices, normal_bytes) ||
       ranges_overlap(prepared, descriptor_size,
                      palette->position_matrices, position_bytes) ||
       ranges_overlap(prepared, descriptor_size,
                      palette->normal_matrices, normal_bytes)) {
        errno = EINVAL;
        return -1;
    }
    if(palette_values(palette) < 0)
        return -1;
    if(compact) {
        for(size_t i = 0; i < palette->joint_count; ++i) {
            const matrix_t *m = palette->position_matrices + i;
            if((*m)[0][3] != 0 || (*m)[1][3] != 0 ||
               (*m)[2][3] != 0 || (*m)[3][3] != 1) {
                errno = EDOM;
                return -1;
            }
        }
    }
    for(size_t i = 0; i < palette->joint_count; ++i) {
        if(compact) {
            pvr_skin_compact_joint_t *joint = (pvr_skin_compact_joint_t *)storage + i;
            const matrix_t *m = palette->position_matrices + i;
            for(size_t c = 0; c < 4; ++c)
                joint->position.col[c] = shz_vec3_init((*m)[c][0], (*m)[c][1], (*m)[c][2]);
            memcpy(&joint->normal, palette->normal_matrices + i, sizeof(joint->normal));
        }
        else {
            pvr_skin_prepared_joint_t *joint = (pvr_skin_prepared_joint_t *)storage + i;
            shz_kos_matrix_import(&joint->position, palette->position_matrices + i);
            memcpy(&joint->normal, palette->normal_matrices + i, sizeof(joint->normal));
        }
    }
    if(compact)
        *(pvr_skin_compact_palette_t *)prepared = (pvr_skin_compact_palette_t){
            storage, palette->joint_count, SKIN_COMPACT_PALETTE_VERSION};
    else
        *(pvr_skin_prepared_palette_t *)prepared = (pvr_skin_prepared_palette_t){
            storage, palette->joint_count, SKIN_PALETTE_VERSION};
    return 0;
}

int pvr_skin_palette_prepare(const pvr_skin_palette_t *palette,
    pvr_skin_prepared_joint_t *storage, size_t joint_capacity,
    pvr_skin_prepared_palette_t *prepared) {
    return skin_palette_prepare(palette, storage, joint_capacity, prepared, 0);
}

int pvr_skin_palette_prepare_compact(const pvr_skin_palette_t *palette,
    pvr_skin_compact_joint_t *storage, size_t joint_capacity,
    pvr_skin_compact_palette_t *prepared) {
    return skin_palette_prepare(palette, storage, joint_capacity, prepared, 1);
}

static int skin_palette_preflight(const pvr_skin_palette_t *palette,
    const pvr_skin_prepared_palette_t *prepared, const void *output,
    size_t output_bytes, size_t *joint_count) {
    size_t position_bytes, normal_bytes;

    if(prepared) {
        size_t bytes;
        if(((uintptr_t)prepared &
            (_Alignof(pvr_skin_prepared_palette_t) - 1u)) ||
           prepared->version != SKIN_PALETTE_VERSION || !prepared->joints ||
           !prepared->joint_count ||
           ((uintptr_t)prepared->joints &
            (_Alignof(pvr_skin_prepared_joint_t) - 1u))) {
            errno = EINVAL;
            return -1;
        }
        if(prepared->joint_count > SIZE_MAX / sizeof(*prepared->joints)) {
            errno = ERANGE;
            return -1;
        }
        bytes = prepared->joint_count * sizeof(*prepared->joints);
        if(!address_range(prepared->joints, bytes) ||
           !address_range(prepared, sizeof(*prepared))) {
            errno = ERANGE;
            return -1;
        }
        if(ranges_overlap(prepared->joints, bytes, output, output_bytes) ||
           ranges_overlap(prepared, sizeof(*prepared), output, output_bytes)) {
            errno = EINVAL;
            return -1;
        }
        *joint_count = prepared->joint_count;
        return 0;
    }
    if(palette_ranges(palette, &position_bytes, &normal_bytes) < 0)
        return -1;
    if(ranges_overlap(palette->position_matrices, position_bytes,
                      output, output_bytes) ||
       ranges_overlap(palette->normal_matrices, normal_bytes,
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }
    if(palette_values(palette) < 0)
        return -1;
    *joint_count = palette->joint_count;
    return 0;
}

static void skin_accumulate(const pvr_deform_vertex_t *source,
                            const pvr_skin_palette_t *palette,
                            const pvr_skin_prepared_palette_t *prepared,
                            uint16_t joint, float weight,
                            pvr_deform_vertex_t *vertex) {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;

    {
        shz_mat4x4_t position_matrix;
        shz_mat3x3_t normal_matrix;
        const shz_mat4x4_t *position;
        const shz_mat3x3_t *normal;
        shz_vec3_t transformed_position;
        shz_vec3_t transformed_normal;

        /* One-off transforms preserve XMTRX. Let SH4ZAM select its portable
           backend for host tools so they exercise the same integration. */
        if(prepared) {
            position = &prepared->joints[joint].position;
            normal = &prepared->joints[joint].normal;
        }
        else {
            shz_kos_matrix_import(&position_matrix,
                                  palette->position_matrices + joint);
            memcpy(&normal_matrix, palette->normal_matrices + joint,
                   sizeof(normal_matrix));
            position = &position_matrix;
            normal = &normal_matrix;
        }
        transformed_position = shz_mat4x4_transform_point3(
            position,
            shz_vec3_init(source->position.x, source->position.y,
                          source->position.z));
        transformed_normal = shz_mat3x3_transform_vec3(
            normal,
            shz_vec3_init(source->normal.x, source->normal.y,
                          source->normal.z));
        px = transformed_position.x;
        py = transformed_position.y;
        pz = transformed_position.z;
        nx = transformed_normal.x;
        ny = transformed_normal.y;
        nz = transformed_normal.z;
    }
    vertex->position.x += px * weight;
    vertex->position.y += py * weight;
    vertex->position.z += pz * weight;
    vertex->normal.x += nx * weight;
    vertex->normal.y += ny * weight;
    vertex->normal.z += nz * weight;
}

static int skin_apply(pvr_deform_vertex_t *output, size_t output_capacity,
                   const pvr_deform_stream_t *vertices,
                   const pvr_skin_stream_t *influences,
                   const pvr_skin_palette_t *palette,
                   pvr_deform_result_t *result,
                   const pvr_skin_prepared_palette_t *prepared) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes;
    size_t output_bytes;
    size_t influence_bytes;
    size_t joint_count;
    size_t i;

    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(!influences || !influences->influences ||
       influences->vertex_count != vertices->vertex_count ||
       ((uintptr_t)influences->influences &
        (_Alignof(pvr_skin_influences_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_influences_t) ||
       (influences->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_influences_t), &influence_bytes) < 0 ||
       !address_range(influences->influences, influence_bytes)) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(influences->influences, influence_bytes,
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    if(skin_palette_preflight(palette, prepared, output, output_bytes,
                              &joint_count) < 0)
        return -1;
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
                influence->joint[slot] >= joint_count)) {
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
            skin_accumulate(source, palette, prepared, influence->joint[slot],
                            weight, &vertex);
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

static int skin_apply_spans(pvr_deform_vertex_t *output,
                         size_t output_capacity,
                         const pvr_deform_stream_t *vertices,
                         const pvr_skin_span_stream_t *influences,
                         const pvr_skin_palette_t *palette,
                         pvr_deform_result_t *result,
                         const pvr_skin_prepared_palette_t *prepared) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes;
    size_t output_bytes;
    size_t span_bytes;
    size_t weight_bytes;
    size_t joint_count;
    size_t i;

    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(!influences || !influences->spans ||
       !influences->weights || !influences->weight_count ||
       influences->vertex_count != vertices->vertex_count ||
       ((uintptr_t)influences->spans &
        (_Alignof(pvr_skin_span_t) - 1u)) ||
       ((uintptr_t)influences->weights &
        (_Alignof(pvr_skin_weight_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_span_t) ||
       (influences->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_span_t), &span_bytes) < 0 ||
       influences->weight_count > SIZE_MAX / sizeof(pvr_skin_weight_t)) {
        errno = ERANGE;
        return -1;
    }
    weight_bytes = influences->weight_count * sizeof(pvr_skin_weight_t);
    if(!address_range(influences->spans, span_bytes) ||
       !address_range(influences->weights, weight_bytes)) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(influences->spans, span_bytes, output, output_bytes) ||
       ranges_overlap(influences->weights, weight_bytes,
                      output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

    if(skin_palette_preflight(palette, prepared, output, output_bytes,
                              &joint_count) < 0)
        return -1;

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
                weight->joint >= joint_count)) {
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
            skin_accumulate(source, palette, prepared, weight->joint,
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

int pvr_skin_apply(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices, const pvr_skin_stream_t *influences,
    const pvr_skin_palette_t *palette, pvr_deform_result_t *result) {
    return skin_apply(output, output_capacity, vertices, influences, palette,
                      result, NULL);
}

int pvr_skin_apply_spans(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices,
    const pvr_skin_span_stream_t *influences,
    const pvr_skin_palette_t *palette, pvr_deform_result_t *result) {
    return skin_apply_spans(output, output_capacity, vertices, influences,
                            palette, result, NULL);
}

int pvr_skin_apply_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result) {
    return skin_apply(output, output_capacity, vertices, influences, NULL,
                      result, palette);
}

int pvr_skin_apply_spans_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_span_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result) {
    return skin_apply_spans(output, output_capacity, vertices, influences, NULL,
                            result, palette);
}

static int influence_total(const pvr_skin_influences_t *influence,
                            size_t joint_count, float *sum) {
    float total = 0.0f;
    for(size_t slot = 0; slot < 4; ++slot) {
        float weight = influence->weight[slot];
        if(!isfinite(weight) || weight < 0.0f ||
           (weight > 0.0f && influence->joint[slot] >= joint_count)) {
            errno = EILSEQ;
            return -1;
        }
        total += weight;
    }
    if(!isfinite(total) || total <= FLT_MIN) {
        errno = EILSEQ;
        return -1;
    }
    *sum = total;
    return 0;
}

int pvr_skin_influences_prepare(const pvr_skin_stream_t *influences,
    size_t joint_count, pvr_skin_prepared_influence_t *storage,
    size_t vertex_capacity, pvr_skin_prepared_influences_t *prepared) {
    size_t input_bytes, storage_bytes;
    pvr_skin_prepared_influences_t snapshot;

    if(!influences || !storage || !prepared || !joint_count ||
       ((uintptr_t)influences & (_Alignof(pvr_skin_stream_t) - 1u)) ||
       !influences->influences ||
       ((uintptr_t)influences->influences &
        (_Alignof(pvr_skin_influences_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_influences_t) ||
       (influences->stride & 3u) ||
       ((uintptr_t)storage & (_Alignof(pvr_skin_prepared_influence_t) - 1u)) ||
       ((uintptr_t)prepared & (_Alignof(pvr_skin_prepared_influences_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(vertex_capacity < influences->vertex_count) {
        errno = ENOSPC;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_influences_t), &input_bytes) < 0 ||
       influences->vertex_count > SIZE_MAX / sizeof(*storage)) {
        errno = ERANGE;
        return -1;
    }
    storage_bytes = influences->vertex_count * sizeof(*storage);
    if(!address_range(influences->influences, input_bytes) ||
       !address_range(storage, storage_bytes) ||
       !address_range(influences, sizeof(*influences)) ||
       !address_range(prepared, sizeof(*prepared))) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(storage, storage_bytes, prepared, sizeof(*prepared)) ||
       ranges_overlap(storage, storage_bytes, influences, sizeof(*influences)) ||
       ranges_overlap(prepared, sizeof(*prepared), influences, sizeof(*influences)) ||
       ranges_overlap(storage, storage_bytes, influences->influences, input_bytes) ||
       ranges_overlap(prepared, sizeof(*prepared), influences->influences, input_bytes)) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < influences->vertex_count; ++i) {
        const pvr_skin_influences_t *source = (const pvr_skin_influences_t *)
            ((const uint8_t *)influences->influences + i * influences->stride);
        float total;
        if(influence_total(source, joint_count, &total) < 0)
            return -1;
    }
    for(size_t i = 0; i < influences->vertex_count; ++i) {
        const pvr_skin_influences_t *source = (const pvr_skin_influences_t *)
            ((const uint8_t *)influences->influences + i * influences->stride);
        float total = source->weight[0] + source->weight[1] +
                      source->weight[2] + source->weight[3];
        pvr_skin_prepared_influence_t record = { { 0 }, 0, { 0 } };
        for(size_t slot = 0; slot < 4; ++slot) {
            if(source->weight[slot] == 0.0f)
                continue;
            record.active_mask |= 1u << slot;
            record.joint[slot] = source->joint[slot];
            record.weight[slot] = source->weight[slot] / total;
        }
        storage[i] = record;
    }
    snapshot.influences = storage;
    snapshot.vertex_count = influences->vertex_count;
    snapshot.joint_count = joint_count;
    snapshot.version = SKIN_INFLUENCES_VERSION;
    *prepared = snapshot;
    return 0;
}

static int prepared_influences_preflight(
    const pvr_skin_prepared_influences_t *influences,
    size_t vertex_count, void *output, size_t output_bytes) {
    size_t influence_bytes;
    if(!influences ||
       ((uintptr_t)influences & (_Alignof(pvr_skin_prepared_influences_t) - 1u)) ||
       influences->version != SKIN_INFLUENCES_VERSION || !influences->influences ||
       influences->vertex_count != vertex_count ||
       ((uintptr_t)influences->influences &
        (_Alignof(pvr_skin_prepared_influence_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(influences->vertex_count > SIZE_MAX / sizeof(*influences->influences)) {
        errno = ERANGE;
        return -1;
    }
    influence_bytes = influences->vertex_count * sizeof(*influences->influences);
    if(!address_range(influences->influences, influence_bytes) ||
       !address_range(influences, sizeof(*influences))) {
        errno = ERANGE;
        return -1;
    }
    if(ranges_overlap(influences->influences, influence_bytes, output, output_bytes) ||
       ranges_overlap(influences, sizeof(*influences), output, output_bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_skin_apply_prepared(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_influences_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes, output_bytes, joint_count;

    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes,
                        &output_bytes) < 0)
        return -1;
    if(prepared_influences_preflight(influences, vertices->vertex_count,
                                    output, output_bytes) < 0 ||
       skin_palette_preflight(NULL, palette, output, output_bytes, &joint_count) < 0)
        return -1;
    if(influences->joint_count != joint_count) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_prepared_influence_t *influence = influences->influences + i;
        pvr_deform_vertex_t vertex = { 0 };
        if(!vertex_finite(source)) {
            errno = EDOM;
            goto fail;
        }
        for(size_t slot = 0; slot < 4; ++slot) {
            if(influence->active_mask & (1u << slot))
                skin_accumulate(source, NULL, palette, influence->joint[slot],
                                influence->weight[slot], &vertex);
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

typedef struct skin_region {
    const void *pointer;
    size_t bytes;
} skin_region_t;

/* Range framing: sources may alias each other, destinations may
   alias neither sources nor one another. Empty regions occupy no storage. */
static int skin_destinations_disjoint(const skin_region_t *sources,
    size_t source_count, const skin_region_t *destinations, size_t count) {
    for(size_t i = 0; i < count; ++i) {
        if(!address_range(destinations[i].pointer, destinations[i].bytes)) {
            errno = ERANGE;
            return -1;
        }
        if(!destinations[i].bytes)
            continue;
        for(size_t j = 0; j < source_count; ++j) {
            if(sources[j].bytes && ranges_overlap(destinations[i].pointer,
                destinations[i].bytes, sources[j].pointer, sources[j].bytes)) {
                errno = EINVAL;
                return -1;
            }
        }
        for(size_t j = 0; j < i; ++j) {
            if(destinations[j].bytes && ranges_overlap(destinations[i].pointer,
                destinations[i].bytes, destinations[j].pointer, destinations[j].bytes)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

static int skin_spans_scan(const pvr_skin_span_stream_t *influences,
    size_t joint_count, skin_region_t sources[3],
    pvr_skin_span_plan_requirements_t *requirements) {
    size_t span_bytes, weight_bytes, active_count = 0;
    if(!influences || !joint_count ||
       ((uintptr_t)influences & (_Alignof(pvr_skin_span_stream_t) - 1u)) ||
       !influences->spans || !influences->weights || !influences->weight_count ||
       ((uintptr_t)influences->spans & (_Alignof(pvr_skin_span_t) - 1u)) ||
       ((uintptr_t)influences->weights & (_Alignof(pvr_skin_weight_t) - 1u)) ||
       influences->stride < sizeof(pvr_skin_span_t) || (influences->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(range_size(influences->vertex_count, influences->stride,
                  sizeof(pvr_skin_span_t), &span_bytes) < 0 ||
       influences->weight_count > SIZE_MAX / sizeof(pvr_skin_weight_t) ||
       influences->vertex_count > SIZE_MAX / sizeof(pvr_skin_prepared_span_t)) {
        errno = ERANGE;
        return -1;
    }
    weight_bytes = influences->weight_count * sizeof(pvr_skin_weight_t);
    sources[0] = (skin_region_t){ influences, sizeof(*influences) };
    sources[1] = (skin_region_t){ influences->spans, span_bytes };
    sources[2] = (skin_region_t){ influences->weights, weight_bytes };
    for(size_t i = 0; i < 3; ++i) {
        if(!address_range(sources[i].pointer, sources[i].bytes)) {
            errno = ERANGE;
            return -1;
        }
    }
    for(size_t i = 0; i < influences->vertex_count; ++i) {
        const pvr_skin_span_t *span = (const pvr_skin_span_t *)
            ((const uint8_t *)influences->spans + i * influences->stride);
        float total = 0.0f;
        size_t active = 0;
        if(span->reserved || !span->weight_count ||
           span->first_weight > influences->weight_count ||
           span->weight_count > influences->weight_count - span->first_weight) {
            errno = EILSEQ;
            return -1;
        }
        for(size_t s = 0; s < span->weight_count; ++s) {
            const pvr_skin_weight_t *weight = influences->weights + span->first_weight + s;
            if(weight->reserved || !isfinite(weight->weight) || weight->weight < 0 ||
               (weight->weight > 0 && weight->joint >= joint_count)) {
                errno = EILSEQ;
                return -1;
            }
            total += weight->weight;
            if(weight->weight > 0)
                ++active;
        }
        if(!isfinite(total) || total <= FLT_MIN) {
            errno = EILSEQ;
            return -1;
        }
        if(active > SIZE_MAX / sizeof(pvr_skin_weight_t) - active_count) {
            errno = ERANGE;
            return -1;
        }
        active_count += active;
    }
    requirements->span_count = influences->vertex_count;
    requirements->weight_count = active_count;
    return 0;
}

int pvr_skin_spans_prepare_query(const pvr_skin_span_stream_t *influences,
    size_t joint_count, pvr_skin_span_plan_requirements_t *requirements) {
    skin_region_t sources[3];
    pvr_skin_span_plan_requirements_t calculated;
    if(!requirements || ((uintptr_t)requirements &
                         (_Alignof(pvr_skin_span_plan_requirements_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(skin_spans_scan(influences, joint_count, sources, &calculated) < 0)
        return -1;
    const skin_region_t destination = { requirements, sizeof(*requirements) };
    if(skin_destinations_disjoint(sources, 3, &destination, 1) < 0)
        return -1;
    *requirements = calculated;
    return 0;
}

int pvr_skin_spans_prepare(const pvr_skin_span_stream_t *influences,
    size_t joint_count, pvr_skin_prepared_span_t *spans, size_t span_capacity,
    pvr_skin_weight_t *weights, size_t weight_capacity,
    pvr_skin_prepared_spans_t *prepared) {
    skin_region_t sources[3];
    pvr_skin_span_plan_requirements_t requirements;
    pvr_skin_prepared_spans_t snapshot;
    size_t next = 0;
    if(!spans || !weights || !prepared ||
       ((uintptr_t)spans & (_Alignof(pvr_skin_prepared_span_t) - 1u)) ||
       ((uintptr_t)weights & (_Alignof(pvr_skin_weight_t) - 1u)) ||
       ((uintptr_t)prepared & (_Alignof(pvr_skin_prepared_spans_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(skin_spans_scan(influences, joint_count, sources, &requirements) < 0)
        return -1;
    if(span_capacity < requirements.span_count || weight_capacity < requirements.weight_count) {
        errno = ENOSPC;
        return -1;
    }
    const skin_region_t destinations[] = {
        { spans, requirements.span_count * sizeof(*spans) },
        { weights, requirements.weight_count * sizeof(*weights) },
        { prepared, sizeof(*prepared) }
    };
    if(skin_destinations_disjoint(sources, 3, destinations, 3) < 0)
        return -1;
    for(size_t i = 0; i < requirements.span_count; ++i) {
        const pvr_skin_span_t *source = (const pvr_skin_span_t *)
            ((const uint8_t *)influences->spans + i * influences->stride);
        float total = 0;
        spans[i].first_weight = next;
        for(size_t s = 0; s < source->weight_count; ++s)
            total += influences->weights[source->first_weight + s].weight;
        for(size_t s = 0; s < source->weight_count; ++s) {
            const pvr_skin_weight_t *weight = influences->weights + source->first_weight + s;
            if(weight->weight == 0)
                continue;
            weights[next++] = (pvr_skin_weight_t){ weight->joint, 0, weight->weight / total };
        }
        spans[i].weight_count = next - spans[i].first_weight;
    }
    snapshot.spans = spans;
    snapshot.weights = weights;
    snapshot.vertex_count = requirements.span_count;
    snapshot.weight_count = requirements.weight_count;
    snapshot.joint_count = joint_count;
    snapshot.version = SKIN_SPANS_VERSION;
    *prepared = snapshot;
    return 0;
}

static int prepared_spans_preflight(const pvr_skin_prepared_spans_t *influences,
    size_t vertex_count, void *output, size_t output_bytes) {
    size_t span_bytes, weight_bytes;
    if(!influences || ((uintptr_t)influences &
                       (_Alignof(pvr_skin_prepared_spans_t) - 1u)) ||
       influences->version != SKIN_SPANS_VERSION ||
       !influences->spans || !influences->weights ||
       influences->vertex_count != vertex_count ||
       ((uintptr_t)influences->spans & (_Alignof(pvr_skin_prepared_span_t) - 1u)) ||
       ((uintptr_t)influences->weights & (_Alignof(pvr_skin_weight_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(influences->vertex_count > SIZE_MAX / sizeof(*influences->spans) ||
       influences->weight_count > SIZE_MAX / sizeof(*influences->weights)) {
        errno = ERANGE;
        return -1;
    }
    span_bytes = influences->vertex_count * sizeof(*influences->spans);
    weight_bytes = influences->weight_count * sizeof(*influences->weights);
    const skin_region_t sources[] = {
        { influences, sizeof(*influences) },
        { influences->spans, span_bytes }, { influences->weights, weight_bytes }
    };
    for(size_t i = 0; i < 3; ++i) {
        if(!address_range(sources[i].pointer, sources[i].bytes)) {
            errno = ERANGE;
            return -1;
        }
    }
    const skin_region_t destination = { output, output_bytes };
    return skin_destinations_disjoint(sources, 3, &destination, 1);
}

int pvr_skin_apply_spans_prepared(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_spans_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result) {
    pvr_deform_result_t progress = { 0 };
    size_t input_bytes, output_bytes, joint_count;
    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes, &output_bytes) < 0)
        return -1;
    if(prepared_spans_preflight(influences, vertices->vertex_count, output, output_bytes) < 0 ||
       skin_palette_preflight(NULL, palette, output, output_bytes, &joint_count) < 0)
        return -1;
    if(influences->joint_count != joint_count) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_prepared_span_t *span = influences->spans + i;
        pvr_deform_vertex_t vertex = { 0 };
        if(!vertex_finite(source)) {
            errno = EDOM;
            goto fail;
        }
        for(size_t s = 0; s < span->weight_count; ++s) {
            const pvr_skin_weight_t *weight = influences->weights + span->first_weight + s;
            skin_accumulate(source, NULL, palette, weight->joint, weight->weight, &vertex);
        }
        if(!finite3(vertex.position.x, vertex.position.y, vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y, &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto fail;
        }
        vertex.position.w = 1;
        vertex.normal.w = 0;
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

/* Sharing this leaf between two consumers must not introduce an out-of-line
   call for every influence; retain the original compact span loop's shape. */
SHZ_FORCE_INLINE void skin_accumulate_compact(const pvr_deform_vertex_t *source,
    const pvr_skin_compact_joint_t *joint, float weight,
    pvr_deform_vertex_t *vertex) {
    const shz_mat3x4_t *m = &joint->position;
    /* Three row dot products read exactly the twelve affine components.
       SH4ZAM uses FIPR, leaving XMTRX available to the caller. */
    const shz_vec3_t p = shz_vec4_dot3(
        shz_vec4_init(source->position.x, source->position.y, source->position.z, 1),
        shz_vec4_init(m->col[0].x, m->col[1].x, m->col[2].x, m->col[3].x),
        shz_vec4_init(m->col[0].y, m->col[1].y, m->col[2].y, m->col[3].y),
        shz_vec4_init(m->col[0].z, m->col[1].z, m->col[2].z, m->col[3].z));
    const shz_vec3_t n = shz_mat3x3_transform_vec3(&joint->normal,
        shz_vec3_init(source->normal.x, source->normal.y, source->normal.z));
    vertex->position.x += p.x * weight;
    vertex->position.y += p.y * weight;
    vertex->position.z += p.z * weight;
    vertex->normal.x += n.x * weight;
    vertex->normal.y += n.y * weight;
    vertex->normal.z += n.z * weight;
}

static int compact_palette_preflight(const pvr_skin_compact_palette_t *palette,
    size_t joint_count, void *output, size_t output_bytes) {
    size_t joint_bytes;
    if(!palette || ((uintptr_t)palette & (_Alignof(pvr_skin_compact_palette_t) - 1u)) ||
       !palette->joints || !palette->joint_count ||
       palette->version != SKIN_COMPACT_PALETTE_VERSION ||
       ((uintptr_t)palette->joints & (_Alignof(pvr_skin_compact_joint_t) - 1u)) ||
       palette->joint_count != joint_count) {
        errno = EINVAL;
        return -1;
    }
    if(palette->joint_count > SIZE_MAX / sizeof(*palette->joints)) {
        errno = ERANGE;
        return -1;
    }
    joint_bytes = palette->joint_count * sizeof(*palette->joints);
    if(!address_range(palette, sizeof(*palette)) || !address_range(palette->joints, joint_bytes)) {
        errno = ERANGE;
        return -1;
    }
    const skin_region_t sources[] = {
        {palette, sizeof(*palette)}, {palette->joints, joint_bytes}
    };
    const skin_region_t destination = {output, output_bytes};
    return skin_destinations_disjoint(sources, 2, &destination, 1);
}

int pvr_skin_apply_spans_compact(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_spans_t *influences,
    const pvr_skin_compact_palette_t *palette, pvr_deform_result_t *result) {
    pvr_deform_result_t progress = {0};
    size_t input_bytes, output_bytes;
    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes, &output_bytes) < 0 ||
       prepared_spans_preflight(influences, vertices->vertex_count, output, output_bytes) < 0 ||
       compact_palette_preflight(palette, influences->joint_count, output, output_bytes) < 0)
        return -1;
    for(size_t i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_prepared_span_t *span = influences->spans + i;
        pvr_deform_vertex_t vertex = {0};
        if(!vertex_finite(source)) {
            errno = EDOM;
            goto fail;
        }
        for(size_t s = 0; s < span->weight_count; ++s) {
            const pvr_skin_weight_t *weight = influences->weights + span->first_weight + s;
            skin_accumulate_compact(source, palette->joints + weight->joint,
                                    weight->weight, &vertex);
        }
        if(!finite3(vertex.position.x, vertex.position.y, vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y, &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto fail;
        }
        vertex.position.w = 1;
        vertex.normal.w = 0;
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

int pvr_skin_apply_compact(pvr_deform_vertex_t *output, size_t output_capacity,
    const pvr_deform_stream_t *vertices,
    const pvr_skin_prepared_influences_t *influences,
    const pvr_skin_compact_palette_t *palette, pvr_deform_result_t *result) {
    pvr_deform_result_t progress = {0};
    size_t input_bytes, output_bytes;
    if(result)
        *result = progress;
    if(stream_preflight(vertices, output, output_capacity, &input_bytes, &output_bytes) < 0 ||
       prepared_influences_preflight(influences, vertices->vertex_count, output, output_bytes) < 0 ||
       compact_palette_preflight(palette, influences->joint_count, output, output_bytes) < 0)
        return -1;
    for(size_t i = 0; i < vertices->vertex_count; ++i) {
        const pvr_deform_vertex_t *source = (const pvr_deform_vertex_t *)
            ((const uint8_t *)vertices->vertices + i * vertices->stride);
        const pvr_skin_prepared_influence_t *influence = influences->influences + i;
        pvr_deform_vertex_t vertex = {0};
        if(!vertex_finite(source)) {
            errno = EDOM;
            goto fail;
        }
        for(size_t slot = 0; slot < 4; ++slot) {
            if(influence->active_mask & (1u << slot))
                skin_accumulate_compact(source, palette->joints + influence->joint[slot],
                                        influence->weight[slot], &vertex);
        }
        if(!finite3(vertex.position.x, vertex.position.y, vertex.position.z) ||
           normalize(&vertex.normal.x, &vertex.normal.y, &vertex.normal.z) < 0) {
            errno = ERANGE;
            goto fail;
        }
        vertex.position.w = 1;
        vertex.normal.w = 0;
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
