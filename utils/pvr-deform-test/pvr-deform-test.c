/* KallistiOS ##version##

   Host-side PVR deformation contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_deform.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00002f;
}

static void identity(matrix_t *matrix) {
    const matrix_t value = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    memcpy(matrix, &value, sizeof(value));
}

static void normal_identity(pvr_normal_matrix_t *matrix) {
    memset(matrix, 0, sizeof(*matrix));
    matrix->column[0][0] = 1.0f;
    matrix->column[1][1] = 1.0f;
    matrix->column[2][2] = 1.0f;
}

static void test_morph(void) {
    pvr_deform_vertex_t base[2] = {
        { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } },
        { { 2.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } }
    };
    pvr_morph_delta_t deltas[2] = {
        { { 2.0f, 4.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { { 4.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } }
    };
    pvr_morph_target_t target = { deltas, sizeof(deltas[0]), 0.5f };
    pvr_deform_stream_t stream = { base, 2, sizeof(base[0]) };
    pvr_deform_vertex_t output[2];
    pvr_deform_vertex_t in_place[2];
    pvr_deform_result_t result;

    assert(pvr_morph_apply(output, 2, &stream, &target, 1, &result) == 0);
    assert(result.deformed_vertices == 2);
    assert(output[0].position.x == 2.0f && output[0].position.y == 2.0f);
    assert(close_enough(output[0].normal.y, 0.44721360f));
    assert(close_enough(output[0].normal.z, 0.89442719f));
    assert(output[0].position.w == 1.0f && output[0].normal.w == 0.0f);

    memcpy(in_place, base, sizeof(in_place));
    stream.vertices = in_place;
    assert(pvr_morph_apply(in_place, 2, &stream, &target, 1, &result) == 0);
    assert(close_enough(in_place[0].position.x, 2.0f));
    assert(close_enough(in_place[0].normal.y, 0.44721360f));
    stream.vertices = base;

    deltas[1].position.x = NAN;
    output[1].position.x = 123.0f;
    errno = 0;
    assert(pvr_morph_apply(output, 2, &stream, &target, 1, &result) == -1);
    assert(errno == EDOM && result.deformed_vertices == 1);
    assert(output[1].position.x == 123.0f);
}

static void test_skin(void) {
    pvr_deform_vertex_t source = {
        { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 0.0f }
    };
    pvr_skin_influences_t influence = {
        { 0, 1, UINT16_MAX, UINT16_MAX }, { 1.0f, 3.0f, 0.0f, 0.0f }
    };
    alignas(8) matrix_t positions[2];
    pvr_normal_matrix_t normals[2];
    pvr_skin_palette_t palette = { positions, normals, 2 };
    pvr_deform_stream_t vertices = { &source, 1, sizeof(source) };
    pvr_skin_stream_t influences = { &influence, 1, sizeof(influence) };
    pvr_deform_vertex_t output;
    pvr_deform_vertex_t in_place;
    pvr_deform_vertex_t unchanged;
    pvr_deform_result_t result;
    matrix_t first_position;
    alignas(8) unsigned char unaligned_normals[
        sizeof(pvr_normal_matrix_t) + 4u];

    identity(positions + 0);
    identity(positions + 1);
    positions[1][3][0] = 10.0f;
    normal_identity(normals + 0);
    normal_identity(normals + 1);

    assert(pvr_skin_apply(&output, 1, &vertices, &influences,
                          &palette, &result) == 0);
    assert(result.deformed_vertices == 1);
    assert(close_enough(output.position.x, 7.5f));
    assert(close_enough(output.normal.z, 1.0f));

    in_place = source;
    vertices.vertices = &in_place;
    assert(pvr_skin_apply(&in_place, 1, &vertices, &influences,
                          &palette, &result) == 0);
    assert(close_enough(in_place.position.x, 7.5f));
    assert(close_enough(in_place.normal.z, 1.0f));
    vertices.vertices = &source;

    memset(&output, 0x5a, sizeof(output));
    memcpy(&unchanged, &output, sizeof(output));
    influence.joint[1] = 2;
    errno = 0;
    assert(pvr_skin_apply(&output, 1, &vertices, &influences,
                          &palette, &result) == -1);
    assert(errno == EILSEQ && result.deformed_vertices == 0);
    assert(memcmp(&output, &unchanged, sizeof(output)) == 0);

    influence.joint[1] = 1;
    palette.normal_matrices = (const pvr_normal_matrix_t *)(const void *)
        (unaligned_normals + 2);
    errno = 0;
    assert(pvr_skin_apply(&output, 1, &vertices, &influences,
                          &palette, &result) == -1);
    assert(errno == EINVAL && result.deformed_vertices == 0);

    palette.normal_matrices = normals;
    memcpy(&first_position, positions, sizeof(first_position));
    errno = 0;
    assert(pvr_skin_apply((pvr_deform_vertex_t *)(void *)positions, 1,
                          &vertices, &influences, &palette, &result) == -1);
    assert(errno == EINVAL && result.deformed_vertices == 0);
    assert(memcmp(&first_position, positions, sizeof(first_position)) == 0);

    influence.weight[1] = NAN;
    errno = 0;
    assert(pvr_skin_apply(&output, 1, &vertices, &influences,
                          &palette, &result) == -1);
    assert(errno == EILSEQ && result.deformed_vertices == 0);
}

int main(void) {
    test_morph();
    test_skin();
    puts("pvr deformation tests: PASS");
    return 0;
}
