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

/* Checked matrix apply wrappers are not exercised by this host-only suite. */
void mat_apply(const matrix_t *matrix) {
    (void)matrix;
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

static void test_clips_and_playback(void) {
    const anim_vector_key_t translation_keys[] = {
        { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { 2.0f, { 10.0f, 0.0f, 0.0f, 1.0f } }
    };
    anim_track_view_t translation = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        translation_keys, 2, sizeof(translation_keys[0]));
    anim_transform_tracks_t tracks[2] = {
        {
            .translation = &translation,
            .fallback = {
                .translation = { 0.0f, 0.0f, 0.0f, 1.0f },
                .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
                .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
            }
        },
        {
            .fallback = {
                .translation = { 0.0f, 3.0f, 0.0f, 1.0f },
                .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
                .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
            }
        }
    };
    anim_transform_tracks_t linked_tracks[2] = {
        { .fallback = {
            .translation = { 20.0f, 0.0f, 0.0f, 1.0f },
            .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
            .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
        } },
        { .fallback = {
            .translation = { 0.0f, 7.0f, 0.0f, 1.0f },
            .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
            .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
        } }
    };
    const anim_clip_t clip_source = { tracks, 2, 0.0f, 2.0f };
    const anim_clip_t linked_source = { linked_tracks, 2, 0.0f, 2.0f };
    anim_clip_view_t clip;
    anim_clip_view_t linked;
    anim_transform_t pose[2];
    alignas(8) matrix_t matrices[2];
    anim_pose_result_t pose_result;
    anim_playback_t playback;
    anim_playback_t unchanged_playback;
    anim_playback_result_t advance;
    anim_playback_result_t unchanged_advance;

    assert(anim_clip_open(&clip_source, &clip) == 0);
    assert(anim_clip_open(&linked_source, &linked) == 0);
    assert(anim_clip_sample(&clip, 1.0f, pose, 2, &pose_result) == 0);
    assert(pose_result.sampled_transforms == 2);
    assert(pose[0].translation.x == 5.0f &&
           pose[1].translation.y == 3.0f);
    assert(anim_clip_sample_matrices(&clip, 1.0f, matrices, 2,
                                     &pose_result) == 0);
    assert(matrices[0][3][0] == 5.0f && matrices[1][3][1] == 3.0f);
    assert(anim_clip_sample_blend(&clip, 1.0f, &linked, 1.0f, 0.5f,
                                  pose, 2, &pose_result) == 0);
    assert(close_enough(pose[0].translation.x, 12.5f));
    assert(close_enough(pose[1].translation.y, 5.0f));

    pose_result.sampled_transforms = 99;
    errno = 0;
    assert(anim_clip_sample(&clip, 1.0f, pose, 1, &pose_result) == -1);
    assert(errno == ENOSPC && pose_result.sampled_transforms == 0);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_ONCE) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 0.5f, &advance) == 0);
    assert(playback.time == 0.5f && advance.crossed_boundaries == 0 &&
           advance.state == ANIM_PLAYBACK_PLAYING);
    assert(anim_playback_sample(&playback, pose, 2, &pose_result) == 0);
    assert(close_enough(pose[0].translation.x, 2.5f));
    assert(anim_playback_advance(&playback, 4.0f, &advance) == 0);
    assert(playback.time == 2.0f &&
           playback.state == ANIM_PLAYBACK_COMPLETE &&
           advance.crossed_boundaries == 1);
    assert(anim_playback_play(&playback) == 0 && playback.time == 0.0f);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_LOOP) == 0);
    assert(anim_playback_set_rate(&playback, 2.0f) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 2.5f, &advance) == 0);
    assert(playback.time == 1.0f && advance.crossed_boundaries == 2 &&
           playback.boundary_count == 2);
    assert(anim_playback_seek(&playback, 1.0f) == 0);
    assert(anim_playback_set_rate(&playback, 1.0f) == 0);
    assert(anim_playback_set_direction(&playback,
                                       ANIM_PLAYBACK_BACKWARD) == 0);
    assert(anim_playback_advance(&playback, 1.5f, &advance) == 0);
    assert(playback.time == 1.5f && advance.crossed_boundaries == 1);
    assert(anim_playback_pause(&playback) == 0);
    assert(anim_playback_advance(&playback, 1.0f, &advance) == 0);
    assert(playback.time == 1.5f && advance.crossed_boundaries == 0);

    assert(anim_playback_init(&playback, &clip,
                              ANIM_PLAYBACK_PING_PONG) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 2.5f, &advance) == 0);
    assert(playback.time == 1.5f &&
           playback.direction == ANIM_PLAYBACK_BACKWARD &&
           advance.crossed_boundaries == 1);
    assert(anim_playback_advance(&playback, 2.0f, &advance) == 0);
    assert(playback.time == 0.5f &&
           playback.direction == ANIM_PLAYBACK_FORWARD &&
           advance.crossed_boundaries == 1);
    assert(anim_playback_stop(&playback) == 0);
    assert(playback.time == 0.0f && playback.boundary_count == 0 &&
           playback.state == ANIM_PLAYBACK_STOPPED);

    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 2000001.0f, &advance) == 0);
    assert(playback.time == 1.0f &&
           playback.direction == ANIM_PLAYBACK_FORWARD &&
           advance.crossed_boundaries == UINT64_C(1000000));

    unchanged_playback = playback;
    memset(&advance, 0x5a, sizeof(advance));
    unchanged_advance = advance;
    errno = 0;
    assert(anim_playback_advance(&playback, -1.0f, &advance) == -1);
    assert(errno == EINVAL &&
           memcmp(&playback, &unchanged_playback, sizeof(playback)) == 0 &&
           memcmp(&advance, &unchanged_advance, sizeof(advance)) == 0);

    playback.boundary_count = UINT64_MAX;
    unchanged_playback = playback;
    errno = 0;
    assert(anim_playback_advance(&playback, 2.0f, &advance) == -1);
    assert(errno == ERANGE &&
           memcmp(&playback, &unchanged_playback, sizeof(playback)) == 0);
}

static void test_camera_and_light_binding(void) {
    const anim_vector_key_t eye_keys[] = {
        { 0.0f, { 0.0f, 0.0f, 5.0f, 1.0f } },
        { 2.0f, { 2.0f, 0.0f, 5.0f, 1.0f } }
    };
    const anim_scalar_key_t roll_keys[] = {
        { 0.0f, 0.0f }, { 2.0f, 1.57079632679489661923f }
    };
    const anim_scalar_key_t fov_keys[] = {
        { 0.0f, 1.57079632679489661923f },
        { 2.0f, 1.04719755119659774615f }
    };
    const anim_vector_key_t source_keys[] = {
        { 0.0f, { 0.0f, 2.0f, 0.0f, 1.0f } },
        { 2.0f, { 10.0f, 2.0f, 0.0f, 1.0f } }
    };
    const anim_vector_key_t color_keys[] = {
        { 0.0f, { 1.0f, 0.5f, 0.25f, 0.0f } },
        { 2.0f, { 0.5f, 0.25f, 0.0f, 0.0f } }
    };
    const anim_scalar_key_t intensity_keys[] = {
        { 0.0f, 2.0f }, { 2.0f, 4.0f }
    };
    const anim_scalar_key_t range_keys[] = {
        { 0.0f, 10.0f }, { 2.0f, 20.0f }
    };
    anim_track_view_t eye = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        eye_keys, 2, sizeof(eye_keys[0]));
    anim_track_view_t roll = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        roll_keys, 2, sizeof(roll_keys[0]));
    anim_track_view_t fov = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        fov_keys, 2, sizeof(fov_keys[0]));
    anim_track_view_t source = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        source_keys, 2, sizeof(source_keys[0]));
    anim_track_view_t color = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        color_keys, 2, sizeof(color_keys[0]));
    anim_track_view_t intensity = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        intensity_keys, 2, sizeof(intensity_keys[0]));
    anim_track_view_t range = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        range_keys, 2, sizeof(range_keys[0]));
    const anim_camera_tracks_t camera_tracks = {
        .eye = &eye,
        .roll = &roll,
        .vertical_fov = &fov,
        .fallback = {
            .eye = { 0.0f, 0.0f, 5.0f, 1.0f },
            .target = { 0.0f, 0.0f, 0.0f, 1.0f },
            .up = { 0.0f, 1.0f, 0.0f, 0.0f },
            .roll = 0.0f,
            .vertical_fov = 1.57079632679489661923f
        }
    };
    const anim_light_tracks_t light_tracks = {
        .source = &source,
        .color = &color,
        .intensity = &intensity,
        .range = &range,
        .fallback = {
            .kind = PVR_LIGHT_POINT,
            .source.position = { 0.0f, 2.0f, 0.0f, 1.0f },
            .color = { 1.0f, 0.5f, 0.25f, 0.0f },
            .intensity = 2.0f,
            .attenuation_constant = 1.0f,
            .attenuation_linear = 0.1f,
            .attenuation_quadratic = 0.01f,
            .range = 10.0f
        }
    };
    anim_camera_pose_t camera;
    anim_camera_pose_t unchanged_camera;
    anim_camera_tracks_t invalid_camera;
    pvr_light_t light;
    pvr_light_t unchanged_light;
    anim_light_tracks_t invalid_light;
    alignas(8) matrix_t view;
    alignas(8) matrix_t projection;

    assert(anim_camera_sample(&camera_tracks, 0.0f, &camera) == 0);
    assert(anim_camera_view_matrix_build(&camera, &view) == 0);
    assert(close_enough(view[3][0], 0.0f));
    assert(close_enough(view[3][1], 0.0f));
    assert(close_enough(view[3][2], -5.0f));
    assert(anim_camera_projection_matrix_build(
               &camera, 320.0f, 240.0f, 0.1f, 1000.0f,
               &projection) == 0);
    assert(isfinite(projection[0][0]) && isfinite(projection[3][2]));

    assert(anim_camera_sample(&camera_tracks, 1.0f, &camera) == 0);
    assert(close_enough(camera.eye.x, 1.0f));
    assert(close_enough(camera.roll, 0.78539816339744830962f));
    assert(anim_camera_view_matrix_build(&camera, &view) == 0);

    assert(anim_light_sample(&light_tracks, 1.0f, &light) == 0);
    assert(light.kind == PVR_LIGHT_POINT);
    assert(close_enough(light.source.position.x, 5.0f));
    assert(close_enough(light.color.x, 0.75f));
    assert(close_enough(light.intensity, 3.0f));
    assert(close_enough(light.range, 15.0f));
    assert(light.attenuation_constant == 1.0f &&
           light.attenuation_linear == 0.1f &&
           light.attenuation_quadratic == 0.01f);

    unchanged_camera = camera;
    invalid_camera = camera_tracks;
    invalid_camera.fallback.eye = invalid_camera.fallback.target;
    errno = 0;
    assert(anim_camera_sample(&invalid_camera, 1.0f, &camera) == -1);
    assert(errno == EINVAL &&
           memcmp(&camera, &unchanged_camera, sizeof(camera)) == 0);

    unchanged_light = light;
    invalid_light = light_tracks;
    invalid_light.fallback.intensity = -1.0f;
    errno = 0;
    assert(anim_light_sample(&invalid_light, 1.0f, &light) == -1);
    assert(errno == EINVAL &&
           memcmp(&light, &unchanged_light, sizeof(light)) == 0);
}

int main(void) {
    test_scalar_tracks();
    test_track_rejection();
    test_vector_and_quaternion_tracks();
    test_transforms();
    test_clips_and_playback();
    test_camera_and_light_binding();
    puts("animation tests: PASS");
    return 0;
}
