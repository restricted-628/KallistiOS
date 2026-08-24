/* KallistiOS ##version##

   Host-side animation contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/animation.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00003f;
}

static anim_track_view_t open_track(anim_value_kind_t kind,
                                    anim_interpolation_t interpolation,
                                    const void *keys, size_t count,
                                    size_t stride) {
    anim_track_t source = { kind, interpolation, keys, count, stride };
    anim_track_view_t view;

    assert(anim_track_open(&source, &view) == 0);
    return view;
}

static void test_scalar_tracks(void) {
    typedef struct extended_scalar_key {
        anim_scalar_key_t key;
        uint32_t tag;
    } extended_scalar_key_t;
    const anim_scalar_key_t keys[] = {
        { 0.0f, 0.0f }, { 1.0f, 10.0f }, { 3.0f, 30.0f }
    };
    const extended_scalar_key_t extended[] = {
        { { 0.0f, 2.0f }, UINT32_C(0x11111111) },
        { { 1.0f, 6.0f }, UINT32_C(0x22222222) }
    };
    anim_track_view_t linear = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        keys, 3, sizeof(keys[0]));
    anim_track_view_t step = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_STEP,
        keys, 3, sizeof(keys[0]));
    anim_track_view_t strided = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        extended, 2, sizeof(extended[0]));
    anim_sample_info_t info;
    float value;
    float unchanged;

    assert(linear.start_time == 0.0f && linear.end_time == 3.0f);
    assert(anim_track_sample_scalar(&linear, -2.0f, &value, &info) == 0);
    assert(value == 0.0f && info.lower_key == 0 && info.upper_key == 0);
    assert(anim_track_sample_scalar(&linear, 0.5f, &value, &info) == 0);
    assert(value == 5.0f && info.lower_key == 0 && info.upper_key == 1 &&
           info.factor == 0.5f);
    assert(anim_track_sample_scalar(&linear, 2.0f, &value, &info) == 0);
    assert(value == 20.0f && info.lower_key == 1 && info.upper_key == 2 &&
           info.factor == 0.5f);
    assert(anim_track_sample_scalar(&linear, 5.0f, &value, &info) == 0);
    assert(value == 30.0f && info.lower_key == 2 && info.upper_key == 2);

    assert(anim_track_sample_scalar(&step, 2.0f, &value, &info) == 0);
    assert(value == 10.0f && info.lower_key == 1 && info.upper_key == 1 &&
           info.factor == 0.0f);

    assert(anim_track_sample_scalar(&strided, 0.25f, &value, NULL) == 0);
    assert(value == 3.0f);

    /* A hand-forged view must fail before endpoint pointer arithmetic. */
    linear.track.key_count = SIZE_MAX;
    value = 123.0f;
    unchanged = value;
    errno = 0;
    assert(anim_track_sample_scalar(&linear, 0.5f, &value, NULL) == -1);
    assert(errno == EINVAL && value == unchanged);
}

static void test_track_rejection(void) {
    anim_scalar_key_t scalar[] = {
        { 0.0f, 1.0f }, { 0.0f, 2.0f }
    };
    anim_quaternion_key_t quaternion = {
        0.0f, { 0.0f, 0.0f, 0.0f, 0.0f }
    };
    anim_track_t source = {
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        scalar, 2, sizeof(scalar[0])
    };
    anim_track_view_t output;
    anim_track_view_t unchanged;

    memset(&output, 0x5a, sizeof(output));
    memcpy(&unchanged, &output, sizeof(output));
    errno = 0;
    assert(anim_track_open(&source, &output) == -1);
    assert(errno == EILSEQ && memcmp(&output, &unchanged, sizeof(output)) == 0);

    scalar[1].time = 1.0f;
    scalar[1].value = NAN;
    errno = 0;
    assert(anim_track_open(&source, &output) == -1 && errno == EDOM);

    source.kind = ANIM_VALUE_QUATERNION;
    source.keys = &quaternion;
    source.key_count = 1;
    source.stride = sizeof(quaternion);
    errno = 0;
    assert(anim_track_open(&source, &output) == -1 && errno == EDOM);
}

static void test_vector_and_quaternion_tracks(void) {
    const anim_vector_key_t vectors[] = {
        { 0.0f, { 0.0f, 2.0f, 4.0f, 1.0f } },
        { 2.0f, { 2.0f, 4.0f, 8.0f, 3.0f } }
    };
    const anim_quaternion_key_t rotations[] = {
        { 0.0f, { 2.0f, 0.0f, 0.0f, 0.0f } },
        { 2.0f, { 0.0f, 0.0f, 0.0f, 3.0f } }
    };
    const anim_quaternion_key_t antipodal[] = {
        { 0.0f, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { 1.0f, { -1.0f, 0.0f, 0.0f, 0.0f } }
    };
    anim_track_view_t vector_track = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        vectors, 2, sizeof(vectors[0]));
    anim_track_view_t rotation_track = open_track(
        ANIM_VALUE_QUATERNION, ANIM_INTERPOLATION_LINEAR,
        rotations, 2, sizeof(rotations[0]));
    anim_track_view_t antipodal_track = open_track(
        ANIM_VALUE_QUATERNION, ANIM_INTERPOLATION_LINEAR,
        antipodal, 2, sizeof(antipodal[0]));
    vector_t vector;
    anim_quaternion_t quaternion;

    assert(anim_track_sample_vector(&vector_track, 1.0f, &vector, NULL) == 0);
    assert(vector.x == 1.0f && vector.y == 3.0f && vector.z == 6.0f &&
           vector.w == 2.0f);

    assert(anim_track_sample_quaternion(&rotation_track, 1.0f,
                                        &quaternion, NULL) == 0);
    assert(close_enough(quaternion.w, 0.70710678f));
    assert(close_enough(quaternion.z, 0.70710678f));
    assert(close_enough(quaternion.x, 0.0f) &&
           close_enough(quaternion.y, 0.0f));

    assert(anim_track_sample_quaternion(&antipodal_track, 0.5f,
                                        &quaternion, NULL) == 0);
    assert(close_enough(fabsf(quaternion.w), 1.0f));
    assert(close_enough(quaternion.x, 0.0f) &&
           close_enough(quaternion.y, 0.0f) &&
           close_enough(quaternion.z, 0.0f));
}

static anim_transform_t identity_transform(void) {
    anim_transform_t transform = {
        .translation = { 0.0f, 0.0f, 0.0f, 1.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
    };

    return transform;
}

static void test_transforms(void) {
    const anim_vector_key_t translation_keys[] = {
        { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { 2.0f, { 4.0f, 0.0f, 0.0f, 1.0f } }
    };
    const anim_quaternion_key_t rotation_keys[] = {
        { 0.0f, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    anim_track_view_t translation = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        translation_keys, 2, sizeof(translation_keys[0]));
    anim_track_view_t rotation = open_track(
        ANIM_VALUE_QUATERNION, ANIM_INTERPOLATION_LINEAR,
        rotation_keys, 2, sizeof(rotation_keys[0]));
    anim_transform_tracks_t tracks = {
        .translation = &translation,
        .rotation = &rotation,
        .scale = NULL,
        .fallback = identity_transform()
    };
    anim_transform_t sampled;
    anim_transform_t blended;
    anim_transform_t target = identity_transform();
    alignas(8) matrix_t matrix;
    alignas(8) matrix_t unchanged_matrix;

    target.translation.x = 8.0f;
    target.rotation.w = 0.0f;
    target.rotation.z = 1.0f;
    target.scale.x = 3.0f;
    target.scale.y = 5.0f;
    target.scale.z = 7.0f;

    assert(anim_transform_sample(&tracks, 1.0f, &sampled) == 0);
    assert(sampled.translation.x == 2.0f && sampled.scale.x == 1.0f);
    assert(close_enough(sampled.rotation.w, 0.70710678f));
    assert(close_enough(sampled.rotation.z, 0.70710678f));

    assert(anim_transform_blend(&tracks.fallback, &target, 0.5f,
                                &blended) == 0);
    assert(blended.translation.x == 4.0f);
    assert(blended.scale.x == 2.0f && blended.scale.y == 3.0f &&
           blended.scale.z == 4.0f);
    assert(close_enough(blended.rotation.w, 0.70710678f));
    assert(close_enough(blended.rotation.z, 0.70710678f));

    assert(anim_transform_matrix_build(&blended, &matrix) == 0);
    assert(close_enough(matrix[0][0], 0.0f));
    assert(close_enough(matrix[0][1], 2.0f));
    assert(close_enough(matrix[1][0], -3.0f));
    assert(close_enough(matrix[1][1], 0.0f));
    assert(close_enough(matrix[2][2], 4.0f));
    assert(matrix[3][0] == 4.0f && matrix[3][1] == 0.0f &&
           matrix[3][2] == 0.0f && matrix[3][3] == 1.0f);

    memset(&matrix, 0x5a, sizeof(matrix));
    memcpy(&unchanged_matrix, &matrix, sizeof(matrix));
    blended.rotation.w = NAN;
    errno = 0;
    assert(anim_transform_matrix_build(&blended, &matrix) == -1);
    assert(errno == EINVAL &&
           memcmp(&matrix, &unchanged_matrix, sizeof(matrix)) == 0);

    memset(&sampled, 0x5a, sizeof(sampled));
    memcpy(&target, &sampled, sizeof(target));
    errno = 0;
    assert(anim_transform_blend(&tracks.fallback, &tracks.fallback,
                                NAN, &sampled) == -1);
    assert(errno == EINVAL && memcmp(&sampled, &target, sizeof(sampled)) == 0);
}

int main(void) {
    test_scalar_tracks();
    test_track_rejection();
    test_vector_and_quaternion_tracks();
    test_transforms();
    puts("animation tests: PASS");
    return 0;
}
