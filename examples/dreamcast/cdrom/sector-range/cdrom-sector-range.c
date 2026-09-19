/* KallistiOS ##version##

   Bounded FAD-range validation over BIOS and direct transports.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_RANGE_SECTORS 64u
#define TEST_READ_SECTORS 2u
#define TEST_BYTES (TEST_READ_SECTORS * 2048u)
#define CHAIN_SECTORS 33u
#define CHAIN_BYTES (CHAIN_SECTORS * 2048u)
#define TEST_TIMEOUT_MS 5000u
#define TEST_IDLE_MS 1000u

KOS_INIT_FLAGS(INIT_DEFAULT);

static volatile int callback_count;

static void request_complete(
        cdrom_request_t *request,
        const cdrom_request_status_t *status, void *data) {
    (void)request;
    (void)data;
    if(status->state == CDROM_REQUEST_COMPLETE)
        ++callback_count;
}

static int wait_request(cdrom_request_t *request, const char *label) {
    cdrom_request_status_t status;
    int failed = 0;

    if(!request) {
        printf("%s submit failed: %s\n", label, strerror(errno));
        return -1;
    }
    if(cdrom_request_wait(request, TEST_TIMEOUT_MS + 1000u, &status) < 0) {
        printf("%s wait failed: %s\n", label, strerror(errno));
        (void)cdrom_request_cancel(request);
        if(cdrom_request_wait(request, TEST_TIMEOUT_MS + 1000u,
                              &status) < 0) {
            printf("%s cleanup wait failed: %s\n", label, strerror(errno));
            return -1;
        }
        failed = 1;
    }
    if(status.state != CDROM_REQUEST_COMPLETE) {
        printf("%s terminal state=%d result=%d errno=%d\n", label,
               status.state, status.result, status.error);
        failed = 1;
    }
    if(cdrom_request_wait_callback(request, TEST_TIMEOUT_MS + 1000u) < 0) {
        printf("%s callback wait failed: %s\n", label, strerror(errno));
        return -1;
    }
    if(cdrom_request_destroy(request) < 0) {
        printf("%s destroy failed: %s\n", label, strerror(errno));
        return -1;
    }
    return failed ? -1 : 0;
}

static int test_common_range(
        cdrom_sector_range_t *range, const uint8_t *expected,
        uint8_t *async_buffer, const char *label) {
    cdrom_sector_range_info_t info;
    cdrom_stream_session_status_t stream_status;
    cdrom_stream_session_t *session = NULL;
    cdrom_request_t *transfer = NULL;
    _Alignas(32) uint8_t stream_buffer[TEST_BYTES];
    bool eof = false;
    int failed = 0;

    if(cdrom_sector_range_get_info(range, &info) < 0
            || info.position != 0
            || info.sector_count != TEST_RANGE_SECTORS) {
        printf("%s initial info failed\n", label);
        return -1;
    }

    callback_count = 0;
    if(wait_request(cdrom_sector_range_read_async(
            range, async_buffer, TEST_READ_SECTORS, TEST_TIMEOUT_MS,
            request_complete, NULL), "range async read") < 0)
        return -1;
    if(callback_count != 1 || memcmp(async_buffer, expected, TEST_BYTES)) {
        printf("%s async data/callback mismatch\n", label);
        failed = 1;
    }

    if(cdrom_sector_range_tell(range) != TEST_READ_SECTORS
            || cdrom_sector_range_seek(range, 0, SEEK_SET) != 0) {
        printf("%s seek/tell mismatch\n", label);
        failed = 1;
    }

    if(wait_request(cdrom_sector_range_preseek_async(
            range, TEST_TIMEOUT_MS, NULL, NULL), "range preseek") < 0)
        failed = 1;

    session = cdrom_sector_range_stream_start(
        range, TEST_READ_SECTORS, TEST_TIMEOUT_MS, TEST_IDLE_MS);
    if(!session) {
        printf("%s stream submit failed: %s\n", label, strerror(errno));
        failed = 1;
        goto range_tail;
    }
    if(cdrom_sector_range_seek(range, 0, SEEK_SET) != -1 || errno != EBUSY) {
        printf("%s stream did not exclude seek\n", label);
        failed = 1;
    }
    if(cdrom_stream_session_wait_ready(
            session, TEST_TIMEOUT_MS, &stream_status) < 0
            || stream_status.state != CDROM_STREAM_SESSION_READY) {
        printf("%s stream did not become ready\n", label);
        failed = 1;
        (void)cdrom_stream_session_cancel(session);
        goto finish_stream;
    }

    transfer = cdrom_stream_session_transfer_async(
        session, stream_buffer, sizeof(stream_buffer), TEST_TIMEOUT_MS,
        NULL, NULL);
    if(wait_request(transfer, "range stream transfer") < 0) {
        transfer = NULL;
        failed = 1;
        (void)cdrom_stream_session_cancel(session);
        goto finish_stream;
    }
    transfer = NULL;

finish_stream:
    if(cdrom_stream_session_wait(
            session, TEST_TIMEOUT_MS, &stream_status) < 0
            || stream_status.state != CDROM_STREAM_SESSION_COMPLETE) {
        printf("%s stream terminal mismatch\n", label);
        failed = 1;
    }
    if(cdrom_stream_session_destroy(session) < 0) {
        printf("%s stream destroy failed: %s\n", label, strerror(errno));
        failed = 1;
    }
    session = NULL;
    if(memcmp(stream_buffer, expected, TEST_BYTES)
            || cdrom_sector_range_tell(range) != TEST_READ_SECTORS) {
        printf("%s stream data/cursor mismatch\n", label);
        failed = 1;
    }

range_tail:
    if(cdrom_sector_range_seek(range, 1, SEEK_END)
            != TEST_RANGE_SECTORS
            || cdrom_sector_range_eof(range, &eof) < 0 || !eof) {
        printf("%s EOF/clamp mismatch\n", label);
        failed = 1;
    }
    if(cdrom_sector_range_read(range, async_buffer, 1, TEST_TIMEOUT_MS) != 0) {
        printf("%s EOF read was not zero\n", label);
        failed = 1;
    }
    return failed ? -1 : 0;
}

int main(int argc, char **argv) {
    cdrom_sector_range_t *bios_range = NULL;
    cdrom_sector_range_t *direct_range = NULL;
    gdrom_direct_probe_result_t probe;
    gdrom_direct_sector_type_t sector_type;
    cd_toc_t toc;
    uint8_t *unaligned_allocation = NULL;
    uint8_t *unaligned;
    uint8_t *bios_chain = NULL;
    uint8_t *chain_async = NULL;
    _Alignas(32) uint8_t bios_async[TEST_BYTES];
    _Alignas(32) uint8_t direct_async[TEST_BYTES];
    uint32_t fad;
    int failed = 0;

    (void)argc;
    (void)argv;
    puts("KOS bounded sector-range validation");

    if(cdrom_read_toc(&toc, false) != ERR_OK
            || !(fad = cdrom_locate_data_track(&toc))) {
        puts("BIOS data-track discovery failed");
        failed = 1;
        goto done;
    }
    printf("data range begins at FAD %lu\n", (unsigned long)fad);

    bios_range = cdrom_sector_range_open(fad, TEST_RANGE_SECTORS);
    if(!bios_range) {
        printf("BIOS range open failed: %s\n", strerror(errno));
        failed = 1;
        goto done;
    }

    unaligned_allocation = malloc(TEST_BYTES + 1u);
    if(!unaligned_allocation) {
        failed = 1;
        goto done;
    }
    unaligned = unaligned_allocation + 1;
    if(cdrom_sector_range_read(
            bios_range, unaligned, TEST_READ_SECTORS,
            TEST_TIMEOUT_MS) != TEST_READ_SECTORS) {
        printf("BIOS unaligned sync read failed: %s\n", strerror(errno));
        failed = 1;
        goto done;
    }
    bios_chain = aligned_alloc(32, CHAIN_BYTES);
    chain_async = aligned_alloc(32, CHAIN_BYTES);
    if(!bios_chain || !chain_async) {
        failed = 1;
        goto done;
    }
    if(cdrom_sector_range_seek(bios_range, 0, SEEK_SET) != 0
            || cdrom_sector_range_read(
                bios_range, bios_chain, CHAIN_SECTORS,
                TEST_TIMEOUT_MS) != CHAIN_SECTORS
            || cdrom_sector_range_seek(bios_range, 0, SEEK_SET) != 0
            || wait_request(cdrom_sector_range_read_async(
                bios_range, chain_async, CHAIN_SECTORS, TEST_TIMEOUT_MS,
                NULL, NULL), "BIOS chained async read") < 0
            || memcmp(bios_chain, chain_async, CHAIN_BYTES)) {
        puts("BIOS 33-sector chain mismatch");
        failed = 1;
        goto done;
    }
    if(cdrom_sector_range_seek(bios_range, 0, SEEK_SET) != 0
            || test_common_range(
                bios_range, unaligned, bios_async, "BIOS") < 0) {
        failed = 1;
        goto done;
    }
    puts("BIOS range sync/async/preseek/stream/EOF passed");

    if(gdrom_direct_probe(&probe, TEST_TIMEOUT_MS) < 0) {
        printf("direct probe failed: %s\n", strerror(errno));
        failed = 1;
        goto done;
    }
    sector_type = probe.status.disc_type == CD_CDROM_XA
            || probe.status.disc_type == CD_CDI
        ? GDROM_DIRECT_SECTOR_MODE2_FORM1
        : GDROM_DIRECT_SECTOR_MODE1;
    direct_range = gdrom_direct_sector_range_open(
        fad, TEST_RANGE_SECTORS, sector_type);
    if(!direct_range) {
        printf("direct range open failed: %s\n", strerror(errno));
        failed = 1;
        goto done;
    }
    if(wait_request(cdrom_sector_range_read_async(
            direct_range, chain_async, CHAIN_SECTORS, TEST_TIMEOUT_MS,
            NULL, NULL), "direct chained async read") < 0
            || memcmp(bios_chain, chain_async, CHAIN_BYTES)) {
        puts("direct 33-sector chain mismatch");
        failed = 1;
        goto done;
    }
    if(cdrom_sector_range_seek(direct_range, 0, SEEK_SET) != 0) {
        failed = 1;
        goto done;
    }
    if(cdrom_sector_range_read(
            direct_range, direct_async, TEST_READ_SECTORS,
            TEST_TIMEOUT_MS) != TEST_READ_SECTORS
            || memcmp(direct_async, unaligned, TEST_BYTES)) {
        printf("direct sync comparison failed: %s\n", strerror(errno));
        failed = 1;
        goto done;
    }
    if(cdrom_sector_range_seek(direct_range, 0, SEEK_SET) != 0
            || test_common_range(
                direct_range, unaligned, direct_async, "direct") < 0) {
        failed = 1;
        goto done;
    }
    puts("direct range sync/async/preseek/stream/EOF passed");

done:
    if(direct_range && cdrom_sector_range_close(direct_range) < 0) {
        printf("direct range close failed: %s\n", strerror(errno));
        failed = 1;
    }
    if(bios_range && cdrom_sector_range_close(bios_range) < 0) {
        printf("BIOS range close failed: %s\n", strerror(errno));
        failed = 1;
    }
    free(unaligned_allocation);
    free(bios_chain);
    free(chain_async);

    printf("%s: close or reset the console to exit.\n",
           failed ? "FAIL" : "PASS");
    for(;;)
        thd_sleep(1000);
}
