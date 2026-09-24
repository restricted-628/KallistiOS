/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Direct staged streaming, with explicit request/session lifetimes.
   See ../stream-bios for the opt-in legacy BIOS PIO/DMA interface.
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEST_SECTORS 8u
#define TEST_BYTES (TEST_SECTORS * 2048u)
#define COMMAND_MS 10000u
#define WAIT_MS 15000u

_Alignas(32) static uint8_t stream_data[TEST_BYTES];
_Alignas(32) static uint8_t reference_data[TEST_BYTES];
static unsigned int callbacks;

static void transfer_done(cdrom_request_t *request,
                          const cdrom_request_status_t *status, void *data) {
    (void)request;
    (void)status;
    (void)data;
    ++callbacks;
}

/* A failed bounded drain must not lead to freeing an active request or
   returning to code that reuses its DMA buffer. Halt this diagnostic instead. */
static void drain_request(cdrom_request_t *request) {
    (void)cdrom_request_cancel(request);
    if(cdrom_request_wait(request, WAIT_MS, NULL) < 0
            || cdrom_request_wait_callback(request, WAIT_MS) < 0
            || cdrom_request_destroy(request) < 0)
        arch_panic("stream: request could not be safely drained");
}

static int read_stream(uint32_t fad, gdrom_direct_sector_type_t type) {
    cdrom_stream_session_t *session;
    cdrom_stream_session_status_t status;
    cdrom_request_t *request = NULL;
    cdrom_request_status_t transfer;
    int result = -1;

    /* The generic constructor is direct Mode-1. Choose the explicit direct
       constructor only when the disc needs the Mode-2 Form-1 layout. */
    session = type == GDROM_DIRECT_SECTOR_MODE1
        ? cdrom_stream_session_start(fad, TEST_SECTORS, COMMAND_MS, COMMAND_MS)
        : gdrom_direct_stream_session_start(fad, TEST_SECTORS, type,
                                             COMMAND_MS, COMMAND_MS);
    if(!session) {
        perror("stream start");
        return -1;
    }
    if(cdrom_stream_session_wait_ready(session, WAIT_MS, &status) < 0) {
        perror("stream ready wait");
        goto out;
    }
    if(status.backend != CDROM_REQUEST_BACKEND_DIRECT
            || status.state != CDROM_STREAM_SESSION_READY) {
        printf("stream did not become direct/ready: state=%d error=%d\n",
               status.state, status.error);
        goto out;
    }

    for(size_t offset = 0; offset < TEST_BYTES; offset += TEST_BYTES / 2) {
        request = cdrom_stream_session_transfer_async(
            session, stream_data + offset, TEST_BYTES / 2, COMMAND_MS,
            transfer_done, NULL);
        if(!request) {
            perror("stream transfer submit");
            goto out;
        }
        if(cdrom_request_wait(request, WAIT_MS, &transfer) < 0) {
            perror("stream transfer wait");
            goto out;
        }
        if(cdrom_request_wait_callback(request, WAIT_MS) < 0) {
            perror("stream callback wait");
            goto out;
        }
        if(transfer.state != CDROM_REQUEST_COMPLETE
                || transfer.completed_bytes != TEST_BYTES / 2
                || transfer.backend != CDROM_REQUEST_BACKEND_DIRECT) {
            printf("stream transfer failed: state=%d error=%d bytes=%zu\n",
                   transfer.state, transfer.error, transfer.completed_bytes);
            goto out;
        }
        if(cdrom_request_destroy(request) < 0) {
            perror("stream transfer destroy");
            goto out;
        }
        request = NULL;
    }
    if(cdrom_stream_session_wait(session, WAIT_MS, &status) < 0) {
        perror("stream completion wait");
        goto out;
    }
    if(status.state != CDROM_STREAM_SESSION_COMPLETE
            || status.completed_bytes != TEST_BYTES || status.remaining_bytes
            || callbacks != 2) {
        puts("stream completion accounting mismatch");
        goto out;
    }
    result = 0;

out:
    if(request)
        drain_request(request);
    (void)cdrom_stream_session_cancel(session);
    if(cdrom_stream_session_wait(session, WAIT_MS, NULL) < 0
            || cdrom_stream_session_destroy(session) < 0)
        arch_panic("stream: session could not be safely drained");
    return result;
}

int main(void) {
    cd_toc_t toc;
    int drive, disc;
    uint32_t fad;
    gdrom_direct_sector_type_t type;

    puts("Direct staged stream test (requires at least eight data sectors)");
    if(cdrom_get_status(&drive, &disc) < 0
            || cdrom_read_toc(&toc, false) != ERR_OK) {
        puts("Cannot read disc status/TOC");
        return EXIT_FAILURE;
    }
    fad = cdrom_locate_data_track(&toc);
    if(!fad) {
        puts("No data track found");
        return EXIT_FAILURE;
    }
    type = disc == CD_CDROM_XA || disc == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1 : GDROM_DIRECT_SECTOR_MODE1;

    memset(stream_data, 0xa5, sizeof(stream_data));
    dcache_purge_range((uintptr_t)stream_data, sizeof(stream_data));
    if(read_stream(fad, type) < 0)
        return EXIT_FAILURE;

    /* No command may interleave while the stream owns the drive. Only read
       the independent PIO reference after session completion/destruction. */
    if(gdrom_direct_read_sectors(reference_data, fad, TEST_SECTORS, type,
                                     COMMAND_MS, NULL) < 0) {
        perror("direct PIO reference");
        return EXIT_FAILURE;
    }
    if(memcmp(stream_data, reference_data, TEST_BYTES)) {
        puts("DIRECT-STREAM: FAIL payload mismatch");
        return EXIT_FAILURE;
    }
    puts("DIRECT-STREAM: PASS transfers=2 callbacks=2");
    return EXIT_SUCCESS;
}
