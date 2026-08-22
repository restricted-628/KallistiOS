/* KallistiOS ##version##

   examples/dreamcast/pvr/pipeline_status/pipeline_status.c
   Copyright (C) 2026 Joseph Black

   Exercises coherent PVR pipeline snapshots and checked fault-state APIs.
*/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static void submit_triangle(const pvr_poly_hdr_t *header) {
    const pvr_vertex_t vertices[3] __attribute__((aligned(32))) = {
        { .flags = PVR_CMD_VERTEX, .x = 160.0f, .y = 380.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 320.0f, .y = 100.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 480.0f, .y = 380.0f,
          .z = 1.0f, .u = 0.0f, .v = 0.0f, .argb = 0xff00ff00,
          .oargb = 0 }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    pvr_pipeline_status_t initial;
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    unsigned int frame;

    (void)argc;
    (void)argv;

    errno = 0;
    assert(pvr_get_pipeline_status(&status) == -1);
    assert(errno == ENODEV);

    assert(pvr_init_defaults() == 0);

    errno = 0;
    assert(pvr_get_pipeline_status(NULL) == -1);
    assert(errno == EINVAL);

    assert(pvr_get_pipeline_status(&initial) == 0);
    assert(initial.initialized != 0);
    assert(initial.scene_active == 0);
    assert(initial.render_busy == 0);
    assert(initial.faults.mask == PVR_FAULT_NONE);

    errno = 0;
    assert(pvr_clear_faults(PVR_FAULT_ALL << 1) == -1);
    assert(errno == EINVAL);
    assert(pvr_clear_faults(PVR_FAULT_ALL) == 0);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    pvr_poly_compile(&header, &context);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        if(frame == 0) {
            assert(pvr_get_pipeline_status(&status) == 0);
            assert(status.scene_active != 0);
            assert(status.sequence > initial.sequence);
            assert(status.open_list == PVR_LIST_NONE);
        }

        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);

        if(frame == 0) {
            assert(pvr_get_pipeline_status(&status) == 0);
            assert(status.scene_active != 0);
            assert(status.ta_busy != 0);
            assert(status.open_list == PVR_LIST_OP_POLY);
        }

        submit_triangle(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);

        if(frame == 0) {
            assert(pvr_get_pipeline_status(&status) == 0);
            assert(status.scene_active == 0);
            assert(status.open_list == PVR_LIST_NONE);
        }
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();

    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.sequence > initial.sequence);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(status.faults.sequence == 0);

    puts("RESULT: PASS (PVR pipeline status)");
    pvr_shutdown();

    return 0;
}
