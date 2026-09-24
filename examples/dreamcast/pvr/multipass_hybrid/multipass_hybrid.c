/* KallistiOS ##version##

   examples/dreamcast/pvr/multipass_hybrid/multipass_hybrid.c
   Copyright (C) 2026 Joseph Black

   Exercises pass-aware early flushing and boundary DMA continuation.
*/

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define PASS_LIST_BUFFER_SIZE (64 * 1024)
#define FRAME_COUNT 120u

static alignas(32) uint8_t pass_buffers[3][2][PASS_LIST_BUFFER_SIZE];
static volatile uint32_t dma_completions;
static volatile uint32_t dma_faults;

static void observe_dma(pvr_event_t event, uint32_t detail, void *user_data) {
    (void)user_data;

    if(event == PVR_EVENT_DMA_COMPLETE && detail == 0)
        ++dma_completions;
    else if(event == PVR_EVENT_FAULT)
        ++dma_faults;
}

static void submit_panel(pvr_list_t list, const pvr_poly_hdr_t *header,
                         float left, float right, float z, uint32_t color) {
    alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = left, .y = 90.0f, .z = z,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = right, .y = 90.0f, .z = z,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = left, .y = 390.0f, .z = z,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = right, .y = 390.0f, .z = z,
          .argb = color, .oargb = 0 }
    };

    assert(pvr_list_begin(list) == 0);
    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
    assert(pvr_list_finish() == 0);
}

int main(int argc, char **argv) {
    const pvr_init_params_t params = {
        .vertex_buf_size = 512 * 1024,
        .dma_enabled = 1,
        .opb_overflow_count = 1
    };
    const pvr_pass_config_t passes[3] = {
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } },
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } },
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } }
    };
    const float left[3] = { 40.0f, 230.0f, 420.0f };
    const float right[3] = { 220.0f, 410.0f, 600.0f };
    const uint32_t opaque_color[3] = {
        0xff2050d0, 0xff20b050, 0xffd03030
    };
    const uint32_t translucent_color[3] = {
        0x80a0c0ff, 0x80a0ffa0, 0xffffa0a0
    };
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t op_context;
    pvr_poly_cxt_t tr_context;
    pvr_poly_hdr_t op_header;
    pvr_poly_hdr_t tr_header;
    bool tested_duplicate = false;
    int event_handle;
    unsigned int frame;
    size_t pass;

    (void)argc;
    (void)argv;

    assert(pvr_init_multipass(&params, passes, 3) == 0);

    for(pass = 0; pass < 3; ++pass) {
        assert(pvr_set_pass_vertbuf_checked(
            pass, PVR_LIST_OP_POLY, pass_buffers[pass][0],
            sizeof(pass_buffers[pass][0]), NULL) == 0);
        assert(pvr_set_pass_vertbuf_checked(
            pass, PVR_LIST_TR_POLY, pass_buffers[pass][1],
            sizeof(pass_buffers[pass][1]), NULL) == 0);
    }

    event_handle = pvr_event_handler_add(
        PVR_EVENT_DMA_COMPLETE | PVR_EVENT_FAULT, observe_dma, NULL);
    assert(event_handle >= 0);

    pvr_poly_cxt_col(&op_context, PVR_LIST_OP_POLY);
    op_context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&op_header, &op_context);

    pvr_poly_cxt_col(&tr_context, PVR_LIST_TR_POLY);
    tr_context.gen.culling = PVR_CULLING_NONE;
    tr_context.gen.alpha = true;
    tr_context.blend.src = PVR_BLEND_SRCALPHA;
    tr_context.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&tr_header, &tr_context);

    for(frame = 0; frame < FRAME_COUNT; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        for(pass = 0; pass < 3; ++pass) {
            submit_panel(PVR_LIST_OP_POLY, &op_header, left[pass],
                         right[pass], 1.0f, opaque_color[pass]);
            submit_panel(PVR_LIST_TR_POLY, &tr_header, left[pass] + 25.0f,
                         right[pass] - 25.0f, 2.0f,
                         translucent_color[pass]);

            if(pass < 2) {
                assert(pvr_list_flush(PVR_LIST_OP_POLY) == 0);

                if(!tested_duplicate) {
                    errno = 0;
                    assert(pvr_list_flush(PVR_LIST_OP_POLY) == -1);
                    assert(errno == EALREADY);
                    tested_duplicate = true;
                }

                assert(pvr_scene_next_pass() == 0);
            }
        }

        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    assert(pvr_event_handler_remove(event_handle) == 0);
    assert(dma_faults == 0);
    assert(dma_completions == FRAME_COUNT * 6u);

    puts("RESULT: PASS (PVR multipass hybrid flushing)");
    pvr_shutdown();
    return 0;
}
