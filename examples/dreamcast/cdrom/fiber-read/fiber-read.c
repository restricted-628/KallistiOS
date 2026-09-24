/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <kos/fiber_disc.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES (8u * 2048u)
_Alignas(32) static uint8_t data[BYTES], reference[BYTES];
_Alignas(32) static uint8_t loader_stack[8192], sibling_stack[8192];
static fiber_disc_t *disc_io;
static fiber_disc_read_t *active_read;
static kfiber_t *main_fiber;
static uint32_t fad;
static gdrom_direct_sector_type_t type;
static bool finished, failed;
static unsigned int sibling_steps;

static void loader(void *unused) {
    cdrom_request_status_t status;
    (void)unused;
    active_read = fiber_disc_read_dma(disc_io, data, fad, 8, type, 10000);
    if(!active_read) {
        perror("fiber direct DMA submit");
        failed = finished = true;
        return;
    }
    /* Only this fiber parks. No cdrom_request_wait() on this stack. */
    if(fiber_disc_await(active_read, &status) < 0)
        arch_panic("fiber read wait failed with live resources");
    failed |= status.state != CDROM_REQUEST_COMPLETE
        || status.completed_bytes != BYTES;
    if(failed)
        printf("read failed: state=%d error=%d bytes=%zu\n",
               status.state, status.error, status.completed_bytes);
    if(fiber_disc_read_destroy(active_read) < 0)
        arch_panic("fiber read release failed");
    active_read = NULL;
    finished = true;
}

static void sibling(void *unused) {
    (void)unused;
    while(!finished) {
        /* Stand-in for one bounded chunk of unrelated application work. */
        ++sibling_steps;
        if(fiber_switch(main_fiber) < 0)
            arch_panic("sibling yield failed");
    }
}

int main(void) {
    cd_toc_t toc;
    int drive, disc_type;
    kfiber_t *fibers[2];
    bool stopping = false;
    uint64_t deadline;

    /* Synchronous setup is before fiber dispatch, not inside a child fiber. */
    if(cdrom_get_status(&drive, &disc_type) < 0
            || cdrom_read_toc(&toc, false) != ERR_OK
            || !(fad = cdrom_locate_data_track(&toc))) {
        puts("FIBER-READ: no readable data track");
        return EXIT_FAILURE;
    }
    type = disc_type == CD_CDROM_XA || disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1 : GDROM_DIRECT_SECTOR_MODE1;
    main_fiber = fiber_attach();
    disc_io = fiber_disc_create(2);
    fibers[0] = fiber_create(loader_stack, sizeof(loader_stack), loader, NULL);
    fibers[1] = fiber_create(sibling_stack, sizeof(sibling_stack), sibling, NULL);
    if(!main_fiber || !disc_io || !fibers[0] || !fibers[1])
        arch_panic("fiber read setup failed");
    memset(data, 0xa5, sizeof(data));
    dcache_purge_range((uintptr_t)data, sizeof(data));
    deadline = timer_ms_gettime64() + 30000;

    while(fiber_get_state(fibers[0]) != KFIBER_STATE_FINISHED
            || fiber_get_state(fibers[1]) != KFIBER_STATE_FINISHED) {
        bool ready = false;
        if(timer_ms_gettime64() >= deadline) {
            if(stopping) arch_panic("fiber read cleanup deadline exceeded");
            if(fiber_disc_shutdown(disc_io) < 0)
                arch_panic("fiber read cancellation failed");
            stopping = failed = true;
            deadline = timer_ms_gettime64() + 15000;
        }
        if(fiber_disc_pump(disc_io) < 0) arch_panic("disc completion pump failed");
        for(unsigned int i = 0; i < 2; ++i)
            if(fiber_get_state(fibers[i]) == KFIBER_STATE_READY
                    && fiber_switch(fibers[i]) < 0)
                arch_panic("application dispatch failed");
        for(unsigned int i = 0; i < 2; ++i)
            ready |= fiber_get_state(fibers[i]) == KFIBER_STATE_READY;
        if(ready)
            thd_pass(); /* Also give other OS threads a scheduling opportunity. */
        else if(!finished && fiber_disc_idle(disc_io, 20) < 0 && errno != ETIMEDOUT)
            arch_panic("disc owner idle failed");
    }

    if(fiber_destroy(fibers[0]) < 0 || fiber_destroy(fibers[1]) < 0
            || fiber_disc_destroy(disc_io) < 0)
        arch_panic("fiber read final cleanup failed");
    /* No active fibers or DMA here: an ordinary synchronous PIO comparison
       is safe, and remains on the direct backend. */
    if(!failed && (gdrom_direct_read_sectors(reference, fad, 8, type, 10000, NULL) < 0
            || memcmp(data, reference, BYTES)))
        failed = true;
    printf("FIBER-READ: %s sibling-steps=%u\n", failed ? "FAIL" : "PASS", sibling_steps);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
