/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_skeleton_affine.h>
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define COUNT 3u
static matrix_t world[COUNT], reference[COUNT];
static pvr_normal_matrix_t reference_normal[COUNT];
static pvr_chunk_skeleton_joint_t joints[COUNT];
static pvr_chunk_skeleton_t skeleton = {joints, COUNT, COUNT};
static pvr_chunk_skeleton_affine_joint_t packed_joints[COUNT];
static shz_mat3x4_t packed_world[COUNT];
static pvr_chunk_skeleton_affine_t affine;
static pvr_chunk_skeleton_affine_pose_t pose;
static pvr_skin_prepared_joint_t output[COUNT], before[COUNT];
static pvr_skin_prepared_palette_t palette;
static shz_mat4x4_t sentinel;

static int close_float(float a, float b) {
    return fabsf(a - b) <= 0.0002f * fmaxf(1.0f, fabsf(b));
}

static void xmtrx_seed(void) {
    for(size_t i = 0; i < 16; ++i)
        sentinel.elem[i] = (float)i * 0.25f - 2.0f;
    shz_xmtrx_load_4x4(&sentinel);
}

static void xmtrx_check(void) {
    shz_mat4x4_t actual;
    shz_xmtrx_store_4x4(&actual);
    assert(memcmp(&actual, &sentinel, sizeof(actual)) == 0);
}

static void init(unsigned frame) {
    for(size_t i = 0; i < COUNT; ++i) {
        memset(world + i, 0, sizeof(*world));
        memset(&joints[i].inverse_bind, 0, sizeof(matrix_t));
        for(size_t j = 0; j < 4; ++j) {
            world[i][j][j] = 1.0f;
            joints[i].inverse_bind[j][j] = 1.0f;
        }
        /* Nonsymmetric, noncommuting matrices: negative/nonuniform scale,
           shear, translation, and node order different from joint order. */
        world[i][0][0] = -(1.0f + (float)i * 0.25f);
        world[i][1][1] = 2.0f + (float)frame * 0.015625f;
        world[i][2][2] = 0.75f;
        world[i][1][0] = 0.3125f;
        world[i][2][1] = -0.1875f;
        world[i][3][0] = (float)frame * 0.25f;
        world[i][3][1] = (float)i - 2.5f;
        world[i][3][2] = 3.0f;
        joints[i].node_index = (i + 2u) % COUNT;
        joints[i].inverse_bind[0][1] = -0.25f;
        joints[i].inverse_bind[3][0] = 1.5f + (float)i;
        joints[i].inverse_bind[3][2] = -0.5f;
    }
}

static void prepare(void) {
    assert(pvr_chunk_skeleton_affine_prepare(&skeleton, packed_joints, COUNT,
                                            &affine) == 0);
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT, packed_world,
                                                 COUNT, &pose) == 0);
    xmtrx_check();
}

static void compare(void) {
    pvr_skin_palette_t old;
    assert(pvr_chunk_skeleton_palette_build(&skeleton, world, COUNT,
        reference, COUNT, reference_normal, COUNT, &old) == 0);
    xmtrx_check();
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT, &palette) == 0);
    xmtrx_check();
    assert(palette.joints == output && palette.joint_count == COUNT);
    for(size_t i = 0; i < COUNT; ++i) {
        for(size_t c = 0; c < 4; ++c) {
            for(size_t r = 0; r < 4; ++r) {
                float scalar = 0.0f;
                for(size_t k = 0; k < 4; ++k)
                    scalar += world[joints[i].node_index][k][r] *
                              joints[i].inverse_bind[c][k];
                assert(close_float(output[i].position.elem2D[c][r], scalar));
                assert(close_float(output[i].position.elem2D[c][r],
                                   reference[i][c][r]));
            }
        }
        for(size_t c = 0; c < 3; ++c)
            for(size_t r = 0; r < 3; ++r)
                assert(close_float(output[i].normal.elem2D[c][r],
                                   reference_normal[i].column[c][r]));
    }

    /* The producer must interoperate with the existing prepared consumer. */
    pvr_deform_vertex_t vertex = {
        .position = {0.5f, 1.0f, -0.25f, 1.0f},
        .normal = {0.0f, 0.0f, 1.0f, 0.0f}
    };
    pvr_deform_vertex_t new_vertex, old_vertex;
    pvr_skin_influences_t weights = {{0, 1, 2, 0}, {0.25f, 0.5f, 0.25f, 0}};
    pvr_deform_stream_t vertices = {&vertex, 1, sizeof(vertex)};
    pvr_skin_stream_t influences = {&weights, 1, sizeof(weights)};
    assert(pvr_skin_apply(&old_vertex, 1, &vertices, &influences, &old, NULL) == 0);
    assert(pvr_skin_apply_prepared_palette(&new_vertex, 1, &vertices,
                                           &influences, &palette, NULL) == 0);
    assert(close_float(new_vertex.position.x, old_vertex.position.x));
    assert(close_float(new_vertex.position.y, old_vertex.position.y));
    assert(close_float(new_vertex.position.z, old_vertex.position.z));
    assert(close_float(new_vertex.normal.x, old_vertex.normal.x));
    assert(close_float(new_vertex.normal.y, old_vertex.normal.y));
    assert(close_float(new_vertex.normal.z, old_vertex.normal.z));
    xmtrx_check();
}

static void test_admission(void) {
    pvr_chunk_skeleton_affine_joint_t saved_joints[COUNT];
    shz_mat3x4_t saved_world[COUNT];
    pvr_chunk_skeleton_affine_t saved_affine;
    pvr_chunk_skeleton_affine_pose_t saved_pose;
    init(0);
    prepare();
    memcpy(saved_joints, packed_joints, sizeof(saved_joints));
    memcpy(saved_world, packed_world, sizeof(saved_world));
    saved_affine = affine;
    saved_pose = pose;
    joints[2].inverse_bind[1][3] = 0.25f;
    errno = 0;
    assert(pvr_chunk_skeleton_affine_prepare(&skeleton, packed_joints, COUNT,
                                            &affine) == -1 && errno == EDOM);
    assert(memcmp(packed_joints, saved_joints, sizeof(saved_joints)) == 0);
    assert(memcmp(&affine, &saved_affine, sizeof(affine)) == 0);
    joints[2].inverse_bind[1][3] = 0;
    joints[2].node_index = COUNT;
    assert(pvr_chunk_skeleton_affine_prepare(&skeleton, packed_joints, COUNT,
                                            &affine) == -1 && errno == EDOM);
    world[2][3][3] = 2.0f;
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT, packed_world,
                                                 COUNT, &pose) == -1);
    assert(memcmp(packed_world, saved_world, sizeof(saved_world)) == 0);
    assert(memcmp(&pose, &saved_pose, sizeof(pose)) == 0);
    world[2][3][3] = 1.0f;
    world[2][2][0] = NAN;
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT, packed_world,
                                                 COUNT, &pose) == -1);
    assert(memcmp(packed_world, saved_world, sizeof(saved_world)) == 0);
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT, packed_world,
                                                 COUNT - 1, &pose) == -1 &&
           errno == ENOSPC);
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT,
        (shz_mat3x4_t *)world, COUNT, &pose) == -1 && errno == EINVAL);
    assert(pvr_chunk_skeleton_pose_prepare_affine(world,
        SIZE_MAX / sizeof(matrix_t) + 1, packed_world, SIZE_MAX, &pose) == -1 &&
        errno == EOVERFLOW);
    assert(pvr_chunk_skeleton_affine_prepare(NULL, packed_joints, COUNT,
                                            &affine) == -1 && errno == EINVAL);
    xmtrx_check();
}

static void test_failure_atomicity(void) {
    pvr_skin_prepared_palette_t saved_palette;
    init(0);
    prepare();
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT, &palette) == 0);
    memcpy(before, output, sizeof(before));
    saved_palette = palette;
    /* Joint 2 uses node 1: reject a late singular result before any publish. */
    world[1][0][0] = 0.0f;
    assert(pvr_chunk_skeleton_pose_prepare_affine(world, COUNT, packed_world,
                                                 COUNT, &pose) == 0);
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT, &palette) == -1 &&
           errno == ERANGE);
    assert(memcmp(output, before, sizeof(before)) == 0);
    assert(memcmp(&palette, &saved_palette, sizeof(palette)) == 0);
    xmtrx_check();

    init(0);
    world[1][0][0] = FLT_MAX;
    joints[2].inverse_bind[0][0] = 4.0f;
    prepare();
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT, &palette) == -1);
    assert(memcmp(output, before, sizeof(before)) == 0);
    assert(memcmp(&palette, &saved_palette, sizeof(palette)) == 0);
    xmtrx_check();

    init(0);
    prepare();
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT - 1, &palette) == -1 &&
           errno == ENOSPC);
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
        COUNT, (pvr_skin_prepared_palette_t *)output) == -1 && errno == EINVAL);
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose,
        (pvr_skin_prepared_joint_t *)packed_world, COUNT, &palette) == -1 &&
        errno == EINVAL);
    assert(memcmp(output, before, sizeof(before)) == 0);
    xmtrx_check();
}

static void test_snapshot(void) {
    init(5);
    prepare();
    compare();
    memcpy(before, output, sizeof(before));
    /* Snapshots own their copies; changing source data cannot affect them. */
    memset(world, 0, sizeof(world));
    memset(joints, 0, sizeof(joints));
    assert(pvr_chunk_skeleton_palette_build_affine(&affine, &pose, output,
                                                   COUNT, &palette) == 0);
    for(size_t i = 0; i < COUNT; ++i) {
        assert(memcmp(&output[i].position, &before[i].position,
                      sizeof(output[i].position)) == 0);
        assert(memcmp(&output[i].normal, &before[i].normal,
                      sizeof(output[i].normal)) == 0);
    }
    xmtrx_check();
}

int main(void) {
    xmtrx_seed();
    for(unsigned frame = 0; frame < 24; ++frame) {
        init(frame);
        prepare();
        compare();
    }
    test_admission();
    test_failure_atomicity();
    test_snapshot();
    printf("affine matrix bytes=%u legacy=%u joint bytes=%u legacy=%u\n",
           (unsigned)sizeof(shz_mat3x4_t), (unsigned)sizeof(matrix_t),
           (unsigned)sizeof(pvr_chunk_skeleton_affine_joint_t),
           (unsigned)sizeof(pvr_chunk_skeleton_joint_t));
    puts("RESULT: PASS (affine skeleton, normal matrices, skinning, XMTRX)");
    return 0;
}
