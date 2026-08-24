/* KallistiOS ##version##

   matrix_camera.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix3d.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct vec3 {
    float x;
    float y;
    float z;
} vec3_t;

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

static int vec3_normalize(vec3_t *vector) {
    float length_squared = vector->x * vector->x +
                           vector->y * vector->y +
                           vector->z * vector->z;
#ifndef __DREAMCAST__
    float reciprocal;
#endif

    if(!isfinite(length_squared)) {
        errno = ERANGE;
        return -1;
    }

    if(length_squared <= FLT_MIN) {
        errno = EDOM;
        return -1;
    }

#ifdef __DREAMCAST__
    {
        shz_vec3_t normalized = shz_vec3_normalize(
            shz_vec3_init(vector->x, vector->y, vector->z));

        if(!isfinite(normalized.x) || !isfinite(normalized.y) ||
           !isfinite(normalized.z)) {
            errno = ERANGE;
            return -1;
        }

        vector->x = normalized.x;
        vector->y = normalized.y;
        vector->z = normalized.z;
        return 0;
    }
#else
    reciprocal = 1.0f / sqrtf(length_squared);
    if(!isfinite(reciprocal)) {
        errno = ERANGE;
        return -1;
    }

    vector->x *= reciprocal;
    vector->y *= reciprocal;
    vector->z *= reciprocal;
    return 0;
#endif
}

static vec3_t vec3_cross(const vec3_t *lhs, const vec3_t *rhs) {
#ifdef __DREAMCAST__
    shz_vec3_t result = shz_vec3_cross(
        shz_vec3_init(lhs->x, lhs->y, lhs->z),
        shz_vec3_init(rhs->x, rhs->y, rhs->z));
    vec3_t output = { result.x, result.y, result.z };

    return output;
#else
    vec3_t result = {
        lhs->y * rhs->z - lhs->z * rhs->y,
        lhs->z * rhs->x - lhs->x * rhs->z,
        lhs->x * rhs->y - lhs->y * rhs->x
    };

    return result;
#endif
}

static float scalar_divide(float numerator, float denominator) {
#ifdef __DREAMCAST__
    return shz_divf(numerator, denominator);
#else
    return numerator / denominator;
#endif
}

int mat_perspective_build(matrix_t *out,
                          const mat_perspective_desc_t *desc) {
    matrix_t screen;
    matrix_t frustum;
    matrix_t result;
    float denominator;

    if(!out || !desc || !matrix_aligned(out)) {
        errno = EINVAL;
        return -1;
    }

    if(!isfinite(desc->x_center) || !isfinite(desc->y_center) ||
       !isfinite(desc->cot_half_fov) || !isfinite(desc->z_near) ||
       !isfinite(desc->z_far) || desc->y_center <= 0.0f ||
       desc->cot_half_fov <= 0.0f ||
       desc->z_near <= 0.0f || desc->z_far <= desc->z_near) {
        errno = EDOM;
        return -1;
    }

    denominator = desc->z_near - desc->z_far;
    screen[0][0] = desc->y_center;
    screen[0][1] = 0.0f;
    screen[0][2] = 0.0f;
    screen[0][3] = 0.0f;
    screen[1][0] = 0.0f;
    screen[1][1] = desc->y_center;
    screen[1][2] = 0.0f;
    screen[1][3] = 0.0f;
    screen[2][0] = 0.0f;
    screen[2][1] = 0.0f;
    screen[2][2] = 1.0f;
    screen[2][3] = 0.0f;
    screen[3][0] = desc->x_center;
    screen[3][1] = desc->y_center;
    screen[3][2] = 0.0f;
    screen[3][3] = 1.0f;

    frustum[0][0] = desc->cot_half_fov;
    frustum[0][1] = 0.0f;
    frustum[0][2] = 0.0f;
    frustum[0][3] = 0.0f;
    frustum[1][0] = 0.0f;
    frustum[1][1] = desc->cot_half_fov;
    frustum[1][2] = 0.0f;
    frustum[1][3] = 0.0f;
    frustum[2][0] = 0.0f;
    frustum[2][1] = 0.0f;
    frustum[2][2] = scalar_divide(desc->z_far + desc->z_near,
                                  denominator);
    frustum[2][3] = -1.0f;
    frustum[3][0] = 0.0f;
    frustum[3][1] = 0.0f;
    frustum[3][2] = scalar_divide(2.0f * desc->z_far * desc->z_near,
                                  denominator);
    frustum[3][3] = 1.0f;

    if(!matrix_finite(&frustum) ||
       mat_compose(&result, &screen, &frustum) < 0 ||
       !matrix_finite(&result)) {
        errno = ERANGE;
        return -1;
    }

    memcpy(out, &result, sizeof(result));
    return 0;
}

int mat_perspective_apply(const mat_perspective_desc_t *desc) {
    matrix_t matrix;

    if(mat_perspective_build(&matrix, desc) < 0)
        return -1;

    mat_apply(&matrix);
    return 0;
}

int mat_lookat_build(matrix_t *out, const mat_lookat_desc_t *desc) {
    matrix_t orientation = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    matrix_t translation = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    matrix_t result;
    vec3_t forward;
    vec3_t side;
    vec3_t up;

    if(!out || !desc || !matrix_aligned(out)) {
        errno = EINVAL;
        return -1;
    }

    if(!isfinite(desc->eye.x) || !isfinite(desc->eye.y) ||
       !isfinite(desc->eye.z) || !isfinite(desc->center.x) ||
       !isfinite(desc->center.y) || !isfinite(desc->center.z) ||
       !isfinite(desc->up.x) || !isfinite(desc->up.y) ||
       !isfinite(desc->up.z)) {
        errno = EDOM;
        return -1;
    }

    forward.x = desc->center.x - desc->eye.x;
    forward.y = desc->center.y - desc->eye.y;
    forward.z = desc->center.z - desc->eye.z;
    up.x = desc->up.x;
    up.y = desc->up.y;
    up.z = desc->up.z;

    if(vec3_normalize(&forward) < 0 || vec3_normalize(&up) < 0)
        return -1;

    side = vec3_cross(&forward, &up);
    if(vec3_normalize(&side) < 0)
        return -1;

    up = vec3_cross(&side, &forward);
    if(!isfinite(up.x) || !isfinite(up.y) || !isfinite(up.z)) {
        errno = ERANGE;
        return -1;
    }

    orientation[0][0] = side.x;
    orientation[1][0] = side.y;
    orientation[2][0] = side.z;
    orientation[0][1] = up.x;
    orientation[1][1] = up.y;
    orientation[2][1] = up.z;
    orientation[0][2] = -forward.x;
    orientation[1][2] = -forward.y;
    orientation[2][2] = -forward.z;

    translation[3][0] = -desc->eye.x;
    translation[3][1] = -desc->eye.y;
    translation[3][2] = -desc->eye.z;

    if(mat_compose(&result, &orientation, &translation) < 0 ||
       !matrix_finite(&result)) {
        errno = ERANGE;
        return -1;
    }

    memcpy(out, &result, sizeof(result));
    return 0;
}

int mat_lookat_apply(const mat_lookat_desc_t *desc) {
    matrix_t matrix;

    if(mat_lookat_build(&matrix, desc) < 0)
        return -1;

    mat_apply(&matrix);
    return 0;
}
