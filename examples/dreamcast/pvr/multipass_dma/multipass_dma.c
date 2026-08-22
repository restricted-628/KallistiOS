/* KallistiOS ##version##

   examples/dreamcast/pvr/multipass_dma/multipass_dma.c
   Copyright (C) 2026 Joseph Black

   Exercises pass-owned buffered registration and IRQ-chained continuation.
*/

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define PASS_BUFFER_SIZE (64 * 1024)

static alignas(32) uint8_t pass_buffers[3][PASS_BUFFER_SIZE];

static void submit_panel(const pvr_poly_hdr_t *header, float left,
                         float right, uint32_t color) {
    alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = left, .y = 80.0f, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = right, .y = 80.0f, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = left, .y = 400.0f, .z = 1.0f,
          .argb = color, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = right, .y = 400.0f, .z = 1.0f,
          .argb = color, .oargb = 0 }
    };

    assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
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
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } },
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } },
        { .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
                         PVR_BINSIZE_0, PVR_BINSIZE_0 } }
    };
    const float left[3] = { 40.0f, 230.0f, 420.0f };
    const float right[3] = { 220.0f, 410.0f, 600.0f };
    const uint32_t color[3] = { 0xff3070ff, 0xff30e070, 0xffff5040 };
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    unsigned int frame;
    size_t pass;

    (void)argc;
    (void)argv;

    assert(pvr_init_multipass(&params, passes, 3) == 0);

    for(pass = 0; pass < 3; ++pass) {
        assert(pvr_set_pass_vertbuf_checked(
            pass, PVR_LIST_OP_POLY, pass_buffers[pass],
            sizeof(pass_buffers[pass]), NULL) == 0);
    }

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        for(pass = 0; pass < 3; ++pass) {
            submit_panel(&header, left[pass], right[pass], color[pass]);

            if(pass + 1 < 3)
                assert(pvr_scene_next_pass() == 0);
        }

        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);

    puts("RESULT: PASS (PVR buffered multipass)");
    pvr_shutdown();
    return 0;
}
