/* KallistiOS ##version##

   Shared host/SH-4 checked-matrix fixtures. Expected values deliberately do
   not use SH4ZAM or the KOS matrix builders under test.
   Copyright (C) 2026 Joseph Black
*/

#ifndef ANIMATION_MATRIX_FIXTURES_H
#define ANIMATION_MATRIX_FIXTURES_H

#include <dc/animation.h>
#include <dc/matrix.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

static bool fixture_matrix_close(const matrix_t *actual,
                                  const double expected[4][4],
                                  double tolerance) {
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 4; ++r)
            if(!isfinite((*actual)[c][r]) ||
               fabs((*actual)[c][r] - expected[c][r]) >
               tolerance * fmax(1.0, fabs(expected[c][r])))
                return false;
    return true;
}

static void fixture_cross(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void fixture_normalize(double v[3]) {
    double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for(unsigned i = 0; i < 3; ++i)
        v[i] /= length;
}

/* Independent double-precision Rodrigues + right-handed look-at reference. */
static void fixture_camera_reference(const anim_camera_pose_t *pose,
                                      double out[4][4]) {
    double eye[3] = { pose->eye.x, pose->eye.y, pose->eye.z };
    double forward[3] = { pose->target.x - eye[0], pose->target.y - eye[1],
                          pose->target.z - eye[2] };
    double up[3] = { pose->up.x, pose->up.y, pose->up.z };
    double cross[3], rolled[3], side[3];
    double sine = sin((double)pose->roll), cosine = cos((double)pose->roll);
    double dot = 0.0;

    fixture_normalize(forward);
    fixture_cross(forward, up, cross);
    for(unsigned i = 0; i < 3; ++i)
        dot += forward[i] * up[i];
    for(unsigned i = 0; i < 3; ++i)
        rolled[i] = up[i] * cosine + cross[i] * sine +
                    forward[i] * dot * (1.0 - cosine);
    fixture_normalize(rolled);
    fixture_cross(forward, rolled, side);
    fixture_normalize(side);
    fixture_cross(side, forward, up);
    memset(out, 0, sizeof(double) * 16);
    for(unsigned i = 0; i < 3; ++i) {
        out[i][0] = side[i];
        out[i][1] = up[i];
        out[i][2] = -forward[i];
        out[3][0] -= eye[i] * side[i];
        out[3][1] -= eye[i] * up[i];
        out[3][2] += eye[i] * forward[i];
    }
    out[3][3] = 1.0;
}

/* Optional target callback checks XMTRX immediately after each API call. */
static const char *verify_animation_matrices(bool (*state_unchanged)(void),
                                            double tolerance) {
#define MATRIX_CHECK(condition, message) do { \
    if(!(condition)) return message; \
    if(state_unchanged && !state_unchanged()) return "matrix XMTRX changed"; \
} while(0)
    static const anim_quaternion_t rotations[] = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f }
    };
    /* Column-major: identity, +90 degrees about Z, cyclic X->Y->Z->X. */
    static const double bases[3][3][3] = {
        { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        { { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } },
        { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } }
    };
    static const float scales[3][3] = {
        { 2, 3, 4 }, { -2, 3, -4 }, { 0, -3, 4 }
    };
    anim_transform_t transform = {
        .translation = { 4, -5, 6, 1 },
        .rotation = { 1, 0, 0, 0 },
        .scale = { 1, 1, 1, 0 }
    };
    anim_camera_pose_t camera = {
        .eye = { 1, 2, 3, 1 }, .target = { 4, 6, -2, 1 },
        .up = { 0.25f, 2, 0.5f, 0 }, .vertical_fov = 1.0f
    };
    static const float rolls[] = { 0, 0.37f, -0.83f, 1.57079632679f };
    matrix_t actual, unchanged;
    double expected[4][4];

    for(unsigned q = 0; q < 3; ++q) {
        transform.rotation = rotations[q];
        for(unsigned s = 0; s < 3; ++s) {
            transform.scale.x = scales[s][0];
            transform.scale.y = scales[s][1];
            transform.scale.z = scales[s][2];
            memset(expected, 0, sizeof(expected));
            for(unsigned c = 0; c < 3; ++c)
                for(unsigned r = 0; r < 3; ++r)
                    expected[c][r] = bases[q][c][r] * scales[s][c];
            expected[3][0] = 4; expected[3][1] = -5;
            expected[3][2] = 6; expected[3][3] = 1;
            MATRIX_CHECK(anim_transform_matrix_build(&transform, &actual) == 0,
                         "TRS construction");
            MATRIX_CHECK(fixture_matrix_close(&actual, expected, tolerance),
                         "TRS rotation/scale order");
        }
    }
    memcpy(&unchanged, &actual, sizeof(actual));
    transform.rotation.w = NAN;
    errno = 0;
    MATRIX_CHECK(anim_transform_matrix_build(&transform, &actual) == -1 &&
                 errno == EINVAL && !memcmp(&actual, &unchanged, sizeof(actual)),
                 "TRS failure publication");
    for(unsigned i = 0; i < sizeof(rolls) / sizeof(rolls[0]); ++i) {
        camera.roll = rolls[i];
        fixture_camera_reference(&camera, expected);
        MATRIX_CHECK(anim_camera_view_matrix_build(&camera, &actual) == 0,
                     "rolled camera construction");
        MATRIX_CHECK(fixture_matrix_close(&actual, expected, tolerance),
                     "rolled camera reference");
    }
    memcpy(&unchanged, &actual, sizeof(actual));
    camera.target = camera.eye;
    errno = 0;
    MATRIX_CHECK(anim_camera_view_matrix_build(&camera, &actual) == -1 &&
                 errno == EINVAL && !memcmp(&actual, &unchanged, sizeof(actual)),
                 "camera failure publication");

    /* Noncommuting, nonsymmetric operands; exercise all output alias cases. */
    {
        const matrix_t lhs = {
            { 1, 2, 3, 0 }, { 4, 5, 6, 0 },
            { 7, 8, 10, 0 }, { 11, 12, 13, 1 }
        };
        const matrix_t rhs = {
            { 2, 0, 0, 0 }, { 0, -3, 0, 0 },
            { 0, 0, 4, 0 }, { 5, 6, 7, 1 }
        };
        matrix_t left, right;
        for(unsigned alias = 0; alias < 4; ++alias) {
            memcpy(&left, &lhs, sizeof(left));
            memcpy(&right, alias == 3 ? &lhs : &rhs, sizeof(right));
            for(unsigned c = 0; c < 4; ++c)
                for(unsigned r = 0; r < 4; ++r) {
                    expected[c][r] = 0;
                    for(unsigned k = 0; k < 4; ++k)
                        expected[c][r] += (double)left[k][r] * right[c][k];
                }
            matrix_t *out = alias == 0 ? &actual : alias == 2 ? &right : &left;
            MATRIX_CHECK(mat_compose(out, &left, alias == 3 ? &left : &right) == 0,
                         "matrix compose alias");
            MATRIX_CHECK(fixture_matrix_close(out, expected, tolerance),
                         "matrix compose reference");
        }
        memcpy(&unchanged, &actual, sizeof(actual));
        errno = 0;
        MATRIX_CHECK(mat_compose(&actual, NULL, &rhs) == -1 && errno == EINVAL &&
                     !memcmp(&actual, &unchanged, sizeof(actual)),
                     "matrix compose failure publication");
    }
#undef MATRIX_CHECK
    return NULL;
}

#endif
