/* KallistiOS ##version##

   matrix3d.c
   Copyright (C) 2000-2002 Megan Potter and Jordan DeLong
   Copyright (C) 2014 Josh Pearson
   Copyright (C) 2026 Joseph Black

   Some 3D utils to use with the matrix functions
   Based on example code by Marcus Comstedt
*/

#include <assert.h>
#include <stdalign.h>
#include <dc/fmath.h>
#include <dc/matrix.h>
#include <dc/matrix3d.h>
#include <dc/vec3f.h>

void mat_translate(float x, float y, float z) {
    alignas(32) matrix_t m = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { x,    y,    z,    1.0f }
    };

    mat_apply(&m);
}

void mat_scale(float xs, float ys, float zs) {
    alignas(32) matrix_t m = {
        { xs,   0.0f, 0.0f, 0.0f },
        { 0.0f, ys,   0.0f, 0.0f },
        { 0.0f, 0.0f, zs,   0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    mat_apply(&m);
}

void mat_rotate_x(float r) {
    alignas(32) matrix_t m = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    __fsincosr(r, m[2][1], m[1][1]);
    m[2][2] = m[1][1];
    m[1][2] = -m[2][1];
    mat_apply(&m);
}

void mat_rotate_y(float r) {
    alignas(32) matrix_t m = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    __fsincosr(r, m[0][2], m[0][0]);
    m[2][2] = m[0][0];
    m[2][0] = -m[0][2];
    mat_apply(&m);
}

void mat_rotate_z(float r) {
    alignas(32) matrix_t m = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    __fsincosr(r, m[1][0], m[0][0]);
    m[1][1] = m[0][0];
    m[0][1] = -m[1][0];
    mat_apply(&m);
}

void mat_rotate(float xr, float yr, float zr) {
    mat_rotate_x(xr);
    mat_rotate_y(yr);
    mat_rotate_z(zr);
}

void mat_perspective(float xcenter, float ycenter, float cot_fovy_2,
                     float znear, float zfar) {
    alignas(32) matrix_t screen = {
        { ycenter, 0.0f,    0.0f, 0.0f },
        { 0.0f,    ycenter, 0.0f, 0.0f },
        { 0.0f,    0.0f,    1.0f, 0.0f },
        { xcenter, ycenter, 0.0f, 1.0f }
    };
    alignas(32) matrix_t frustum = {
        { cot_fovy_2, 0.0f,       0.0f, 0.0f },
        { 0.0f,       cot_fovy_2, 0.0f, 0.0f },
        { 0.0f,       0.0f,       0.0f, -1.0f },
        { 0.0f,       0.0f,       0.0f, 1.0f }
    };

    assert((znear - zfar) != 0);
    frustum[2][2] = (zfar + znear) / (znear - zfar);
    frustum[3][2] = 2 * zfar * znear / (znear - zfar);

    mat_apply(&screen);
    mat_apply(&frustum);
}


/* The following lookat code is based heavily on KGL's gluLookAt */

static inline void cross(const vec3f_t *v1, const vec3f_t *v2, vec3f_t *r) {
    r->x = v1->y * v2->z - v1->z * v2->y;
    r->y = v1->z * v2->x - v1->x * v2->z;
    r->z = v1->x * v2->y - v1->y * v2->x;
}

void mat_lookat(const point_t *eye, const point_t *center,
                const vector_t *upi) {
    alignas(32) matrix_t m = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    vec3f_t forward, side, up;

    forward.x = center->x - eye->x;
    forward.y = center->y - eye->y;
    forward.z = center->z - eye->z;

    up.x = upi->x;
    up.y = upi->y;
    up.z = upi->z;

    vec3f_normalize(forward.x, forward.y, forward.z);

    /* Side = forward x up */
    cross(&forward, &up, &side);
    vec3f_normalize(side.x, side.y, side.z);

    /* Recompute up as: up = side x forward */
    cross(&side, &forward, &up);

    m[0][0] = side.x;
    m[1][0] = side.y;
    m[2][0] = side.z;

    m[0][1] = up.x;
    m[1][1] = up.y;
    m[2][1] = up.z;

    m[0][2] = -forward.x;
    m[1][2] = -forward.y;
    m[2][2] = -forward.z;

    mat_apply(&m);
    mat_translate(-eye->x, -eye->y, -eye->z);
}
