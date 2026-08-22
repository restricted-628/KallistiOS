/* KallistiOS ##version##

   examples/dreamcast/pvr/material_state/material_state.c
   Copyright (C) 2026 Joseph Black

   Exercises checked global PVR state and extended header compilation.
*/

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define VERTEX_BUFFER_SIZE (64 * 1024)

static alignas(32) uint8_t vertex_buffer[VERTEX_BUFFER_SIZE];

static void submit_rectangle(const pvr_poly_hdr_t *header) {
    alignas(32) const pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = 80.0f, .y = 80.0f, .z = 1.0f,
          .argb = 0xffffc040, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 560.0f, .y = 80.0f, .z = 1.0f,
          .argb = 0xffffc040, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX, .x = 80.0f, .y = 400.0f, .z = 1.0f,
          .argb = 0xffffc040, .oargb = 0 },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 560.0f, .y = 400.0f,
          .z = 1.0f, .argb = 0xffffc040, .oargb = 0 }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

static void check_header_controls(pvr_ptr_t texture) {
    pvr_sprite_cxt_t sprite_context;
    pvr_sprite_hdr_t sprite_header;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                     8, 8, texture, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&header, &context);
    assert(!(header.mode2 & PVR_TA_PM2_SUPERSAMPLE));
    pvr_poly_compile_ex(&header, &context, PVR_COMPILE_SUPERSAMPLE);
    assert(header.mode2 & PVR_TA_PM2_SUPERSAMPLE);

    pvr_sprite_cxt_txr(&sprite_context, PVR_LIST_OP_POLY,
                       PVR_TXRFMT_RGB565, 8, 8, texture,
                       PVR_FILTER_BILINEAR);
    pvr_sprite_compile_ex(&sprite_header, &sprite_context,
                          PVR_COMPILE_SUPERSAMPLE);
    assert(sprite_header.mode2 & PVR_TA_PM2_SUPERSAMPLE);

    pvr_poly_cxt_txr_mod(&context, PVR_LIST_OP_POLY,
                         PVR_TXRFMT_RGB565, 8, 8, texture,
                         PVR_FILTER_BILINEAR, PVR_TXRFMT_RGB565,
                         8, 8, texture, PVR_FILTER_BILINEAR);
    pvr_poly_mod_compile_ex(&header, &context,
                            PVR_COMPILE_SUPERSAMPLE_2);
    assert(!(header.mode2_0 & PVR_TA_PM2_SUPERSAMPLE));
    assert(header.mode2_1 & PVR_TA_PM2_SUPERSAMPLE);
}

int main(int argc, char **argv) {
    const pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
                       PVR_BINSIZE_0, PVR_BINSIZE_0 },
        .vertex_buf_size = 256 * 1024,
        .dma_enabled = 1,
        .opb_overflow_count = 1
    };
    const uint32_t palette[] = {
        0xff102030, 0xff405060, 0xff708090, 0xffa0b0c0
    };
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_ptr_t texture;
    uint32_t minimum;
    uint32_t maximum;
    uint8_t threshold;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init(&params) == 0);
    assert(pvr_set_vertbuf_checked(PVR_LIST_OP_POLY, vertex_buffer,
                                   sizeof(vertex_buffer), NULL) == 0);

    errno = 0;
    assert(pvr_set_vertbuf_checked(PVR_LIST_OP_POLY, vertex_buffer + 1,
                                   sizeof(vertex_buffer), NULL) == -1);
    assert(errno == EINVAL);

    assert(pvr_set_color_clamp(0x00000000, 0xff80ffff) == 0);
    assert(pvr_get_color_clamp(&minimum, &maximum) == 0);
    assert(minimum == 0x00000000);
    assert(maximum == 0xff80ffff);

    errno = 0;
    assert(pvr_get_color_clamp(&minimum, &minimum) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_set_color_clamp(0x00000080, 0x00000040) == -1);
    assert(errno == EINVAL);

    assert(pvr_set_punch_through_alpha(0x80) == 0);
    assert(pvr_get_punch_through_alpha(&threshold) == 0);
    assert(threshold == 0x80);
    errno = 0;
    assert(pvr_set_punch_through_alpha(256) == -1);
    assert(errno == EINVAL);

    assert(pvr_set_pal_entries(64, palette,
                               sizeof(palette) / sizeof(palette[0])) == 0);
    assert(pvr_set_pal_entries(64, NULL, 0) == 0);
    assert(PVR_GET(PVR_PALETTE_TABLE_BASE + 4 * 64) == palette[0]);
    assert(PVR_GET(PVR_PALETTE_TABLE_BASE + 4 * 67) == palette[3]);
    errno = 0;
    assert(pvr_set_pal_entries(1023, palette, 2) == -1);
    assert(errno == EINVAL);

    texture = pvr_mem_malloc(8 * 8 * 2);
    assert(texture);
    check_header_controls(texture);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.color_clamp = true;
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();

        if(frame == 0) {
            errno = 0;
            assert(pvr_set_vertbuf_checked(PVR_LIST_OP_POLY, vertex_buffer,
                                           sizeof(vertex_buffer), NULL) == -1);
            assert(errno == EBUSY);
        }

        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        submit_rectangle(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    vid_waitvbl();
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);

    pvr_mem_free(texture);
    puts("RESULT: PASS (checked PVR material state)");
    pvr_shutdown();
    return 0;
}
