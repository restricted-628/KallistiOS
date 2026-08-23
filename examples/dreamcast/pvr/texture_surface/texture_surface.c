/* KallistiOS ##version##

   examples/dreamcast/pvr/texture_surface/texture_surface.c
   Copyright (C) 2026 Joseph Black

   Exercises checked texture metadata, asynchronous transfers, and bounded
   partial updates.
*/

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_WIDTH 64u
#define TEXTURE_HEIGHT 64u
#define PATCH_WIDTH 16u
#define PATCH_HEIGHT 16u

static alignas(32) uint16_t pixels[TEXTURE_WIDTH * TEXTURE_HEIGHT];
static alignas(32) uint16_t roundtrip[TEXTURE_WIDTH * TEXTURE_HEIGHT];
static alignas(32) uint8_t yuv420[384];
static uint16_t patch[PATCH_WIDTH * PATCH_HEIGHT];

static void fill_checkerboard(void) {
    uint32_t x;
    uint32_t y;

    for(y = 0; y < TEXTURE_HEIGHT; ++y) {
        for(x = 0; x < TEXTURE_WIDTH; ++x) {
            pixels[y * TEXTURE_WIDTH + x] = ((x / 8u + y / 8u) & 1u)
                                                ? 0xffffu : 0x001fu;
        }
    }
}

static void fill_patch(uint16_t color) {
    uint32_t i;

    for(i = 0; i < PATCH_WIDTH * PATCH_HEIGHT; ++i)
        patch[i] = color;
}

static void draw_texture(const pvr_poly_hdr_t *header) {
    pvr_vertex_t vertex;

    pvr_prim(header, sizeof(*header));

    vertex.flags = PVR_CMD_VERTEX;
    vertex.x = 160.0f;
    vertex.y = 80.0f;
    vertex.z = 1.0f;
    vertex.u = 0.0f;
    vertex.v = 0.0f;
    vertex.argb = 0xffffffff;
    vertex.oargb = 0;
    pvr_prim(&vertex, sizeof(vertex));

    vertex.x = 480.0f;
    vertex.u = 1.0f;
    pvr_prim(&vertex, sizeof(vertex));

    vertex.x = 160.0f;
    vertex.y = 400.0f;
    vertex.u = 0.0f;
    vertex.v = 1.0f;
    pvr_prim(&vertex, sizeof(vertex));

    vertex.flags = PVR_CMD_VERTEX_EOL;
    vertex.x = 480.0f;
    vertex.u = 1.0f;
    pvr_prim(&vertex, sizeof(vertex));
}

static void draw_panel(const pvr_poly_hdr_t *header, float width,
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
    pvr_txr_surface_t surface;
    pvr_txr_surface_t temporary;
    pvr_txr_surface_t bound;
    pvr_txr_surface_t mipmap;
    pvr_txr_surface_t vq;
    pvr_txr_surface_t yuv;
    pvr_txr_level_info_t level;
    pvr_txr_request_t *request;
    pvr_txr_request_status_t request_status;
    pvr_render_ticket_t render_ticket;
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t context;
    pvr_poly_cxt_t render_context;
    pvr_poly_hdr_t header;
    pvr_poly_hdr_t render_header;
    uint32_t texture_format;
    size_t yuv_input_size;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);
    fill_checkerboard();

    assert(pvr_txr_surface_alloc(&surface, TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                 PVR_TXR_SURFACE_RGB565,
                                 PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    assert(pvr_txr_load_ex_checked(pixels, surface.vram, TEXTURE_WIDTH,
                                   TEXTURE_HEIGHT, PVR_TXRLOAD_16BPP) == 0);

    texture_format = pvr_txr_surface_pvr_format(&surface);
    assert(texture_format == PVR_TXRFMT_RGB565);
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, texture_format,
                     TEXTURE_WIDTH, TEXTURE_HEIGHT, surface.vram,
                     PVR_FILTER_BILINEAR);
    pvr_poly_compile(&header, &context);
    pvr_poly_cxt_col(&render_context, PVR_LIST_OP_POLY);
    render_context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&render_header, &render_context);

    assert(pvr_txr_surface_alloc(&temporary, TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                 PVR_TXR_SURFACE_RGB565,
                                 PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_upload_async(&temporary, pixels, sizeof(pixels),
                                        &request) == 0);
    assert(pvr_txr_request_wait(request, 1000, &request_status) == 0);
    assert(request_status.state == PVR_TXR_REQUEST_COMPLETE);
    assert(request_status.requested_bytes == sizeof(pixels));
    assert(request_status.completed_bytes == sizeof(pixels));
    assert(pvr_txr_request_destroy(request) == 0);
    assert(pvr_txr_surface_readback(&temporary, roundtrip,
                                    sizeof(roundtrip)) == 0);
    assert(memcmp(roundtrip, pixels, sizeof(roundtrip)) == 0);
    memset(roundtrip, 0, sizeof(roundtrip));
    assert(pvr_txr_surface_readback_level(&temporary, 0, roundtrip,
                                          sizeof(roundtrip)) == 0);
    assert(memcmp(roundtrip, pixels, sizeof(roundtrip)) == 0);
    memset(roundtrip, 0, sizeof(roundtrip));
    assert(pvr_txr_surface_readback_part(&temporary, 32, roundtrip, 32) == 0);
    assert(memcmp(roundtrip, (const uint8_t *)pixels + 32, 32) == 0);
    assert(pvr_txr_surface_pvr_format(&temporary)
           == (PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED));
    assert(pvr_txr_surface_bind(&bound, temporary.vram, temporary.capacity,
                                TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(!bound.owns_vram && bound.vram == temporary.vram);
    pvr_txr_surface_release(&bound);

    assert(pvr_txr_surface_alloc(&yuv, 16, 16, PVR_TXR_SURFACE_YUV422,
                                 PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_yuv_input_size(&yuv, PVR_TXR_YUV420,
                                          &yuv_input_size) == 0);
    assert(yuv_input_size == sizeof(yuv420));
    memset(yuv420, 128, 128);
    memset(yuv420 + 128, 180, 256);
    assert(pvr_txr_surface_yuv_upload_async(&yuv, PVR_TXR_YUV420, yuv420,
                                            sizeof(yuv420), &request) == 0);
    assert(pvr_txr_request_wait(request, 1000, &request_status) == 0);
    assert(request_status.state == PVR_TXR_REQUEST_COMPLETE);
    assert(request_status.completed_bytes == sizeof(yuv420));
    assert(request_status.requested_macroblocks == 1);
    assert(request_status.completed_macroblocks == 1);
    assert(pvr_txr_request_destroy(request) == 0);
    pvr_txr_surface_release(&yuv);

    assert(pvr_txr_surface_init(&mipmap, 8, 8,
                                PVR_TXR_SURFACE_ARGB1555,
                                PVR_TXR_SURFACE_TWIDDLED, true) == 0);
    assert(mipmap.byte_size == 176 && mipmap.mip_levels == 4);
    assert(pvr_txr_surface_get_level(&mipmap, 3, &level) == 0);
    assert(level.width == 1 && level.height == 1);
    assert(level.offset == 6 && level.byte_size == 2);

    assert(pvr_txr_surface_init(&vq, 64, 64, PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_VQ, false) == 0);
    assert(vq.codebook_size == 2048 && vq.byte_size == 3072);

    errno = 0;
    assert(pvr_txr_surface_get_level(&surface, 1, &level) == -1);
    assert(errno == ERANGE);
    errno = 0;
    assert(pvr_txr_surface_upload_part(&surface, surface.byte_size - 1u,
                                       patch, 2, PVR_TXR_TRANSFER_CPU) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_txr_surface_upload_codebook(&surface, pixels, 2048,
                                           PVR_TXR_TRANSFER_CPU) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_txr_load_ex_checked(pixels, surface.vram, TEXTURE_WIDTH,
                                   TEXTURE_HEIGHT,
                                   PVR_TXRLOAD_16BPP
                                   | PVR_TXRLOAD_NONBLOCK) == -1);
    assert(errno == ENOTSUP);
    errno = 0;
    assert(pvr_txr_surface_bind(&bound, pixels, sizeof(pixels), TEXTURE_WIDTH,
                                TEXTURE_HEIGHT, PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == -1);
    assert(errno == EFAULT);
    bound = temporary;
    bound.vram = (pvr_ptr_t)pixels;
    bound.owns_vram = false;
    errno = 0;
    assert(pvr_txr_surface_readback(&bound, roundtrip,
                                    sizeof(roundtrip)) == -1);
    assert(errno == EFAULT);
    memset(&bound, 0, sizeof(bound));
    errno = 0;
    assert(pvr_txr_surface_readback_part(&temporary,
                                         temporary.byte_size - 1u,
                                         roundtrip, 2) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_txr_surface_begin_render(&surface, TEXTURE_WIDTH,
                                        TEXTURE_HEIGHT) == -1);
    assert(errno == ENOTSUP);
    errno = 0;
    assert(pvr_txr_surface_begin_render(&temporary, TEXTURE_WIDTH + 1u,
                                        TEXTURE_HEIGHT) == -1);
    assert(errno == EINVAL);

    assert(pvr_wait_ready() == 0);
    pvr_set_bg_color(1.0f, 0.0f, 0.0f);
    assert(pvr_txr_surface_begin_render(&temporary, TEXTURE_WIDTH,
                                        TEXTURE_HEIGHT) == 0);
    draw_panel(&render_header, TEXTURE_WIDTH, TEXTURE_HEIGHT, 0xffff0000u);
    assert(pvr_scene_finish_tracked(&render_ticket) == 0);
    assert(render_ticket.target == temporary.vram);
    assert(render_ticket.width == TEXTURE_WIDTH);
    assert(render_ticket.height == TEXTURE_HEIGHT);
    assert(render_ticket.stride == TEXTURE_WIDTH);
    assert(pvr_render_ticket_wait(&render_ticket,
                                  PVR_RENDER_STAGE_COMPLETE, 1000) == 0);

    pvr_txr_surface_release(&temporary);

    for(frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        fill_patch((frame & 16u) ? 0xf800u : 0x07e0u);
        assert(pvr_txr_surface_upload_rect(&surface, 0, 24, 24,
                                           PATCH_WIDTH, PATCH_HEIGHT, patch,
                                           PATCH_WIDTH * sizeof(uint16_t)) == 0);

        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        draw_texture(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_ready() == 0);
    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);

    pvr_txr_surface_release(&surface);
    puts("RESULT: PASS (PVR texture surface)");
    pvr_shutdown();
    return 0;
}
