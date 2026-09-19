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

typedef struct event_counts {
    volatile uint32_t registration;
    volatile uint32_t render;
    volatile uint32_t display;
    volatile uint32_t fault;
    volatile uint32_t callback_error;
} event_counts_t;

static volatile uint32_t one_shot_count;
static volatile uint32_t one_shot_remove_error;
static int one_shot_handle = -1;

static void count_event(pvr_event_t event, uint32_t detail, void *user_data) {
    event_counts_t *counts = user_data;
    pvr_pipeline_status_t status;

    if(pvr_get_pipeline_status(&status) < 0) {
        ++counts->callback_error;
        return;
    }

    switch(event) {
        case PVR_EVENT_REGISTRATION_COMPLETE:
            if(detail != status.enabled_lists)
                ++counts->callback_error;
            ++counts->registration;
            break;
        case PVR_EVENT_RENDER_COMPLETE:
            if(detail != status.render_to_texture)
                ++counts->callback_error;
            ++counts->render;
            break;
        case PVR_EVENT_DISPLAY:
            if(detail != status.view_target)
                ++counts->callback_error;
            ++counts->display;
            break;
        case PVR_EVENT_FAULT:
            ++counts->fault;
            break;
        default:
            ++counts->callback_error;
            break;
    }
}

static void remove_one_shot(pvr_event_t event, uint32_t detail,
                            void *user_data) {
    (void)event;
    (void)detail;
    (void)user_data;

    ++one_shot_count;
    if(pvr_event_handler_remove(one_shot_handle) < 0)
        ++one_shot_remove_error;
}

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
    event_counts_t counts = { 0 };
    int event_handle;
    unsigned int frame;

    (void)argc;
    (void)argv;

    errno = 0;
    assert(pvr_get_pipeline_status(&status) == -1);
    assert(errno == ENODEV);

    errno = 0;
    assert(pvr_event_handler_add(PVR_EVENT_DISPLAY, count_event,
                                 &counts) == -1);
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

    errno = 0;
    assert(pvr_event_handler_add(0, count_event, &counts) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_event_handler_add(PVR_EVENT_DISPLAY, NULL, NULL) == -1);
    assert(errno == EINVAL);

    event_handle = pvr_event_handler_add(
        PVR_EVENT_REGISTRATION_COMPLETE | PVR_EVENT_RENDER_COMPLETE |
        PVR_EVENT_DISPLAY | PVR_EVENT_FAULT,
        count_event, &counts);
    assert(event_handle >= 0);
    one_shot_handle = pvr_event_handler_add(
        PVR_EVENT_REGISTRATION_COMPLETE, remove_one_shot, NULL);
    assert(one_shot_handle >= 0);

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

    assert(pvr_event_handler_remove(event_handle) == 0);
    errno = 0;
    assert(pvr_event_handler_remove(event_handle) == -1);
    assert(errno == ENOENT);

    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.sequence > initial.sequence);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(status.faults.sequence == 0);
    assert(counts.registration > 0);
    assert(counts.render > 0);
    assert(counts.display > 0);
    assert(counts.fault == 0);
    assert(counts.callback_error == 0);
    assert(one_shot_count == 1);
    assert(one_shot_remove_error == 0);

    puts("RESULT: PASS (PVR pipeline status)");
    pvr_shutdown();

    return 0;
}
