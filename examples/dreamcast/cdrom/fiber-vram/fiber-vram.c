/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Disc -> allocated texture RAM, with application-owned cooperative fibers.
   No RAM payload staging, service executor, or BIOS fallback.
*/
#include <kos.h>
#include <kos/fiber_disc.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "texture-pattern.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define GUARD_BYTES 32u
#define SECTORS (TEXTURE_BYTES / 2048u)
_Static_assert(TEXTURE_BYTES % 2048u == 0, "asset must fill whole sectors");
_Alignas(32) static uint8_t loader_stack[8192], sibling_stack[8192];
static fiber_disc_t *disc_io;
static kfiber_t *main_fiber;
static pvr_ptr_t texture;
static uint32_t fad;
static gdrom_direct_sector_type_t sector_type;
static bool finished, failed;
static unsigned int sibling_steps;

static void loader(void *unused) {
    cdrom_request_status_t status;
    fiber_disc_read_t *read;
    (void)unused;
    read = fiber_disc_read_dma(disc_io, texture, fad, SECTORS, sector_type, 10000);
    if(!read) {
        perror("VRAM DMA submit");
        failed = finished = true;
        return;
    }
    /* Parks only this fiber. Retirement includes callback completion. */
    if(fiber_disc_await(read, &status) < 0)
        arch_panic("VRAM wait failed with live resources");
    failed |= status.state != CDROM_REQUEST_COMPLETE
        || status.completed_bytes != TEXTURE_BYTES;
    printf("FIBER-VRAM: read state=%d error=%d bytes=%zu\n",
           status.state, status.error, status.completed_bytes);
    if(fiber_disc_read_destroy(read) < 0)
        arch_panic("VRAM request release failed");
    finished = true;
}

static void sibling(void *unused) {
    (void)unused;
    while(!finished) {
        ++sibling_steps; /* One bounded piece of unrelated application work. */
        if(fiber_switch(main_fiber) < 0)
            arch_panic("VRAM sibling yield failed");
    }
}

static int locate_texture(void) {
    iso9660_file_info_t info;
    int drive, disc_type;
    /* Blocking metadata setup happens before child fibers are dispatched. */
    if(cdrom_get_status(&drive, &disc_type) < 0
            || fs_iso9660_get_path_info("/cd/texture.bin", &info) < 0)
        return -1;
    /* A single raw DMA read cannot interpret interleaving/extended records.
       Reject unsupported layouts instead of reading adjacent unrelated data. */
    if(info.size != TEXTURE_BYTES || info.sector_count != SECTORS
            || info.extent_fad < 150 || info.ext_attr_length
            || info.file_unit_size || info.interleave_gap
            || (info.flags & ~ISO9660_FILE_HIDDEN)) {
        errno = EINVAL;
        return -1;
    }
    fad = info.extent_fad;
    sector_type = disc_type == CD_CDROM_XA || disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1 : GDROM_DIRECT_SECTOR_MODE1;
    return 0;
}

static bool verify_texture(pvr_ptr_t allocation) {
    volatile const uint16_t *pixels = texture;
    volatile const uint16_t *before = allocation;
    volatile const uint16_t *after = (uint16_t *)texture + TEXTURE_BYTES / 2;
    for(unsigned int i = 0; i < GUARD_BYTES / 2; ++i)
        if(before[i] != 0x5aa5 || after[i] != 0xa55a) {
            puts("FIBER-VRAM: guard corruption");
            return false;
        }
    for(unsigned int y = 0; y < TEXTURE_HEIGHT; ++y)
        for(unsigned int x = 0; x < TEXTURE_WIDTH; ++x)
            if(pixels[y * TEXTURE_WIDTH + x] != texture_pixel(x, y)) {
                printf("FIBER-VRAM: pixel mismatch at %u,%u\n", x, y);
                return false;
            }
    return true;
}

static int draw_texture(void) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = 160, .y = 80, .z = 1,
          .u = 0, .v = 0, .argb = 0xffffffff },
        { .flags = PVR_CMD_VERTEX, .x = 480, .y = 80, .z = 1,
          .u = 1, .v = 0, .argb = 0xffffffff },
        { .flags = PVR_CMD_VERTEX, .x = 160, .y = 400, .z = 1,
          .u = 0, .v = 1, .argb = 0xffffffff },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 480, .y = 400, .z = 1,
          .u = 1, .v = 1, .argb = 0xffffffff }
    };
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                    PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                    TEXTURE_WIDTH, TEXTURE_HEIGHT, texture, PVR_FILTER_NONE);
    pvr_poly_compile(&header, &context);
    for(unsigned int frame = 0; frame < 180; ++frame) {
        if(pvr_wait_ready() < 0) return -1;
        pvr_scene_begin();
        if(pvr_list_begin(PVR_LIST_OP_POLY) < 0
                || pvr_prim(&header, sizeof(header)) < 0
                || pvr_prim(vertices, sizeof(vertices)) < 0
                || pvr_list_finish() < 0 || pvr_scene_finish() < 0)
            return -1;
    }
    return 0;
}

int main(void) {
    kfiber_t *fibers[2];
    pvr_ptr_t allocation;
    pvr_pipeline_status_t pipeline;
    bool stopping = false;
    uint64_t deadline;

    if(locate_texture() < 0) {
        perror("FIBER-VRAM: /cd/texture.bin (see README)");
        return EXIT_FAILURE;
    }
    if(pvr_init_defaults() < 0) return EXIT_FAILURE;
    allocation = pvr_mem_malloc(TEXTURE_BYTES + 2 * GUARD_BYTES);
    if(!allocation) {
        pvr_shutdown();
        return EXIT_FAILURE;
    }
    /* Keep the allocator's texture aperture. Do not convert to the other
       VRAM bus aperture: its interleaving is different. Both guards are ours. */
    texture = (uint8_t *)allocation + GUARD_BYTES;
    volatile uint16_t *before = allocation;
    volatile uint16_t *after = (uint16_t *)texture + TEXTURE_BYTES / 2;
    for(unsigned int i = 0; i < GUARD_BYTES / 2; ++i) {
        before[i] = 0x5aa5;
        after[i] = 0xa55a;
    }
    main_fiber = fiber_attach();
    disc_io = fiber_disc_create(1);
    fibers[0] = fiber_create(loader_stack, sizeof(loader_stack), loader, NULL);
    fibers[1] = fiber_create(sibling_stack, sizeof(sibling_stack), sibling, NULL);
    if(!main_fiber || !disc_io || !fibers[0] || !fibers[1])
        arch_panic("VRAM fiber setup failed");
    deadline = timer_ms_gettime64() + 30000;
    /* No render submission, upload, or other DMA touches this allocation. */
    while(fiber_get_state(fibers[0]) != KFIBER_STATE_FINISHED
            || fiber_get_state(fibers[1]) != KFIBER_STATE_FINISHED) {
        bool ready = false;
        if(timer_ms_gettime64() >= deadline) {
            if(stopping) arch_panic("VRAM cleanup deadline exceeded");
            if(fiber_disc_shutdown(disc_io) < 0)
                arch_panic("VRAM cancellation failed");
            stopping = failed = true;
            deadline = timer_ms_gettime64() + 15000;
        }
        if(fiber_disc_pump(disc_io) < 0) arch_panic("VRAM completion pump failed");
        for(unsigned int i = 0; i < 2; ++i)
            if(fiber_get_state(fibers[i]) == KFIBER_STATE_READY
                    && fiber_switch(fibers[i]) < 0)
                arch_panic("VRAM fiber dispatch failed");
        for(unsigned int i = 0; i < 2; ++i)
            ready |= fiber_get_state(fibers[i]) == KFIBER_STATE_READY;
        if(ready) thd_pass();
        else if(!finished && fiber_disc_idle(disc_io, 20) < 0 && errno != ETIMEDOUT)
            arch_panic("VRAM owner idle failed");
    }
    if(fiber_destroy(fibers[0]) < 0 || fiber_destroy(fibers[1]) < 0
            || fiber_disc_destroy(disc_io) < 0)
        arch_panic("VRAM fiber cleanup failed");

    /* DMA and callback have retired. Uncached VRAM needs no dcache flush. */
    if(!verify_texture(allocation)) failed = true;
    if(!failed) {
        puts("FIBER-VRAM: bytes/guards PASS; rendering texture");
        if(draw_texture() < 0)
            arch_panic("VRAM render submission failed; texture still owned");
        /* pvr_wait_ready alone is not a last-frame texture lifetime fence. */
        if(pvr_wait_render_done() < 0)
            arch_panic("VRAM render fence failed; texture still owned");
        if(pvr_get_pipeline_status(&pipeline) < 0
                || pipeline.faults.mask != PVR_FAULT_NONE)
            failed = true;
    }
    pvr_mem_free(allocation);
    if(pvr_shutdown() < 0) failed = true;
    printf("FIBER-VRAM: %s sibling-steps=%u\n", failed ? "FAIL" : "PASS", sibling_steps);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
