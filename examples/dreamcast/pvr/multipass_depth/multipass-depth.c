/* KallistiOS ##version##

   Numeric multipass depth/color boundary fixture.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

/* Checks stay active in release builds: submission must not live in assert. */
#define CHECK(test) do { \
    if(!(test)) { \
        printf("FAIL line %d: %s (errno=%d)\n", __LINE__, #test, errno); \
        exit(1); \
    } \
} while(0)

static alignas(32) uint8_t staging[3][4096];
static const pvr_pass_config_t passes[3] = {
    { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } },
    { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } },
    { .opb_sizes = { PVR_BINSIZE_16, 0, 0, 0, 0 } }
};

static void quad(const pvr_poly_hdr_t *header, float x0, float y0,
                 float x1, float y1, float depth, uint32_t color) {
    const alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = x0, .y = y0, .z = depth,
          .argb = color },
        { .flags = PVR_CMD_VERTEX, .x = x1, .y = y0, .z = depth,
          .argb = color },
        { .flags = PVR_CMD_VERTEX, .x = x0, .y = y1, .z = depth,
          .argb = color },
        { .flags = PVR_CMD_VERTEX_EOL, .x = x1, .y = y1, .z = depth,
          .argb = color }
    };
    CHECK(pvr_prim(header, sizeof(*header)) == 0);
    CHECK(pvr_prim(vertices, sizeof(vertices)) == 0);
}

static unsigned pixel(const vid_framebuffer_info_t *fb, unsigned x, unsigned y,
                       uint16_t expected) {
    const volatile uint16_t *address = (const volatile uint16_t *)
        ((const uint8_t *)fb->address + y * fb->stride_bytes + x * 2);
    const uint16_t actual = *address;
    printf("pixel %u,%u: %04x expected %04x\n", x, y, actual, expected);
    return actual != expected;
}

static void check_regions(unsigned policy) {
    /* Read-only diagnostic of the array actually submitted to the ISP, after
       rendering is idle. Use the uncached 32-bit VRAM view, as the driver does.
       This distinguishes an encoding failure from a rasterizer limitation. */
    const uint32_t address = PVR_GET(PVR_ISP_TILEMAT_ADDR);
    CHECK(address < 8 * 1024 * 1024 - (6 + 20 * 15 * 3 * 6) * 4);
    const volatile uint32_t *regions =
        (const volatile uint32_t *)(PVR_RAM_BASE + address);
    CHECK(regions[0] == 0x10000000);
    for(unsigned x = 0; x < 20; ++x) {
        for(unsigned y = 0; y < 15; ++y) {
            for(unsigned pass = 0; pass < 3; ++pass) {
                uint32_t expected = (y << 8) | (x << 2);
                if(pass && !(pass == 1 && policy == 2))
                    expected |= 0x40000000;
                if(pass < 2)
                    expected |= 0x10000000;
                if(x == 19 && y == 14 && pass == 2)
                    expected |= 0x80000000;
                CHECK(regions[6 + ((x * 15 + y) * 3 + pass) * 6] == expected);
            }
        }
    }
    printf("region controls PASS: %08lx %08lx %08lx\n",
           (unsigned long)regions[6], (unsigned long)regions[12],
           (unsigned long)regions[18]);
}

static unsigned run_case(unsigned mode, unsigned policy) {
    const pvr_init_params_t params = {
        .vertex_buf_size = 512 * 1024,
        .dma_enabled = mode != 0,
        .opb_overflow_count = 1
    };
    pvr_pass_depth_t depth[3] = {
        PVR_PASS_DEPTH_CLEAR,
        policy == 2 ? PVR_PASS_DEPTH_CLEAR : PVR_PASS_DEPTH_PRESERVE,
        PVR_PASS_DEPTH_PRESERVE
    };
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_pipeline_status_t status;
    vid_framebuffer_info_t fb;
    printf("case mode=%u (direct/DMA/hybrid), policy=%u (legacy/preserve/clear)\n",
           mode, policy);

    if(policy == 0)
        CHECK(pvr_init_multipass(&params, passes, 3) == 0);
    else
        CHECK(pvr_init_multipass_depth(&params, passes, depth, 3) == 0);

    /* The initializer copies policies; later caller changes have no effect. */
    depth[1] = (pvr_pass_depth_t)99;
    if(mode) {
        for(size_t pass = 0; pass < 3; ++pass)
            CHECK(pvr_set_pass_vertbuf_checked(pass, PVR_LIST_OP_POLY,
                    staging[pass], sizeof(staging[pass]), NULL) == 0);
    }
    pvr_set_bg_color(0, 0, 1);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    context.depth.write = PVR_DEPTHWRITE_ENABLE;
    pvr_poly_compile(&header, &context);

    for(unsigned frame = 0; frame < 12; ++frame) {
        CHECK(pvr_wait_ready() == 0);
        pvr_scene_begin();
        for(size_t pass = 0; pass < 3; ++pass) {
            CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
            if(pass == 0)
                quad(&header, 80, 80, 560, 400, 4, 0xffff0000);
            else if(pass == 1)
                quad(&header, 160, 120, 480, 360, 2, 0xff00ff00);
            else {
                /* The far yellow panel must fail against either red or
                   green: this catches accidentally clearing every pass. */
                quad(&header, 240, 180, 400, 300, 1, 0xffffff00);
                quad(&header, 500, 120, 540, 360, 8, 0xff00ffff);
            }
            CHECK(pvr_list_finish() == 0);
            if(pass + 1 < 3) {
                if(mode == 2)
                    CHECK(pvr_list_flush(PVR_LIST_OP_POLY) == 0);
                CHECK(pvr_scene_next_pass() == 0);
            }
        }
        CHECK(pvr_scene_finish() == 0);
    }
    CHECK(pvr_wait_ready() == 0);
    CHECK(pvr_wait_render_done() == 0);
    vid_waitvbl();
    CHECK(pvr_get_pipeline_status(&status) == 0);
    CHECK(status.faults.mask == PVR_FAULT_NONE);
    check_regions(policy);
    CHECK(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED, &fb) == 0);
    CHECK(fb.pixel_mode == PM_RGB565 && fb.width == 640 && fb.height == 480);
    unsigned failures = pixel(&fb, 10, 10, 0x001f);
    failures += pixel(&fb, 100, 100, 0xf800);
    failures += pixel(&fb, 320, 240, policy == 2 ? 0x07e0 : 0xf800);
    failures += pixel(&fb, 520, 240, 0x07ff);
    failures += pixel(&fb, 550, 390, 0xf800);
    printf("case pixel mismatches: %u\n", failures);
    CHECK(pvr_shutdown() == 0);
    return failures;
}

int main(void) {
    const pvr_init_params_t params = {
        .vertex_buf_size = 512 * 1024, .opb_overflow_count = 1
    };
    pvr_pass_depth_t depth[3] = {
        PVR_PASS_DEPTH_PRESERVE, PVR_PASS_DEPTH_CLEAR, PVR_PASS_DEPTH_CLEAR
    };
    /* Invalid plans must be rejected before the live video state is cleared. */
    volatile uint16_t *sentinel = vram_s;
    *sentinel = 0x1234;
    errno = 0;
    CHECK(pvr_init_multipass_depth(&params, passes, depth, 3) == -1);
    CHECK(errno == EINVAL && *sentinel == 0x1234);
    depth[0] = PVR_PASS_DEPTH_CLEAR;
    depth[2] = (pvr_pass_depth_t)99;
    errno = 0;
    CHECK(pvr_init_multipass_depth(&params, passes, depth, 3) == -1);
    CHECK(errno == EINVAL && *sentinel == 0x1234);
    errno = 0;
    CHECK(pvr_init_multipass_depth(&params, passes, NULL, 3) == -1);
    CHECK(errno == EINVAL && *sentinel == 0x1234);
    unsigned failures = 0;
    for(unsigned mode = 0; mode < 3; ++mode)
        for(unsigned policy = 0; policy < 3; ++policy)
            failures += run_case(mode, policy);
    printf("total pixel mismatches: %u/45\n", failures);
    CHECK(failures == 0);
    puts("RESULT: PASS (multipass depth/color pixels, direct/DMA/hybrid)");
    return 0;
}
