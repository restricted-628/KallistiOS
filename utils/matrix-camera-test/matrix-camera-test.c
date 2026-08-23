/* KallistiOS ##version##

   Host-side explicit camera matrix tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix3d.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static alignas(32) matrix_t current_matrix;

static const matrix_t identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static int close_enough(float actual, float expected) {
    float difference = fabsf(actual - expected);
    float scale = fmaxf(1.0f, fmaxf(fabsf(actual), fabsf(expected)));

    return isfinite(difference) && isfinite(scale) &&
           difference <= 0.00001f * scale;
}

static void expect_matrix(const matrix_t *actual, const matrix_t *expected) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row)
            assert(close_enough((*actual)[column][row],
                                (*expected)[column][row]));
    }
}

static void fill_matrix(matrix_t *matrix, float value) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row)
            (*matrix)[column][row] = value;
    }
}

void mat_apply(const matrix_t *src) {
    matrix_t result;

    assert(mat_compose(&result, &current_matrix, src) == 0);
    memcpy(&current_matrix, &result, sizeof(current_matrix));
}

static void test_compose(void) {
    alignas(32) matrix_t translation = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 5.0f, 6.0f, 7.0f, 1.0f }
    };
    alignas(32) matrix_t scale = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    alignas(32) const matrix_t expected = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f },
        { 5.0f, 6.0f, 7.0f, 1.0f }
    };
    alignas(32) matrix_t result;
    alignas(8) unsigned char unaligned[sizeof(matrix_t) + 1u];

    assert(mat_compose(&result, &translation, &scale) == 0);
    expect_matrix(&result, &expected);

    memcpy(&result, &translation, sizeof(result));
    assert(mat_compose(&result, &result, &scale) == 0);
    expect_matrix(&result, &expected);

    memcpy(&result, &scale, sizeof(result));
    assert(mat_compose(&result, &translation, &result) == 0);
    expect_matrix(&result, &expected);

    errno = 0;
    assert(mat_compose(NULL, &translation, &scale) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_compose((matrix_t *)(void *)(unaligned + 1),
                       &translation, &scale) == -1 && errno == EINVAL);
}

static void test_perspective(void) {
    mat_perspective_desc_t desc = {
        .x_center = 320.0f,
        .y_center = 240.0f,
        .cot_half_fov = 1.0f,
        .z_near = 1.0f,
        .z_far = 100.0f
    };
    alignas(32) const matrix_t expected = {
        { 240.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 240.0f, 0.0f, 0.0f },
        { -320.0f, -240.0f, -1.02020202f, -1.0f },
        { 320.0f, 240.0f, -2.02020202f, 1.0f }
    };
    alignas(32) matrix_t result;
    alignas(32) matrix_t unchanged;

    assert(mat_perspective_build(&result, &desc) == 0);
    expect_matrix(&result, &expected);

    memcpy(&current_matrix, &identity, sizeof(current_matrix));
    assert(mat_perspective_apply(&desc) == 0);
    expect_matrix(&current_matrix, &expected);

    fill_matrix(&unchanged, 19.0f);
    memcpy(&result, &unchanged, sizeof(result));
    desc.z_far = desc.z_near;
    errno = 0;
    assert(mat_perspective_build(&result, &desc) == -1 && errno == EDOM);
    assert(memcmp(&result, &unchanged, sizeof(result)) == 0);

    desc.z_far = 100.0f;
    desc.y_center = 0.0f;
    errno = 0;
    assert(mat_perspective_build(&result, &desc) == -1 && errno == EDOM);
    desc.y_center = 240.0f;
    desc.cot_half_fov = NAN;
    errno = 0;
    assert(mat_perspective_build(&result, &desc) == -1 && errno == EDOM);
    desc.cot_half_fov = FLT_MAX;
    desc.y_center = FLT_MAX;
    errno = 0;
    assert(mat_perspective_build(&result, &desc) == -1 && errno == ERANGE);
    assert(memcmp(&result, &unchanged, sizeof(result)) == 0);

    memcpy(&current_matrix, &identity, sizeof(current_matrix));
    errno = 0;
    assert(mat_perspective_apply(&desc) == -1 && errno == ERANGE);
    expect_matrix(&current_matrix, &identity);

    errno = 0;
    assert(mat_perspective_build(NULL, &desc) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_perspective_build(&result, NULL) == -1 && errno == EINVAL);
}

static void test_lookat(void) {
    mat_lookat_desc_t desc = {
        .eye = { 1.0f, 2.0f, 3.0f, 1.0f },
        .center = { 1.0f, 2.0f, 2.0f, 1.0f },
        .up = { 0.0f, 1.0f, 0.0f, 0.0f }
    };
    alignas(32) const matrix_t expected = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { -1.0f, -2.0f, -3.0f, 1.0f }
    };
    alignas(32) matrix_t result;
    alignas(32) matrix_t unchanged;

    assert(mat_lookat_build(&result, &desc) == 0);
    expect_matrix(&result, &expected);

    memcpy(&current_matrix, &identity, sizeof(current_matrix));
    assert(mat_lookat_apply(&desc) == 0);
    expect_matrix(&current_matrix, &expected);

    fill_matrix(&unchanged, 23.0f);
    memcpy(&result, &unchanged, sizeof(result));
    desc.center = desc.eye;
    errno = 0;
    assert(mat_lookat_build(&result, &desc) == -1 && errno == EDOM);
    assert(memcmp(&result, &unchanged, sizeof(result)) == 0);

    desc.center.z = 2.0f;
    desc.up.x = 0.0f;
    desc.up.y = 0.0f;
    desc.up.z = -1.0f;
    errno = 0;
    assert(mat_lookat_build(&result, &desc) == -1 && errno == EDOM);

    desc.up.y = 1.0f;
    desc.up.z = 0.0f;
    desc.center.x = -FLT_MAX;
    desc.eye.x = FLT_MAX;
    errno = 0;
    assert(mat_lookat_build(&result, &desc) == -1 && errno == ERANGE);
    assert(memcmp(&result, &unchanged, sizeof(result)) == 0);

    memcpy(&current_matrix, &identity, sizeof(current_matrix));
    errno = 0;
    assert(mat_lookat_apply(&desc) == -1 && errno == ERANGE);
    expect_matrix(&current_matrix, &identity);

    errno = 0;
    assert(mat_lookat_build(NULL, &desc) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_lookat_build(&result, NULL) == -1 && errno == EINVAL);
}

int main(void) {
    test_compose();
    test_perspective();
    test_lookat();
    puts("camera matrix tests passed");
    return 0;
}
