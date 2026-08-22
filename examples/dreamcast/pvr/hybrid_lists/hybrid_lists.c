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
    pvr_poly_cxt_t op_context;
    pvr_poly_cxt_t tr_context;
    pvr_poly_hdr_t op_header;
    pvr_poly_hdr_t tr_header;
    bool tested_duplicate = false;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init(&pvr_params) == 0);
    assert(pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_buffer,
                           sizeof(tr_buffer)) == NULL);

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

    puts("RESULT: PASS (buffered/direct list submission)");
    pvr_shutdown();

    return 0;
}
