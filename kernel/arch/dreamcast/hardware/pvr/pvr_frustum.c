/* KallistiOS ##version##

   pvr_frustum.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_frustum.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define FRUSTUM_PLANES 6u
#define CLIPPED_POLYGON_MAX 9u

typedef struct clip_vertex {
    float x;
    float y;
    float w;
    float u;
    float v;
    uint32_t argb;
    uint32_t oargb;
} clip_vertex_t;

typedef struct position_transform {
#ifdef __DREAMCAST__
    shz_mat4x4_t matrix;
#else
    const matrix_t *matrix;
#endif
} position_transform_t;

_Static_assert(sizeof(pvr_vertex_t) == 32,
               "canonical PVR vertices must occupy one TA block");

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
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

static int frustum_valid(const pvr_frustum_t *frustum) {
    return frustum && matrix_aligned(&frustum->object_to_screen) &&
           matrix_finite(&frustum->object_to_screen) &&
           isfinite(frustum->left) && isfinite(frustum->top) &&
           isfinite(frustum->right) && isfinite(frustum->bottom) &&
           isfinite(frustum->w_near) && isfinite(frustum->w_far) &&
           frustum->left < frustum->right &&
           frustum->top < frustum->bottom &&
           frustum->w_near > FLT_MIN &&
           frustum->w_near < frustum->w_far;
}

int pvr_frustum_init(pvr_frustum_t *frustum, const matrix_t *object_to_screen,
                     float left, float top, float right, float bottom,
                     float w_near, float w_far) {
    pvr_frustum_t candidate;

    if(!frustum || !object_to_screen || !matrix_aligned(object_to_screen) ||
       !matrix_aligned(&frustum->object_to_screen)) {
        errno = EINVAL;
        return -1;
    }

    if(!matrix_finite(object_to_screen) || !isfinite(left) ||
       !isfinite(top) || !isfinite(right) || !isfinite(bottom) ||
       !isfinite(w_near) || !isfinite(w_far) || left >= right ||
       top >= bottom || w_near <= FLT_MIN || w_near >= w_far) {
        errno = EDOM;
        return -1;
    }

    memcpy(&candidate.object_to_screen, object_to_screen,
           sizeof(candidate.object_to_screen));
    candidate.left = left;
    candidate.top = top;
    candidate.right = right;
    candidate.bottom = bottom;
    candidate.w_near = w_near;
    candidate.w_far = w_far;
    memcpy(frustum, &candidate, sizeof(candidate));
    return 0;
}

static void position_transform_init(position_transform_t *transform,
                                    const matrix_t *matrix) {
#ifdef __DREAMCAST__
    /* This memory-to-memory path uses FIPR rather than XMTRX. One imported
       matrix can therefore serve the complete cull or clip operation without
       disturbing application-owned accelerator state. */
    shz_kos_matrix_import(&transform->matrix, matrix);
#else
    transform->matrix = matrix;
#endif
}

static int transform_position(const position_transform_t *transform,
                              float x, float y,
                              float z, clip_vertex_t *out) {
#ifdef __DREAMCAST__
    shz_vec4_t result = shz_mat4x4_transform_vec4(
        &transform->matrix, shz_vec4_init(x, y, z, 1.0f));

    out->x = result.x;
    out->y = result.y;
    out->w = result.w;
#else
    const matrix_t *matrix = transform->matrix;

    out->x = (*matrix)[0][0] * x + (*matrix)[1][0] * y +
             (*matrix)[2][0] * z + (*matrix)[3][0];
    out->y = (*matrix)[0][1] * x + (*matrix)[1][1] * y +
             (*matrix)[2][1] * z + (*matrix)[3][1];
    out->w = (*matrix)[0][3] * x + (*matrix)[1][3] * y +
             (*matrix)[2][3] * z + (*matrix)[3][3];
#endif

    return isfinite(out->x) && isfinite(out->y) && isfinite(out->w);
}

static float plane_distance(const pvr_frustum_t *frustum,
                            const clip_vertex_t *vertex, size_t plane) {
    switch(plane) {
        case 0:
            return vertex->x - frustum->left * vertex->w;
        case 1:
            return frustum->right * vertex->w - vertex->x;
        case 2:
            return vertex->y - frustum->top * vertex->w;
        case 3:
            return frustum->bottom * vertex->w - vertex->y;
        case 4:
            return vertex->w - frustum->w_near;
        default:
            return frustum->w_far - vertex->w;
    }
}

int pvr_frustum_classify_aabb(const pvr_frustum_t *frustum,
                              const point_t *minimum, const point_t *maximum,
                              pvr_frustum_classification_t *result) {
    clip_vertex_t corners[8];
    pvr_frustum_classification_t classification = PVR_FRUSTUM_INSIDE;
    position_transform_t transform;
    size_t i;
    size_t plane;

    if(!frustum_valid(frustum) || !minimum || !maximum || !result) {
        errno = EINVAL;
        return -1;
    }

    if(!isfinite(minimum->x) || !isfinite(minimum->y) ||
       !isfinite(minimum->z) || !isfinite(maximum->x) ||
       !isfinite(maximum->y) || !isfinite(maximum->z) ||
       minimum->x > maximum->x || minimum->y > maximum->y ||
       minimum->z > maximum->z) {
        errno = EDOM;
        return -1;
    }

    position_transform_init(&transform, &frustum->object_to_screen);

    for(i = 0; i < 8; ++i) {
        float x = (i & 1u) ? maximum->x : minimum->x;
        float y = (i & 2u) ? maximum->y : minimum->y;
        float z = (i & 4u) ? maximum->z : minimum->z;

        if(!transform_position(&transform, x, y, z, corners + i)) {
            errno = ERANGE;
            return -1;
        }
    }

    for(plane = 0; plane < FRUSTUM_PLANES; ++plane) {
        size_t inside = 0;

        for(i = 0; i < 8; ++i) {
            float distance = plane_distance(frustum, corners + i, plane);

            if(!isfinite(distance)) {
                errno = ERANGE;
                return -1;
            }

            if(distance >= 0.0f)
                ++inside;
        }

        if(!inside) {
            *result = PVR_FRUSTUM_OUTSIDE;
            return 0;
        }

        if(inside != 8)
            classification = PVR_FRUSTUM_INTERSECT;
    }

    *result = classification;
    return 0;
}

static void object_plane(const pvr_frustum_t *frustum, size_t plane,
                         float coefficients[4]) {
    const matrix_t *matrix = &frustum->object_to_screen;
    float scale;

    switch(plane) {
        case 0:
            scale = -frustum->left;
            coefficients[0] = (*matrix)[0][0] + scale * (*matrix)[0][3];
            coefficients[1] = (*matrix)[1][0] + scale * (*matrix)[1][3];
            coefficients[2] = (*matrix)[2][0] + scale * (*matrix)[2][3];
            coefficients[3] = (*matrix)[3][0] + scale * (*matrix)[3][3];
            break;
        case 1:
            scale = frustum->right;
            coefficients[0] = scale * (*matrix)[0][3] - (*matrix)[0][0];
            coefficients[1] = scale * (*matrix)[1][3] - (*matrix)[1][0];
            coefficients[2] = scale * (*matrix)[2][3] - (*matrix)[2][0];
            coefficients[3] = scale * (*matrix)[3][3] - (*matrix)[3][0];
            break;
        case 2:
            scale = -frustum->top;
            coefficients[0] = (*matrix)[0][1] + scale * (*matrix)[0][3];
            coefficients[1] = (*matrix)[1][1] + scale * (*matrix)[1][3];
            coefficients[2] = (*matrix)[2][1] + scale * (*matrix)[2][3];
            coefficients[3] = (*matrix)[3][1] + scale * (*matrix)[3][3];
            break;
        case 3:
            scale = frustum->bottom;
            coefficients[0] = scale * (*matrix)[0][3] - (*matrix)[0][1];
            coefficients[1] = scale * (*matrix)[1][3] - (*matrix)[1][1];
            coefficients[2] = scale * (*matrix)[2][3] - (*matrix)[2][1];
            coefficients[3] = scale * (*matrix)[3][3] - (*matrix)[3][1];
            break;
        case 4:
            coefficients[0] = (*matrix)[0][3];
            coefficients[1] = (*matrix)[1][3];
            coefficients[2] = (*matrix)[2][3];
            coefficients[3] = (*matrix)[3][3] - frustum->w_near;
            break;
        default:
            coefficients[0] = -(*matrix)[0][3];
            coefficients[1] = -(*matrix)[1][3];
            coefficients[2] = -(*matrix)[2][3];
            coefficients[3] = frustum->w_far - (*matrix)[3][3];
            break;
    }
}

int pvr_frustum_classify_sphere(const pvr_frustum_t *frustum,
                                const point_t *center, float radius,
                                pvr_frustum_classification_t *result) {
    pvr_frustum_classification_t classification = PVR_FRUSTUM_INSIDE;
    size_t plane;

    if(!frustum_valid(frustum) || !center || !result) {
        errno = EINVAL;
        return -1;
    }
    if(!isfinite(center->x) || !isfinite(center->y) ||
       !isfinite(center->z) || !isfinite(radius) || radius < 0.0f) {
        errno = EDOM;
        return -1;
    }

    for(plane = 0; plane < FRUSTUM_PLANES; ++plane) {
        float coefficients[4];
        float normal_length;
        float distance;
        float extent;

        object_plane(frustum, plane, coefficients);
        normal_length = sqrtf(coefficients[0] * coefficients[0] +
                              coefficients[1] * coefficients[1] +
                              coefficients[2] * coefficients[2]);
        distance = coefficients[0] * center->x +
                   coefficients[1] * center->y +
                   coefficients[2] * center->z + coefficients[3];
        extent = radius * normal_length;
        if(!isfinite(normal_length) || !isfinite(distance) ||
           !isfinite(extent)) {
            errno = ERANGE;
            return -1;
        }
        if(distance < -extent) {
            *result = PVR_FRUSTUM_OUTSIDE;
            return 0;
        }
        if(distance < extent)
            classification = PVR_FRUSTUM_INTERSECT;
    }

    *result = classification;
    return 0;
}

int pvr_frustum_classify_triangle(
    const pvr_vertex_t input[3], const pvr_frustum_t *frustum,
    pvr_frustum_classification_t *result) {
    clip_vertex_t vertices[3];
    pvr_frustum_classification_t classification = PVR_FRUSTUM_INSIDE;
    position_transform_t transform;
    size_t vertex;
    size_t plane;

    if(!input || !frustum_valid(frustum) || !result) {
        errno = EINVAL;
        return -1;
    }

    position_transform_init(&transform, &frustum->object_to_screen);
    for(vertex = 0; vertex < 3; ++vertex) {
        if(!isfinite(input[vertex].x) || !isfinite(input[vertex].y) ||
           !isfinite(input[vertex].z)) {
            errno = EDOM;
            return -1;
        }
        if(!transform_position(&transform, input[vertex].x,
                               input[vertex].y, input[vertex].z,
                               vertices + vertex)) {
            errno = ERANGE;
            return -1;
        }
    }

    for(plane = 0; plane < FRUSTUM_PLANES; ++plane) {
        size_t inside = 0;

        for(vertex = 0; vertex < 3; ++vertex) {
            float distance = plane_distance(frustum, vertices + vertex,
                                            plane);

            if(!isfinite(distance)) {
                errno = ERANGE;
                return -1;
            }
            if(distance >= 0.0f)
                ++inside;
        }
        if(!inside) {
            *result = PVR_FRUSTUM_OUTSIDE;
            return 0;
        }
        if(inside != 3u)
            classification = PVR_FRUSTUM_INTERSECT;
    }

    *result = classification;
    return 0;
}

int pvr_frustum_project_modifier_warp(
    pvr_modifier_vol_t *output, const pvr_modifier_vol_t *input,
    const pvr_frustum_t *frustum) {
    alignas(32) pvr_modifier_vol_t staged;
    float source[3][3];
    float *destination[3][3];
    position_transform_t transform;
    size_t vertex;

    if(!output || !input || !frustum_valid(frustum) ||
       ((uintptr_t)output & 31u) || ((uintptr_t)input & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(input->flags != PVR_CMD_VERTEX &&
       input->flags != PVR_CMD_VERTEX_EOL) {
        errno = EILSEQ;
        return -1;
    }

    staged = *input;
    source[0][0] = input->ax;
    source[0][1] = input->ay;
    source[0][2] = input->az;
    source[1][0] = input->bx;
    source[1][1] = input->by;
    source[1][2] = input->bz;
    source[2][0] = input->cx;
    source[2][1] = input->cy;
    source[2][2] = input->cz;
    destination[0][0] = &staged.ax;
    destination[0][1] = &staged.ay;
    destination[0][2] = &staged.az;
    destination[1][0] = &staged.bx;
    destination[1][1] = &staged.by;
    destination[1][2] = &staged.bz;
    destination[2][0] = &staged.cx;
    destination[2][1] = &staged.cy;
    destination[2][2] = &staged.cz;
    position_transform_init(&transform, &frustum->object_to_screen);

    for(vertex = 0; vertex < 3u; ++vertex) {
        clip_vertex_t transformed;
        float reciprocal_w;

        if(!isfinite(source[vertex][0]) ||
           !isfinite(source[vertex][1]) ||
           !isfinite(source[vertex][2])) {
            errno = EDOM;
            return -1;
        }
        if(!transform_position(&transform, source[vertex][0],
                               source[vertex][1], source[vertex][2],
                               &transformed)) {
            errno = ERANGE;
            return -1;
        }

        /* Moving W, rather than clipping an edge, preserves the input
           topology. Identical shared positions map to identical screen
           positions, so a closed volume cannot acquire a near-plane hole. */
        if(transformed.w < frustum->w_near)
            transformed.w = frustum->w_near;
#ifdef __DREAMCAST__
        reciprocal_w = shz_invf(transformed.w);
#else
        reciprocal_w = 1.0f / transformed.w;
#endif
        *destination[vertex][0] = transformed.x * reciprocal_w;
        *destination[vertex][1] = transformed.y * reciprocal_w;
        *destination[vertex][2] = reciprocal_w;
        if(!isfinite(*destination[vertex][0]) ||
           !isfinite(*destination[vertex][1]) ||
           !isfinite(*destination[vertex][2])) {
            errno = ERANGE;
            return -1;
        }
    }

    memmove(output, &staged, sizeof(staged));
    return 0;
}

static uint32_t color_lerp(uint32_t lhs, uint32_t rhs, float amount) {
    uint32_t output = 0;
    unsigned int shift;

    for(shift = 0; shift < 32; shift += 8) {
        float a = (float)((lhs >> shift) & 0xffu);
        float b = (float)((rhs >> shift) & 0xffu);
        uint32_t channel;

#ifdef __DREAMCAST__
        channel = (uint32_t)(shz_lerpf(a, b, amount) + 0.5f);
#else
        channel = (uint32_t)(a + (b - a) * amount + 0.5f);
#endif

        if(channel > 255u)
            channel = 255u;
        output |= channel << shift;
    }

    return output;
}

static clip_vertex_t vertex_lerp(const clip_vertex_t *lhs,
                                 const clip_vertex_t *rhs, float amount,
                                 uint32_t attributes) {
    clip_vertex_t output = *lhs;

#ifdef __DREAMCAST__
    output.x = shz_lerpf(lhs->x, rhs->x, amount);
    output.y = shz_lerpf(lhs->y, rhs->y, amount);
    output.w = shz_lerpf(lhs->w, rhs->w, amount);
#else
    output.x = lhs->x + (rhs->x - lhs->x) * amount;
    output.y = lhs->y + (rhs->y - lhs->y) * amount;
    output.w = lhs->w + (rhs->w - lhs->w) * amount;
#endif

    if(attributes & PVR_FRUSTUM_CLIP_UV) {
#ifdef __DREAMCAST__
        output.u = shz_lerpf(lhs->u, rhs->u, amount);
        output.v = shz_lerpf(lhs->v, rhs->v, amount);
#else
        output.u = lhs->u + (rhs->u - lhs->u) * amount;
        output.v = lhs->v + (rhs->v - lhs->v) * amount;
#endif
    }

    if(attributes & PVR_FRUSTUM_CLIP_ARGB)
        output.argb = color_lerp(lhs->argb, rhs->argb, amount);

    if(attributes & PVR_FRUSTUM_CLIP_OARGB)
        output.oargb = color_lerp(lhs->oargb, rhs->oargb, amount);

    return output;
}

static int clip_plane(const pvr_frustum_t *frustum, size_t plane,
                      const clip_vertex_t *input, size_t input_count,
                      clip_vertex_t *output, size_t *output_count,
                      uint32_t attributes) {
    const clip_vertex_t *previous = input + input_count - 1u;
    float previous_distance = plane_distance(frustum, previous, plane);
    int previous_inside;
    size_t count = 0;
    size_t i;

    if(!isfinite(previous_distance))
        return -1;
    previous_inside = previous_distance >= 0.0f;

    for(i = 0; i < input_count; ++i) {
        const clip_vertex_t *current = input + i;
        float current_distance = plane_distance(frustum, current, plane);
        int current_inside;

        if(!isfinite(current_distance))
            return -1;
        current_inside = current_distance >= 0.0f;

        if(current_inside != previous_inside) {
            float denominator = previous_distance - current_distance;
            float amount;

            if(!isfinite(denominator) || fabsf(denominator) <= FLT_MIN)
                return -1;
#ifdef __DREAMCAST__
            amount = shz_divf(previous_distance, denominator);
#else
            amount = previous_distance / denominator;
#endif
            if(!isfinite(amount) || amount < 0.0f || amount > 1.0f ||
               count >= CLIPPED_POLYGON_MAX)
                return -1;
            output[count++] = vertex_lerp(previous, current, amount,
                                          attributes);
        }

        if(current_inside) {
            if(count >= CLIPPED_POLYGON_MAX)
                return -1;
            output[count++] = *current;
        }

        previous = current;
        previous_distance = current_distance;
        previous_inside = current_inside;
    }

    *output_count = count;
    return 0;
}

int pvr_frustum_clip_triangle(pvr_vertex_t *output, size_t output_capacity,
                              const pvr_vertex_t input[3],
                              const pvr_frustum_t *frustum,
                              uint32_t attributes,
                              pvr_frustum_clip_result_t *result) {
    alignas(32) pvr_vertex_t staged[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    clip_vertex_t polygons[2][CLIPPED_POLYGON_MAX];
    pvr_frustum_clip_result_t progress = { 0, 0 };
    position_transform_t transform;
    size_t count = 3;
    size_t plane;
    size_t triangle;
    size_t output_count;
    unsigned int current = 0;
    size_t i;

    if(result)
        *result = progress;

    if(!output || !input || !frustum_valid(frustum) ||
       ((uintptr_t)output & 31u) || ((uintptr_t)input & 31u) ||
       (attributes & ~PVR_FRUSTUM_CLIP_ALL)) {
        errno = EINVAL;
        return -1;
    }

    position_transform_init(&transform, &frustum->object_to_screen);

    for(i = 0; i < 3; ++i) {
        if(input[i].flags != PVR_CMD_VERTEX &&
           input[i].flags != PVR_CMD_VERTEX_EOL) {
            errno = EILSEQ;
            return -1;
        }

        if(!isfinite(input[i].x) || !isfinite(input[i].y) ||
           !isfinite(input[i].z) ||
           ((attributes & PVR_FRUSTUM_CLIP_UV) &&
            (!isfinite(input[i].u) || !isfinite(input[i].v)))) {
            errno = EDOM;
            return -1;
        }

        if(!transform_position(&transform, input[i].x, input[i].y, input[i].z,
                               &polygons[current][i])) {
            errno = ERANGE;
            return -1;
        }
        polygons[current][i].u = input[i].u;
        polygons[current][i].v = input[i].v;
        polygons[current][i].argb = input[i].argb;
        polygons[current][i].oargb = input[i].oargb;
    }

    for(plane = 0; plane < FRUSTUM_PLANES && count; ++plane) {
        size_t clipped_count = 0;

        if(clip_plane(frustum, plane, polygons[current], count,
                      polygons[current ^ 1u], &clipped_count, attributes) < 0) {
            errno = ERANGE;
            return -1;
        }

        current ^= 1u;
        count = clipped_count;
    }

    progress.polygon_vertices = count;
    output_count = count >= 3u ? (count - 2u) * 3u : 0u;
    progress.output_vertices = output_count;

    if(output_count > output_capacity) {
        if(result)
            *result = progress;
        errno = ENOSPC;
        return -1;
    }

    for(triangle = 0; triangle + 2u < count; ++triangle) {
        const size_t indices[3] = { 0, triangle + 1u, triangle + 2u };
        size_t vertex;

        for(vertex = 0; vertex < 3; ++vertex) {
            const clip_vertex_t *source =
                &polygons[current][indices[vertex]];
            pvr_vertex_t *destination = staged + triangle * 3u + vertex;
            float reciprocal_w;

            if(source->w <= FLT_MIN) {
                errno = ERANGE;
                return -1;
            }

#ifdef __DREAMCAST__
            reciprocal_w = shz_invf(source->w);
#else
            reciprocal_w = 1.0f / source->w;
#endif
            destination->flags = vertex == 2u ? PVR_CMD_VERTEX_EOL :
                                                PVR_CMD_VERTEX;
            destination->x = source->x * reciprocal_w;
            destination->y = source->y * reciprocal_w;
            destination->z = reciprocal_w;
            destination->u = source->u;
            destination->v = source->v;
            destination->argb = source->argb;
            destination->oargb = source->oargb;

            if(!isfinite(destination->x) || !isfinite(destination->y) ||
               !isfinite(destination->z) ||
               ((attributes & PVR_FRUSTUM_CLIP_UV) &&
                (!isfinite(destination->u) || !isfinite(destination->v)))) {
                errno = ERANGE;
                return -1;
            }
        }
    }

    if(output_count)
        memmove(output, staged, output_count * sizeof(*output));

    if(result)
        *result = progress;
    return 0;
}
