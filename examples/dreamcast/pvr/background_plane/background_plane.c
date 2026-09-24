/* KallistiOS ##version##

   examples/dreamcast/pvr/background_plane/background_plane.c
   Copyright (C) 2026 Joseph Black

   Exercises checked per-scene background-plane geometry and colors.
*/

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static const pvr_background_plane_t gradient = {
    .depth = 0.0001f,
    .vertices = {
        { .x = 0.0f, .y = 480.0f, .z = 0.00001f, .color = 0x000000ff },
        { .x = 0.0f, .y = 0.0f, .z = 0.00001f, .color = 0x00ff0000 },
        { .x = 640.0f, .y = 480.0f, .z = 0.00001f,
          .color = 0x0000ff00 }
    }
};

static void check_same_plane(const pvr_background_plane_t *a,
                             const pvr_background_plane_t *b) {
    unsigned int i;

    assert(a->depth == b->depth);
    for(i = 0; i < 3; ++i) {
        assert(a->vertices[i].x == b->vertices[i].x);
        assert(a->vertices[i].y == b->vertices[i].y);
        assert(a->vertices[i].z == b->vertices[i].z);
        assert(a->vertices[i].color == b->vertices[i].color);
    }
}

int main(int argc, char **argv) {
    pvr_background_plane_t invalid = gradient;
    pvr_background_plane_t observed;
    pvr_pipeline_status_t status;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);

    errno = 0;
    assert(pvr_scene_set_background_plane(&gradient) == -1);
    assert(errno == EPERM);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        if(frame == 0) {
            invalid.depth = NAN;
            errno = 0;
            assert(pvr_scene_set_background_plane(&invalid) == -1);
            assert(errno == EINVAL);
            invalid = gradient;
            invalid.vertices[1].color = 0xff000000;
            errno = 0;
            assert(pvr_scene_set_background_plane(&invalid) == -1);
            assert(errno == EINVAL);
        }

        assert(pvr_scene_set_background_plane(&gradient) == 0);
        assert(pvr_scene_get_background_plane(&observed) == 0);
        check_same_plane(&observed, &gradient);

        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        if(frame == 0) {
            errno = 0;
            assert(pvr_scene_set_background_plane(&gradient) == -1);
            assert(errno == EBUSY);
        }
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();

    errno = 0;
    assert(pvr_scene_get_background_plane(&observed) == -1);
    assert(errno == EPERM);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);

    puts("RESULT: PASS (PVR background plane)");
    pvr_shutdown();

    return 0;
}
