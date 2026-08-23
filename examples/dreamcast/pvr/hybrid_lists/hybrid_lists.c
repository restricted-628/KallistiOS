/* KallistiOS ##version##

   examples/dreamcast/pvr/hybrid_lists/hybrid_lists.c
   Copyright (C) 2026 Joseph Black

   Demonstrates mixing a buffered translucent list with direct opaque-list
   submission in the same scene.
*/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

#define TR_BUFFER_SIZE (64 * 1024)

KOS_INIT_FLAGS(INIT_DEFAULT);

static uint8_t tr_buffer[TR_BUFFER_SIZE] __attribute__((aligned(32)));
static volatile uint32_t dma_completions;
static volatile uint32_t dma_faults;

static const pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
      PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,
    1,
    0,
    0,
    2,
    1
};

static void observe_dma(pvr_event_t event, uint32_t detail, void *user_data) {
    (void)user_data;

    if(event == PVR_EVENT_DMA_COMPLETE && detail == 0)
        ++dma_completions;
    else if(event == PVR_EVENT_FAULT)
        ++dma_faults;
}

static void submit_triangle(const pvr_poly_hdr_t *header, uint32_t color,
                            float left, float top, float right, float bottom,
                            float z) {
    pvr_vertex_t vertices[3] __attribute__((aligned(32))) = {
        { .flags = PVR_CMD_VERTEX, .x = left, .y = bottom, .z = z,
          .u = 0.0f, .v = 0.0f, .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = (left + right) * 0.5f, .y = top,
          .z = z, .u = 0.0f, .v = 0.0f, .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = right, .y = bottom, .z = z,
          .u = 0.0f, .v = 0.0f, .argb = color, .oargb = 0 }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    static const uint8_t full_block[32] __attribute__((aligned(32)));
    pvr_poly_cxt_t op_context;
    pvr_poly_cxt_t tr_context;
    pvr_poly_hdr_t op_header;
    pvr_poly_hdr_t tr_header;
    bool tested_duplicate = false;
    int event_handle;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init(&pvr_params) == 0);
    assert(pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_buffer,
                           sizeof(tr_buffer)) == NULL);
    event_handle = pvr_event_handler_add(
        PVR_EVENT_DMA_COMPLETE | PVR_EVENT_FAULT, observe_dma, NULL);
    assert(event_handle >= 0);

    pvr_poly_cxt_col(&op_context, PVR_LIST_OP_POLY);
    pvr_poly_compile(&op_header, &op_context);

    pvr_poly_cxt_col(&tr_context, PVR_LIST_TR_POLY);
    tr_context.gen.alpha = true;
    tr_context.blend.src = PVR_BLEND_SRCALPHA;
    tr_context.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&tr_header, &tr_context);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        submit_triangle(&tr_header, 0x800000ff, 250.0f, 120.0f,
                        520.0f, 390.0f, 2.0f);
        assert(pvr_list_finish() == 0);
        assert(pvr_list_flush(PVR_LIST_TR_POLY) == 0);

        if(!tested_duplicate) {
            errno = 0;
            assert(pvr_list_flush(PVR_LIST_TR_POLY) == -1);
            assert(errno == EALREADY);
            tested_duplicate = true;
        }

        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        submit_triangle(&op_header, 0xffff0000, 120.0f, 90.0f,
                        410.0f, 400.0f, 1.0f);
        assert(pvr_list_finish() == 0);

        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();
    assert(pvr_event_handler_remove(event_handle) == 0);
    assert(dma_completions == 120);
    assert(dma_faults == 0);

    /* Fill the active RAM half completely. Finishing must reject the scene
       before appending its required 32-byte EOL rather than overrunning the
       caller-owned allocation. Shutdown below intentionally abandons this
       rejected scene. */
    pvr_scene_begin();
    for(frame = 0; frame < TR_BUFFER_SIZE / 2 / sizeof(full_block); ++frame) {
        assert(pvr_list_prim(PVR_LIST_TR_POLY, full_block,
                             sizeof(full_block)) == 0);
    }

    errno = 0;
    assert(pvr_scene_finish() == -1);
    assert(errno == ENOSPC);

    puts("RESULT: PASS (buffered/direct list submission)");
    pvr_shutdown();

    return 0;
}
