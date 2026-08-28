/* KallistiOS ##version##

   Host-side PVR lighting contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_lighting.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00002f;
}

static void test_normal_matrix(void) {
    alignas(8) matrix_t transform = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f },
        { 7.0f, 8.0f, 9.0f, 1.0f }
    };
    pvr_normal_matrix_t normal;
    pvr_normal_matrix_t unchanged;

    assert(pvr_normal_matrix_build(&normal, &transform) == 0);
    assert(close_enough(normal.column[0][0], 0.5f));
    assert(close_enough(normal.column[1][1], 1.0f / 3.0f));
    assert(close_enough(normal.column[2][2], 0.25f));
    assert(normal.column[1][0] == 0.0f && normal.column[2][0] == 0.0f);

    /* A shear catches accidental inverse-without-transpose storage. The
       transformed normal (1,-2,0) remains perpendicular to A*(0,1,0). */
    transform[0][0] = 1.0f;
    transform[1][0] = 2.0f;
    transform[1][1] = 1.0f;
    transform[2][2] = 1.0f;
    assert(pvr_normal_matrix_build(&normal, &transform) == 0);
    assert(close_enough(normal.column[0][0], 1.0f));
    assert(close_enough(normal.column[0][1], -2.0f));
    assert(close_enough(normal.column[1][0], 0.0f));
    assert(close_enough(normal.column[1][1], 1.0f));

    memset(&normal, 0x5a, sizeof(normal));
    memcpy(&unchanged, &normal, sizeof(normal));
    transform[2][2] = 0.0f;
    errno = 0;
    assert(pvr_normal_matrix_build(&normal, &transform) == -1);
    assert(errno == ERANGE && memcmp(&normal, &unchanged, sizeof(normal)) == 0);
}

static void test_normal_transform(void) {
    alignas(8) matrix_t transform = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_normal_matrix_t matrix;
    vector_t input[3] = {
        { 1.0f, 1.0f, 0.0f, 4.0f },
        { 0.0f, 0.0f, 1.0f, 5.0f },
        { 0.0f, 0.0f, 0.0f, 6.0f }
    };
    vector_t output[3];
    vector_t unchanged;
    pvr_normal_stream_t stream = { input, 2, sizeof(vector_t) };
    pvr_normal_result_t result;

    assert(pvr_normal_matrix_build(&matrix, &transform) == 0);
    assert(pvr_normal_transform(output, 3, &stream, &matrix, &result) == 0);
    assert(result.transformed_normals == 2);
    assert(close_enough(output[0].x, 0.83205029f));
    assert(close_enough(output[0].y, 0.55470020f));
    assert(close_enough(output[0].z, 0.0f) && output[0].w == 0.0f);
    assert(close_enough(output[1].z, 1.0f) && output[1].w == 0.0f);

    /* Exact canonical in-place operation is supported. */
    stream.normal_count = 1;
    assert(pvr_normal_transform(input, 3, &stream, &matrix, NULL) == 0);
    assert(close_enough(input[0].x, 0.83205029f));

    stream.normals = input + 2;
    stream.normal_count = 1;
    memset(output, 0x5a, sizeof(output));
    memcpy(&unchanged, output, sizeof(unchanged));
    errno = 0;
    assert(pvr_normal_transform(output, 3, &stream, &matrix, &result) == -1);
    assert(errno == EDOM && result.transformed_normals == 0);
    assert(memcmp(output, &unchanged, sizeof(unchanged)) == 0);
}

static void test_color_pack(void) {
    uint32_t color = UINT32_C(0x5a5a5a5a);
    unsigned char unaligned[sizeof(uint32_t) + 1u];

    assert(pvr_color_pack_argb(&color, 1.0f, 0.5f, 0.0f, -1.0f) == 0);
    assert(color == UINT32_C(0xff800000));
    assert(pvr_color_pack_argb(&color, 2.0f, 2.0f, 0.25f, 1.0f) == 0);
    assert(color == UINT32_C(0xffff40ff));

    color = UINT32_C(0x5a5a5a5a);
    errno = 0;
    assert(pvr_color_pack_argb(&color, 1.0f, NAN, 0.0f, 0.0f) == -1);
    assert(errno == EDOM && color == UINT32_C(0x5a5a5a5a));

    errno = 0;
    assert(pvr_color_pack_argb(NULL, 1.0f, 1.0f, 1.0f, 1.0f) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_color_pack_argb((uint32_t *)(void *)(unaligned + 1),
                               1.0f, 1.0f, 1.0f, 1.0f) == -1);
    assert(errno == EINVAL);
}

static void test_environment_map(void) {
    vector_t normal = { 0.0f, 0.0f, 1.0f, 0.0f };
    float uv[2] = { -1.0f, -1.0f };
    float unchanged[2];

    assert(pvr_environment_map_uv(uv, &normal) == 0);
    assert(close_enough(uv[0], 0.5f) && close_enough(uv[1], 0.5f));

    normal.x = 1.0f;
    normal.z = 0.0f;
    assert(pvr_environment_map_uv(uv, &normal) == 0);
    assert(close_enough(uv[0], 1.0f) && close_enough(uv[1], 0.5f));

    normal.x = 0.0f;
    normal.y = 1.0f;
    assert(pvr_environment_map_uv(uv, &normal) == 0);
    assert(close_enough(uv[0], 0.5f) && close_enough(uv[1], 0.0f));

    memcpy(unchanged, uv, sizeof(uv));
    normal.y = 0.5f;
    errno = 0;
    assert(pvr_environment_map_uv(uv, &normal) == -1);
    assert(errno == EDOM && memcmp(uv, unchanged, sizeof(uv)) == 0);
}

static void initialize_lighting(pvr_light_t lights[2],
                                pvr_lighting_context_t *context) {
    memset(lights, 0, 2 * sizeof(*lights));
    lights[0].kind = PVR_LIGHT_DIRECTIONAL;
    lights[0].source.direction.z = 2.0f;
    lights[0].color.x = 1.0f;
    lights[0].color.y = 1.0f;
    lights[0].color.z = 1.0f;
    lights[0].intensity = 0.5f;

    lights[1].kind = PVR_LIGHT_POINT;
    lights[1].source.position.z = 2.0f;
    lights[1].color.x = 1.0f;
    lights[1].intensity = 1.0f;
    lights[1].attenuation_constant = 1.0f;
    lights[1].range = 3.0f;

    memset(context, 0, sizeof(*context));
    context->ambient[0] = 0.1f;
    context->ambient[1] = 0.1f;
    context->ambient[2] = 0.1f;
    context->lights = lights;
    context->light_count = 2;
}

static void test_lighting(void) {
    pvr_light_t lights[2];
    pvr_lighting_context_t context;
    pvr_lighting_sample_t samples[2] = {
        {
            .position = { 0.0f, 0.0f, 0.0f, 1.0f },
            .normal = { 0.0f, 0.0f, 1.0f, 0.0f },
            .color = { 1.0f, 0.5f, 0.25f, 0.75f }
        },
        {
            .position = { 0.0f, 0.0f, 0.0f, 1.0f },
            .normal = { 0.0f, 0.0f, -1.0f, 0.0f },
            .color = { 1.0f, 0.5f, 0.25f, 0.75f }
        }
    };
    pvr_lighting_stream_t stream = {
        samples, 2, sizeof(pvr_lighting_sample_t)
    };
    pvr_lighting_result_t result;
    uint32_t output[2];
    uint32_t unchanged[2];

    initialize_lighting(lights, &context);
    assert(pvr_lighting_apply(output, 2, &stream, &context, &result) == 0);
    assert(result.shaded_samples == 2);
    assert(output[0] == UINT32_C(0xbfff4d26));
    assert(output[1] == UINT32_C(0xbf1a0d06));

    /* A saturated light with a zero channel must not form 0 * infinity. */
    lights[1].intensity = FLT_MAX;
    assert(pvr_lighting_apply(output, 2, &stream, &context, &result) == 0);
    assert(output[0] == UINT32_C(0xbfff4d26));
    lights[1].intensity = 1.0f;

    /* A malformed sample reports a valid output prefix. */
    samples[1].normal.z = 0.5f;
    output[0] = UINT32_C(0xaaaaaaaa);
    output[1] = UINT32_C(0x5a5a5a5a);
    errno = 0;
    assert(pvr_lighting_apply(output, 2, &stream, &context, &result) == -1);
    assert(errno == EDOM && result.shaded_samples == 1);
    assert(output[0] == UINT32_C(0xbfff4d26));
    assert(output[1] == UINT32_C(0x5a5a5a5a));

    /* Context validation completes before any caller output is published. */
    samples[1].normal.z = -1.0f;
    memcpy(unchanged, output, sizeof(output));
    lights[0].color.x = NAN;
    errno = 0;
    assert(pvr_lighting_apply(output, 2, &stream, &context, &result) == -1);
    assert(errno == EINVAL && result.shaded_samples == 0);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
}

int main(void) {
    test_normal_matrix();
    test_normal_transform();
    test_environment_map();
    test_color_pack();
    test_lighting();
    puts("pvr lighting tests: PASS");
    return 0;
}
