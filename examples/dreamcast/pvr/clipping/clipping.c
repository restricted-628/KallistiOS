/* KallistiOS ##version##

   examples/dreamcast/pvr/clipping/clipping.c
   Copyright (C) 2026 Joseph Black

   Exercises checked per-scene pixel clipping and TA user-clip commands.
*/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static const pvr_pixel_clip_t pixel_clip = {
    .left = 160,
    .top = 96,
    .right = 480,
    .bottom = 384
};

static const pvr_user_clip_t user_clip = {
    .left = 6,
    .top = 4,
    .right = 13,
    .bottom = 10
};

static void submit_fullscreen_quad(const pvr_poly_hdr_t *header) {
    const pvr_vertex_t vertices[4] __attribute__((aligned(32))) = {
        { .flags = PVR_CMD_VERTEX, .x = 0.0f, .y = 0.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 640.0f, .y = 0.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 0.0f, .y = 480.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 640.0f, .y = 480.0f,
          .z = 1.0f, .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00,
          .oargb = 0 }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    const pvr_user_clip_t too_wide = { 0, 0, PVR_USER_CLIP_MAX_X + 1, 0 };
    const pvr_user_clip_t outside_target = { 0, 0, 20, 14 };
    pvr_pipeline_status_t status;
    pvr_pixel_clip_t observed;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_poly_hdr_t command;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);

    errno = 0;
    assert(pvr_scene_set_pixel_clip(&pixel_clip) == -1);
    assert(errno == EPERM);

    errno = 0;
    assert(pvr_user_clip_compile(&command, PVR_LIST_OP_POLY,
                                 &too_wide) == -1);
    assert(errno == EINVAL);
    assert(pvr_user_clip_compile(&command, PVR_LIST_OP_POLY,
                                 &user_clip) == 0);
    assert(command.cmd == (PVR_CMD_USERCLIP |
                           FIELD_PREP(PVR_TA_CMD_TYPE, PVR_LIST_OP_POLY)));
    assert(command.start_x == user_clip.left);
    assert(command.start_y == user_clip.top);
    assert(command.end_x == user_clip.right);
    assert(command.end_y == user_clip.bottom);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.clip_mode = PVR_USERCLIP_INSIDE;
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_scene_set_pixel_clip(&pixel_clip) == 0);
        assert(pvr_scene_get_pixel_clip(&observed) == 0);
        assert(observed.left == pixel_clip.left);
        assert(observed.top == pixel_clip.top);
        assert(observed.right == pixel_clip.right);
        assert(observed.bottom == pixel_clip.bottom);

        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);

        if(frame == 0) {
            errno = 0;
            assert(pvr_scene_set_pixel_clip(&pixel_clip) == -1);
            assert(errno == EBUSY);
            errno = 0;
            assert(pvr_user_clip_submit(PVR_LIST_OP_MOD, &user_clip) == -1);
            assert(errno == ENODEV);
            errno = 0;
            assert(pvr_user_clip_submit(PVR_LIST_OP_POLY,
                                        &outside_target) == -1);
            assert(errno == EINVAL);
        }

        assert(pvr_user_clip_submit(PVR_LIST_OP_POLY, &user_clip) == 0);
        submit_fullscreen_quad(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();

    errno = 0;
    assert(pvr_scene_get_pixel_clip(&observed) == -1);
    assert(errno == EPERM);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);

    puts("RESULT: PASS (checked PVR clipping)");
    pvr_shutdown();

    return 0;
}
