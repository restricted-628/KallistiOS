/* KallistiOS ##version##

   examples/dreamcast/pvr/texture_surface/texture_surface.c
   Copyright (C) 2026 Joseph Black

   Exercises checked texture metadata and bounded partial updates.
*/

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_WIDTH 64u
#define TEXTURE_HEIGHT 64u
#define PATCH_WIDTH 16u
#define PATCH_HEIGHT 16u

static alignas(32) uint16_t pixels[TEXTURE_WIDTH * TEXTURE_HEIGHT];
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

int main(int argc, char **argv) {
    pvr_txr_surface_t surface;
    pvr_txr_surface_t temporary;
    pvr_txr_surface_t bound;
    pvr_txr_surface_t mipmap;
    pvr_txr_surface_t vq;
    pvr_txr_level_info_t level;
    pvr_pipeline_status_t status;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    uint32_t texture_format;
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

    assert(pvr_txr_surface_alloc(&temporary, TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                 PVR_TXR_SURFACE_RGB565,
                                 PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_upload(&temporary, pixels, sizeof(pixels),
                                  PVR_TXR_TRANSFER_DMA) == 0);
    assert(pvr_txr_surface_pvr_format(&temporary)
           == (PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED));
    assert(pvr_txr_surface_bind(&bound, temporary.vram, temporary.capacity,
                                TEXTURE_WIDTH, TEXTURE_HEIGHT,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(!bound.owns_vram && bound.vram == temporary.vram);
    pvr_txr_surface_release(&bound);
    pvr_txr_surface_release(&temporary);

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
