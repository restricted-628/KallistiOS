/* KallistiOS ##version##

   examples/dreamcast/pvr/render_ticket/render_ticket.c
   Copyright (C) 2026 Joseph Black

   Exercises identity-specific render and display completion.
*/

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define RTT_WIDTH 128u
#define RTT_HEIGHT 128u
#define FRAME_COUNT 60u

static void submit_panel(const pvr_poly_hdr_t *header, float width,
                         float height, uint32_t color) {
    alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = 0.0f, .y = 0.0f, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = width, .y = 0.0f, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 0.0f, .y = height, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = width, .y = height, .z = 1.0f,
          .argb = color, .oargb = 0 }
    };

    assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
    assert(pvr_list_finish() == 0);
}

int main(int argc, char **argv) {
    const pvr_init_params_t params = {
        { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
          PVR_BINSIZE_0, PVR_BINSIZE_0 },
        64 * 1024, 0, 0, 0, 0, 0
    };
    pvr_render_ticket_t texture_ticket = { 0 };
    pvr_render_ticket_t display_ticket = { 0 };
    pvr_render_ticket_t forged_ticket;
    pvr_pipeline_status_t status;
    pvr_render_stage_t stage;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_ptr_t texture;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init(&params) == 0);
    texture = pvr_mem_malloc(RTT_WIDTH * RTT_HEIGHT * 2u);
    assert(texture);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);

    errno = 0;
    assert(pvr_scene_finish_tracked(NULL) == -1);
    assert(errno == EINVAL);

    for(frame = 0; frame < FRAME_COUNT; ++frame) {
        assert(pvr_wait_ready() == 0);
        assert(pvr_scene_begin_rtt(texture, RTT_WIDTH, RTT_HEIGHT,
                                   RTT_WIDTH) == 0);
        submit_panel(&header, RTT_WIDTH, RTT_HEIGHT,
                     0xff202040u + (frame << 8));
        assert(pvr_scene_finish_tracked(&texture_ticket) == 0);
        assert(texture_ticket.id != PVR_RENDER_ID_INVALID);
        assert(texture_ticket.target == texture);
        assert(texture_ticket.to_texture != 0);
        assert(texture_ticket.width == RTT_WIDTH);
        assert(texture_ticket.height == RTT_HEIGHT);
        assert(texture_ticket.stride == RTT_WIDTH);

        errno = 0;
        assert(pvr_render_ticket_wait(&texture_ticket,
                                      PVR_RENDER_STAGE_DISPLAYED, 1000) == -1);
        assert(errno == ENOTSUP);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        submit_panel(&header, 640.0f, 480.0f,
                     0xff401010u + (frame << 16));
        assert(pvr_scene_finish_tracked(&display_ticket) == 0);
        assert(display_ticket.id > texture_ticket.id);
        assert(display_ticket.target == NULL);
        assert(display_ticket.to_texture == 0);

        assert(pvr_render_ticket_wait(&texture_ticket,
                                      PVR_RENDER_STAGE_COMPLETE, 1000) == 0);
        assert(pvr_render_ticket_wait(&display_ticket,
                                      PVR_RENDER_STAGE_DISPLAYED, 1000) == 0);
        assert(pvr_render_ticket_get_stage(&texture_ticket, &stage) == 0);
        assert(stage == PVR_RENDER_STAGE_COMPLETE);
        assert(pvr_render_ticket_get_stage(&display_ticket, &stage) == 0);
        assert(stage == PVR_RENDER_STAGE_DISPLAYED);
    }

    forged_ticket = display_ticket;
    forged_ticket.id += 100;
    errno = 0;
    assert(pvr_render_ticket_get_stage(&forged_ticket, &stage) == -1);
    assert(errno == ENOENT);

    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.completed_render_id >= display_ticket.id);
    assert(status.displayed_render_id == display_ticket.id);
    assert(status.pending_display_render_id == PVR_RENDER_ID_INVALID);
    assert(status.faults.mask == PVR_FAULT_NONE);

    pvr_mem_free(texture);
    puts("RESULT: PASS (PVR render tickets)");
    pvr_shutdown();
    return 0;
}
