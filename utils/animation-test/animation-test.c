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
    anim_boolean_key_t boolean = { 0.0f, 1u };

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

    source.kind = ANIM_VALUE_BOOLEAN;
    source.interpolation = ANIM_INTERPOLATION_LINEAR;
    source.keys = &boolean;
    source.stride = sizeof(boolean);
    errno = 0;
    assert(anim_track_open(&source, &output) == -1 && errno == EINVAL);
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
    const anim_boolean_key_t visibility_keys[] = {
        { 0.0f, 1u }, { 1.0f, 0u }, { 2.0f, 1u }
    };
    anim_track_view_t translation = open_track(
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        translation_keys, 2, sizeof(translation_keys[0]));
    anim_track_view_t visibility = open_track(
        ANIM_VALUE_BOOLEAN, ANIM_INTERPOLATION_STEP,
        visibility_keys, 3, sizeof(visibility_keys[0]));
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
    const anim_visibility_tracks_t visibility_tracks[2] = {
        { &visibility, true }, { NULL, false }
    };
    const anim_clip_t clip_source = {
        tracks, 2, 0.0f, 2.0f, visibility_tracks
    };
    const anim_clip_t linked_source = {
        linked_tracks, 2, 0.0f, 2.0f, NULL
    };
    anim_clip_view_t clip;
    anim_clip_view_t linked;
    anim_transform_t pose[2];
    alignas(8) matrix_t matrices[2];
    anim_pose_result_t pose_result;
    anim_playback_t playback;
    anim_playback_t unchanged_playback;
    anim_playback_result_t advance;
    anim_playback_result_t unchanged_advance;
    bool visible[2];

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
    assert(anim_clip_sample_visibility(&clip, 1.5f, visible, 2,
                                       &pose_result) == 0);
    assert(!visible[0] && !visible[1] &&
           pose_result.sampled_transforms == 2);
    assert(anim_clip_sample_visibility(&linked, 1.0f, visible, 2,
                                       &pose_result) == 0);
    assert(visible[0] && visible[1]);

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

static void brute_emit_segment(const anim_event_key_t *events,
                               size_t event_count, float from, float to,
                               anim_playback_direction_t direction,
                               anim_event_occurrence_t *output,
                               size_t *count) {
    size_t i;

    if(direction == ANIM_PLAYBACK_FORWARD) {
        for(i = 0; i < event_count; ++i) {
            if(events[i].time > from && events[i].time <= to) {
                output[*count].event = events[i];
                output[(*count)++].direction = direction;
            }
        }
    }
    else {
        for(i = event_count; i > 0; --i) {
            if(events[i - 1u].time >= to && events[i - 1u].time < from) {
                output[*count].event = events[i - 1u];
                output[(*count)++].direction = direction;
            }
        }
    }
}

static void brute_emit_point(const anim_event_key_t *events,
                             size_t event_count, float time,
                             anim_playback_direction_t direction,
                             anim_event_occurrence_t *output,
                             size_t *count) {
    size_t i;

    for(i = 0; i < event_count; ++i) {
        if(events[i].time == time) {
            output[*count].event = events[i];
            output[(*count)++].direction = direction;
            return;
        }
    }
}

static size_t brute_playback_events(const anim_event_key_t *events,
                                    size_t event_count,
                                    anim_playback_mode_t mode,
                                    float initial_time,
                                    anim_playback_direction_t initial_direction,
                                    float elapsed,
                                    anim_event_occurrence_t *output) {
    const float start = 0.0f;
    const float end = 2.0f;
    anim_playback_direction_t direction = initial_direction;
    float time = initial_time;
    float remaining = elapsed;
    size_t count = 0;

    if(remaining == 0.0f)
        return 0;

    if((time == start && direction == ANIM_PLAYBACK_BACKWARD) ||
       (time == end && direction == ANIM_PLAYBACK_FORWARD)) {
        if(mode == ANIM_PLAYBACK_ONCE)
            return 0;
        if(mode == ANIM_PLAYBACK_LOOP) {
            time = direction == ANIM_PLAYBACK_FORWARD ? start : end;
            brute_emit_point(events, event_count, time, direction,
                             output, &count);
        }
        else {
            direction = direction == ANIM_PLAYBACK_FORWARD ?
                ANIM_PLAYBACK_BACKWARD : ANIM_PLAYBACK_FORWARD;
        }
    }

    while(remaining > 0.0f) {
        float boundary = direction == ANIM_PLAYBACK_FORWARD ? end : start;
        float distance = fabsf(boundary - time);
        float step = remaining < distance ? remaining : distance;
        float next = direction == ANIM_PLAYBACK_FORWARD ?
            time + step : time - step;

        brute_emit_segment(events, event_count, time, next, direction,
                           output, &count);
        time = next;
        remaining -= step;
        if(step < distance)
            break;
        if(mode == ANIM_PLAYBACK_ONCE)
            break;
        if(mode == ANIM_PLAYBACK_LOOP) {
            time = direction == ANIM_PLAYBACK_FORWARD ? start : end;
            brute_emit_point(events, event_count, time, direction,
                             output, &count);
        }
        else {
            direction = direction == ANIM_PLAYBACK_FORWARD ?
                ANIM_PLAYBACK_BACKWARD : ANIM_PLAYBACK_FORWARD;
        }
    }
    return count;
}

static void test_events_and_morph_binding(void) {
    const anim_event_key_t event_keys[] = {
        { 0.0f, 0u, 100u },
        { 0.5f, 5u, 105u },
        { 1.5f, 15u, 115u },
        { 2.0f, 20u, 120u }
    };
    anim_event_key_t invalid_keys[] = {
        { 0.0f, 1u, 0u }, { 0.0f, 2u, 0u }
    };
    const anim_scalar_key_t weight_keys[] = {
        { 0.0f, 0.0f }, { 2.0f, 1.0f }
    };
    anim_track_view_t weight = open_track(
        ANIM_VALUE_SCALAR, ANIM_INTERPOLATION_LINEAR,
        weight_keys, 2, sizeof(weight_keys[0]));
    anim_transform_tracks_t transform = {
        .fallback = {
            .translation = { 0.0f, 0.0f, 0.0f, 1.0f },
            .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
            .scale = { 1.0f, 1.0f, 1.0f, 0.0f }
        }
    };
    const anim_clip_t clip_source = {
        &transform, 1, 0.0f, 2.0f, NULL
    };
    const anim_event_track_t event_source = {
        event_keys, sizeof(event_keys) / sizeof(event_keys[0])
    };
    anim_event_track_t invalid_source = { invalid_keys, 2 };
    anim_event_track_view_t events;
    anim_event_track_view_t unchanged_events;
    anim_clip_view_t clip;
    anim_playback_t playback;
    anim_playback_result_t advance;
    anim_event_occurrence_t occurrences[8];
    anim_event_result_t event_result;
    const anim_morph_target_tracks_t morph_tracks[2] = {
        {
            .weight = &weight,
            .fallback = { (const void *)(uintptr_t)0x1000u, 32, 0.0f }
        },
        {
            .fallback = { (const void *)(uintptr_t)0x2000u, 64, 0.25f }
        }
    };
    pvr_morph_target_t morphs[2];
    anim_morph_result_t morph_result;
    size_t mode;
    size_t direction_index;
    size_t time_index;
    size_t elapsed_index;

    assert(anim_clip_open(&clip_source, &clip) == 0);
    assert(anim_event_track_open(&event_source, &events) == 0);

    memset(&unchanged_events, 0x5a, sizeof(unchanged_events));
    events = unchanged_events;
    errno = 0;
    assert(anim_event_track_open(&invalid_source, &events) == -1);
    assert(errno == EILSEQ &&
           memcmp(&events, &unchanged_events, sizeof(events)) == 0);
    assert(anim_event_track_open(&event_source, &events) == 0);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_ONCE) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 1.0f, &advance) == 0);
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        occurrences, 8,
                                        &event_result) == 0);
    assert(event_result.matching_events == 1 &&
           event_result.published_events == 1 && !event_result.truncated &&
           occurrences[0].event.identifier == 5u &&
           occurrences[0].direction == ANIM_PLAYBACK_FORWARD);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_LOOP) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 2.5f, &advance) == 0);
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        occurrences, 3,
                                        &event_result) == 0);
    assert(event_result.matching_events == 5 &&
           event_result.published_events == 3 && event_result.truncated);
    assert(occurrences[0].event.identifier == 5u &&
           occurrences[1].event.identifier == 15u &&
           occurrences[2].event.identifier == 20u);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_LOOP) == 0);
    assert(anim_playback_seek(&playback, 1.0f) == 0);
    assert(anim_playback_set_direction(&playback,
                                       ANIM_PLAYBACK_BACKWARD) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 1.5f, &advance) == 0);
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        occurrences, 8,
                                        &event_result) == 0);
    assert(event_result.matching_events == 4 &&
           occurrences[0].event.identifier == 5u &&
           occurrences[1].event.identifier == 0u &&
           occurrences[2].event.identifier == 20u &&
           occurrences[3].event.identifier == 15u);
    assert(occurrences[0].direction == ANIM_PLAYBACK_BACKWARD &&
           occurrences[3].direction == ANIM_PLAYBACK_BACKWARD);

    assert(anim_playback_init(&playback, &clip,
                              ANIM_PLAYBACK_PING_PONG) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 4.5f, &advance) == 0);
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        occurrences, 8,
                                        &event_result) == 0);
    assert(event_result.matching_events == 7 &&
           event_result.published_events == 7 && !event_result.truncated);
    assert(occurrences[0].event.identifier == 5u &&
           occurrences[2].event.identifier == 20u &&
           occurrences[3].event.identifier == 15u &&
           occurrences[5].event.identifier == 0u &&
           occurrences[6].event.identifier == 5u);
    assert(occurrences[2].direction == ANIM_PLAYBACK_FORWARD &&
           occurrences[3].direction == ANIM_PLAYBACK_BACKWARD &&
           occurrences[6].direction == ANIM_PLAYBACK_FORWARD);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_LOOP) == 0);
    assert(anim_playback_play(&playback) == 0);
    assert(anim_playback_advance(&playback, 2000000.0f, &advance) == 0);
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        NULL, 0, &event_result) == 0);
    assert(event_result.matching_events == UINT64_C(4000000) &&
           event_result.published_events == 0 && event_result.truncated);

    assert(anim_playback_init(&playback, &clip, ANIM_PLAYBACK_LOOP) == 0);
    playback.state = ANIM_PLAYBACK_PLAYING;
    playback.boundary_count = UINT64_MAX;
    advance = (anim_playback_result_t) {
        .previous_time = 0.0f,
        .current_time = 0.0f,
        .crossed_boundaries = UINT64_MAX,
        .previous_direction = ANIM_PLAYBACK_FORWARD,
        .current_direction = ANIM_PLAYBACK_FORWARD,
        .state = ANIM_PLAYBACK_PLAYING
    };
    memset(occurrences, 0x5a, sizeof(occurrences));
    errno = 0;
    assert(anim_playback_collect_events(&playback, &advance, &events,
                                        occurrences, 8,
                                        &event_result) == -1);
    assert(errno == ERANGE && event_result.matching_events == 0 &&
           event_result.published_events == 0 && !event_result.truncated);

    assert(anim_morph_targets_sample(morph_tracks, 2, 1.0f, morphs, 2,
                                     &morph_result) == 0);
    assert(morph_result.sampled_targets == 2 &&
           close_enough(morphs[0].weight, 0.5f) &&
           close_enough(morphs[1].weight, 0.25f) &&
           morphs[0].deltas == morph_tracks[0].fallback.deltas &&
           morphs[1].stride == 64);

    /* Exhaustively compare the arithmetic collector with a small stepwise
       oracle over exact binary fractions and both directions. */
    for(mode = ANIM_PLAYBACK_ONCE; mode <= ANIM_PLAYBACK_PING_PONG; ++mode) {
        for(direction_index = 0; direction_index < 2; ++direction_index) {
            anim_playback_direction_t direction = direction_index ?
                ANIM_PLAYBACK_BACKWARD : ANIM_PLAYBACK_FORWARD;

            for(time_index = 0; time_index <= 8; ++time_index) {
                for(elapsed_index = 0; elapsed_index <= 24;
                    ++elapsed_index) {
                    anim_event_occurrence_t expected[32];
                    anim_event_occurrence_t actual[32];
                    float initial_time = (float)time_index * 0.25f;
                    float elapsed = (float)elapsed_index * 0.25f;
                    size_t expected_count = brute_playback_events(
                        event_keys, sizeof(event_keys) / sizeof(event_keys[0]),
                        (anim_playback_mode_t)mode, initial_time, direction,
                        elapsed, expected);
                    size_t i;

                    assert(expected_count <= 32);
                    assert(anim_playback_init(
                               &playback, &clip,
                               (anim_playback_mode_t)mode) == 0);
                    assert(anim_playback_seek(&playback, initial_time) == 0);
                    assert(anim_playback_set_direction(
                               &playback, direction) == 0);
                    assert(anim_playback_play(&playback) == 0);
                    assert(anim_playback_advance(
                               &playback, elapsed, &advance) == 0);
                    assert(anim_playback_collect_events(
                               &playback, &advance, &events, actual, 32,
                               &event_result) == 0);
                    assert(!event_result.truncated &&
                           event_result.matching_events == expected_count &&
                           event_result.published_events == expected_count);
                    for(i = 0; i < expected_count; ++i) {
                        assert(actual[i].event.identifier ==
                               expected[i].event.identifier);
                        assert(actual[i].direction == expected[i].direction);
                    }
                }
            }
        }
    }
}

int main(void) {
    test_scalar_tracks();
    test_track_rejection();
    test_vector_and_quaternion_tracks();
    test_transforms();
    test_clips_and_playback();
    test_camera_and_light_binding();
    test_events_and_morph_binding();
    puts("animation tests: PASS");
    return 0;
}
