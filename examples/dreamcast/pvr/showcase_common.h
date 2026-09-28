/* KallistiOS ##version##
   Small drawing helpers shared only by the interactive showcase examples.
   Copyright (C) 2026 Joseph Black
*/
#ifndef EXAMPLE_SHOWCASE_COMMON_H
#define EXAMPLE_SHOWCASE_COMMON_H
#ifdef NDEBUG
#error "Showcase examples require assertions: rendering calls are checked with assert()."
#endif
#include <kos.h>
#include <dc/sh4zam.h>
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static pvr_poly_hdr_t demo_color_header, demo_font_header;
static pvr_ptr_t demo_font;
static alignas(32) uint16_t demo_font_pixels[512 * 256];

static inline void demo_quad(float x, float y, float w, float h, float z,
                             uint32_t top, uint32_t bottom,
                             float u0, float v0, float u1, float v1) {
    pvr_vertex_t v[4] = {
        {.flags=PVR_CMD_VERTEX, .x=x, .y=y, .z=z, .u=u0, .v=v0, .argb=top},
        {.flags=PVR_CMD_VERTEX, .x=x+w, .y=y, .z=z, .u=u1, .v=v0, .argb=top},
        {.flags=PVR_CMD_VERTEX, .x=x, .y=y+h, .z=z, .u=u0, .v=v1, .argb=bottom},
        {.flags=PVR_CMD_VERTEX_EOL, .x=x+w, .y=y+h, .z=z, .u=u1, .v=v1, .argb=bottom}
    };
    assert(pvr_prim(v, sizeof(v)) == 0);
}

static inline void demo_init(void) {
    const pvr_init_params_t params = {
        .opb_sizes={PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, PVR_BINSIZE_16},
        .vertex_buf_size=1024 * 1024, .autosort_disabled=1,
        .opb_overflow_count=1
    };
    assert(pvr_init(&params) == 0);
    pvr_set_bg_color(.025f, .04f, .075f);
    pvr_poly_cxt_t c;
    pvr_poly_cxt_col(&c, PVR_LIST_OP_POLY);
    c.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&demo_color_header, &c);
    demo_font = pvr_mem_malloc(sizeof(demo_font_pixels));
    assert(demo_font);
    pvr_poly_cxt_txr(&c, PVR_LIST_TR_POLY,
        PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
        512, 256, demo_font, PVR_FILTER_NONE);
    c.gen.culling = PVR_CULLING_NONE;
    c.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    c.depth.write = PVR_DEPTHWRITE_DISABLE;
    pvr_poly_compile(&demo_font_header, &c);
}

static inline void demo_hud(const char *const lines[8]) {
    memset(demo_font_pixels, 0, sizeof(demo_font_pixels));
    for(unsigned i = 0; i < 8; ++i) {
        if(!lines[i]) continue;
        assert(strlen(lines[i]) * BFONT_THIN_WIDTH <= 512);
        bfont_draw_str_ex(demo_font_pixels + i * BFONT_HEIGHT * 512,
                         512, 0xffff, 0, 16, false, lines[i]);
    }
    assert(pvr_wait_render_done() == 0);
    pvr_txr_load(demo_font_pixels, demo_font, sizeof(demo_font_pixels));
}

static inline void demo_text(unsigned row, float x, float y, float scale,
                             uint32_t color) {
    demo_quad(x, y, 512 * scale, BFONT_HEIGHT * scale, 10, color, color,
              0, (float)(row * BFONT_HEIGHT) / 256,
              1, (float)((row + 1) * BFONT_HEIGHT) / 256);
}

static inline void demo_finish(void) {
    assert(pvr_wait_render_done() == 0);
    pvr_pipeline_status_t status;
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    pvr_mem_free(demo_font);
    assert(pvr_shutdown() == 0);
}
#endif
