/* KallistiOS ##version##

   Asynchronous direct GD-ROM DMA request validation.
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/asic.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>
#include <kos/init.h>
#include <kos/thread.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEST_SECTORS GDROM_DIRECT_DMA_MAX_SECTORS
#define TEST_BYTES   (TEST_SECTORS * GDROM_DIRECT_SECTOR_SIZE)
#define GUARD_BYTES  32u
#define GUARD_VALUE  0xa5u
#define REQUEST_TIMEOUT_MS 6000u

_Alignas(32) static uint8_t direct_buffer[TEST_BYTES + GUARD_BYTES];
_Alignas(32) static uint8_t bios_buffer[TEST_BYTES + GUARD_BYTES];
static volatile bool callback_seen;
static volatile bool callback_coherent;

static int dispose_request(cdrom_request_t *request, bool cancel) {
    if(cancel)
        (void)cdrom_request_cancel(request);

    if(cdrom_request_wait(request, REQUEST_TIMEOUT_MS, NULL) < 0) {
        perror("cleanup request wait");
        return -1;
    }
    if(cdrom_request_wait_callback(request, REQUEST_TIMEOUT_MS) < 0) {
        perror("cleanup callback wait");
        return -1;
    }
    if(cdrom_request_destroy(request) < 0) {
        perror("cleanup request destroy");
        return -1;
    }
    return 0;
}

static bool guard_intact(const uint8_t *buffer) {
    size_t i;

    for(i = TEST_BYTES; i < TEST_BYTES + GUARD_BYTES; ++i) {
        if(buffer[i] != GUARD_VALUE)
            return false;
    }
    return true;
}

static void completed(cdrom_request_t *request,
                      const cdrom_request_status_t *status, void *data) {
    (void)request;
    (void)data;

    callback_coherent = status->backend == CDROM_REQUEST_BACKEND_DIRECT
        && status->state == CDROM_REQUEST_COMPLETE
        && status->completed_bytes == TEST_BYTES
        && status->io_completed_bytes == TEST_BYTES;
    callback_seen = true;
}

int main(int argc, char **argv) {
    gdrom_direct_probe_result_t probe;
    gdrom_direct_result_t direct_result;
    gdrom_direct_sector_type_t sector_type;
    cdrom_request_status_t initial_status = { 0 };
    cdrom_request_status_t final_status = { 0 };
    cdrom_request_t *request = NULL;
    cd_toc_t toc;
    uint32_t fad;
    int bios_result = ERR_SYS;
    int request_error = 0;
    bool request_submitted = false;
    bool request_terminal = false;
    bool passed;

    (void)argc;
    (void)argv;

    memset(direct_buffer, GUARD_VALUE, sizeof(direct_buffer));
    memset(bios_buffer, GUARD_VALUE, sizeof(bios_buffer));
    memset(&direct_result, 0, sizeof(direct_result));

    puts("Asynchronous direct GD-ROM DMA validation");
    if(gdrom_direct_probe(&probe, 4000) < 0
            || (probe.result != ERR_OK && probe.result != ERR_DISC_CHG)) {
        request_error = errno ? errno : cdrom_result_to_errno(probe.result);
        printf("probe failed: result=%d errno=%d\n",
               probe.result, request_error);
        goto done;
    }

    bios_result = cdrom_read_toc(&toc, probe.status.disc_type == CD_GDROM);
    if(bios_result != ERR_OK) {
        printf("TOC failed: result=%d\n", bios_result);
        goto done;
    }

    fad = cdrom_locate_data_track(&toc);
    if(!fad) {
        request_error = ENOENT;
        goto done;
    }

    sector_type = probe.status.disc_type == CD_CDROM_XA
            || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1
        : GDROM_DIRECT_SECTOR_MODE1;

    request = gdrom_direct_read_sectors_dma_async(
        direct_buffer, fad, TEST_SECTORS, sector_type, 4000,
        &direct_result, completed, NULL);
    if(!request) {
        request_error = errno;
        printf("submit failed: %s (%d)\n",
               strerror(request_error), request_error);
        goto done;
    }
    request_submitted = true;

    (void)cdrom_request_get_status(request, &initial_status);
    if(cdrom_request_wait(request, REQUEST_TIMEOUT_MS, &final_status) < 0) {
        request_error = errno;
        printf("wait failed: %s (%d)\n", strerror(request_error),
               request_error);
        goto done;
    }
    request_terminal = true;
    if(cdrom_request_wait_callback(request, REQUEST_TIMEOUT_MS) < 0) {
        request_error = errno;
        printf("callback wait failed: %s (%d)\n", strerror(request_error),
               request_error);
        goto done;
    }

    bios_result = cdrom_read_sectors(bios_buffer, fad, TEST_SECTORS);

done:
    if(request) {
        if(dispose_request(request, !request_terminal) < 0)
            request_error = request_error ? request_error : EIO;
        request = NULL;
    }

    passed = request_error == 0 && request_submitted
        && initial_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && (initial_status.state == CDROM_REQUEST_QUEUED
            || initial_status.state == CDROM_REQUEST_RUNNING
            || initial_status.state == CDROM_REQUEST_COMPLETE)
        && final_status.backend == CDROM_REQUEST_BACKEND_DIRECT
        && final_status.state == CDROM_REQUEST_COMPLETE
        && final_status.result == ERR_OK
        && final_status.requested_bytes == TEST_BYTES
        && final_status.data_bytes == TEST_BYTES
        && final_status.completed_bytes == TEST_BYTES
        && final_status.io_bytes == TEST_BYTES
        && final_status.io_completed_bytes == TEST_BYTES
        && callback_seen && callback_coherent
        && direct_result.command_event && direct_result.dma_event_seen
        && direct_result.dma_event == ASIC_EVT_GD_DMA
        && direct_result.dma_transferred == TEST_BYTES
        && bios_result == ERR_OK
        && memcmp(direct_buffer, bios_buffer, TEST_BYTES) == 0
        && guard_intact(direct_buffer) && guard_intact(bios_buffer);

    printf("%s: initial=%d final=%d backend=%d err=%d\n",
           passed ? "PASS" : "FAIL", initial_status.state,
           final_status.state, final_status.backend, request_error);
    printf("bytes=%lu/%lu callback=%d/%d event=%d/%04lx\n",
           (unsigned long)final_status.completed_bytes,
           (unsigned long)final_status.io_completed_bytes,
           callback_seen, callback_coherent, direct_result.command_event,
           (unsigned long)direct_result.dma_event);
    printf("bios=%d match=%d guards=%d/%d progress=%lu\n",
           bios_result,
           bios_result == ERR_OK
               && memcmp(direct_buffer, bios_buffer, TEST_BYTES) == 0,
           guard_intact(direct_buffer), guard_intact(bios_buffer),
           (unsigned long)direct_result.dma_transferred);

    for(;;)
        thd_sleep(1000);
}
