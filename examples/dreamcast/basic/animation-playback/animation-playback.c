/* KallistiOS ##version##

   Caller-owned animation playback example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static const anim_vector_key_t parent_keys[] = {
    { 0.0f, { -2.0f, 0.0f, 0.0f, 1.0f } },
    { 2.0f, {  2.0f, 0.0f, 0.0f, 1.0f } }
};

static const anim_vector_key_t child_keys[] = {
    { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f } },
    { 2.0f, { 0.0f, 3.0f, 0.0f, 1.0f } }
};

static const anim_vector_key_t camera_keys[] = {
    { 0.0f, { 0.0f, 2.0f, 8.0f, 1.0f } },
    { 2.0f, { 2.0f, 3.0f, 8.0f, 1.0f } }
};

static anim_transform_t identity_transform(void) {
    const anim_transform_t identity = {
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    };

    return identity;
}

static void matrix_identity(matrix_t *matrix) {
    static const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(matrix, &identity, sizeof(identity));
}

int main(int argc, char **argv) {
    const anim_track_t parent_track = {
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        parent_keys, 2, sizeof(parent_keys[0])
    };
    const anim_track_t child_track = {
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        child_keys, 2, sizeof(child_keys[0])
    };
    const anim_track_t camera_track = {
        ANIM_VALUE_VECTOR, ANIM_INTERPOLATION_LINEAR,
        camera_keys, 2, sizeof(camera_keys[0])
    };
    anim_track_view_t parent_view;
    anim_track_view_t child_view;
    anim_track_view_t camera_view;
    anim_transform_tracks_t transforms[2];
    anim_clip_t clip;
    anim_clip_view_t clip_view;
    anim_playback_t playback;
    anim_camera_tracks_t camera_tracks;
    anim_light_tracks_t light_tracks;
    pvr_chunk_hierarchy_node_t nodes[2];
    pvr_chunk_hierarchy_t hierarchy = { nodes, 2 };
    alignas(32) matrix_t local[2];
    alignas(32) matrix_t world[2];
    alignas(32) matrix_t view_matrix;
    alignas(32) matrix_t projection_matrix;
    anim_camera_pose_t camera;
    pvr_light_t light;
    size_t frame;

    (void)argc;
    (void)argv;

    assert(anim_track_open(&parent_track, &parent_view) == 0);
    assert(anim_track_open(&child_track, &child_view) == 0);
    assert(anim_track_open(&camera_track, &camera_view) == 0);

    memset(transforms, 0, sizeof(transforms));
    transforms[0].translation = &parent_view;
    transforms[0].fallback = identity_transform();
    transforms[1].translation = &child_view;
    transforms[1].fallback = identity_transform();
    clip.transforms = transforms;
    clip.transform_count = 2;
    clip.start_time = 0.0f;
    clip.end_time = 2.0f;
    assert(anim_clip_open(&clip, &clip_view) == 0);
    assert(anim_playback_init(&playback, &clip_view,
                              ANIM_PLAYBACK_LOOP) == 0);
    assert(anim_playback_play(&playback) == 0);

    memset(nodes, 0, sizeof(nodes));
    nodes[0].parent_index = PVR_CHUNK_NODE_NONE;
    nodes[1].parent_index = 0;
    matrix_identity(&nodes[0].local_transform);
    matrix_identity(&nodes[1].local_transform);

    memset(&camera_tracks, 0, sizeof(camera_tracks));
    camera_tracks.eye = &camera_view;
    camera_tracks.fallback.eye = camera_keys[0].value;
    camera_tracks.fallback.target = (point_t){ 0.0f, 0.0f, 0.0f, 1.0f };
    camera_tracks.fallback.up = (vector_t){ 0.0f, 1.0f, 0.0f, 0.0f };
    camera_tracks.fallback.vertical_fov = 1.04719755119659774615f;

    memset(&light_tracks, 0, sizeof(light_tracks));
    light_tracks.source = &parent_view;
    light_tracks.fallback.kind = PVR_LIGHT_POINT;
    light_tracks.fallback.source.position = parent_keys[0].value;
    light_tracks.fallback.color =
        (vector_t){ 1.0f, 0.8f, 0.6f, 0.0f };
    light_tracks.fallback.intensity = 1.0f;
    light_tracks.fallback.attenuation_constant = 1.0f;
    light_tracks.fallback.range = 20.0f;

    for(frame = 0; frame < 180; ++frame) {
        assert(anim_clip_sample_matrices(&clip_view, playback.time,
                                         local, 2, NULL) == 0);
        assert(pvr_chunk_hierarchy_traverse_transforms(
                   &hierarchy, local, 2, NULL, world, 2,
                   NULL, NULL, NULL) == 0);
        assert(anim_playback_sample_camera(&playback, &camera_tracks,
                                           &camera) == 0);
        assert(anim_camera_view_matrix_build(&camera, &view_matrix) == 0);
        assert(anim_camera_projection_matrix_build(
                   &camera, 320.0f, 240.0f, 0.1f, 1000.0f,
                   &projection_matrix) == 0);
        assert(anim_playback_sample_light(&playback, &light_tracks,
                                          &light) == 0);
        assert(isfinite(world[1][3][0]) && isfinite(world[1][3][1]) &&
               isfinite(view_matrix[3][2]) &&
               isfinite(projection_matrix[0][0]) &&
               isfinite(light.source.position.x));
        assert(anim_playback_advance(&playback, 1.0f / 60.0f, NULL) == 0);
        thd_sleep(16);
    }

    assert(playback.boundary_count >= 1);
    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2,
                   vid_mode->width, 1,
                   "RESULT: PASS (animation playback)");
    puts("RESULT: PASS (animation playback)");

    for(;;)
        thd_sleep(1000);
}
