/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/gdrom_direct.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

static unsigned checks, failures, calls, callbacks, allocs, frees, live;
static unsigned fail_command, cancel_command, sequence[8], reports;
static bool fail_alloc, fairness, manual_segments;
static unsigned private_finalized;
static uint64_t ticks;
static uint32_t time_step, budgets[8];
static size_t total_sectors, sector_size;
static gdrom_direct_sector_type_t wanted_type;
static void *tracked[4];
static semaphore_t entered, release_worker;
static _Alignas(32) uint8_t storage[64 * 2352 + 64];
static _Alignas(32) uint8_t storage_b[64 * 2352 + 64];
static uint8_t *const buffer = storage + 32;
static uint8_t *const buffer_b = storage_b + 32;

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("DIRECT-DMA-CHAIN: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

uint64_t chain_probe_time(void) { return ticks; }
void *chain_probe_malloc(size_t size) {
    CHECK(size <= 64); /* Chain metadata only, never a payload buffer. */
    if(fail_alloc) { errno = ENOMEM; return NULL; }
    void *ptr = malloc(size);
    CHECK(ptr != NULL);
    for(unsigned i = 0; i < 4; ++i) {
        if(!tracked[i]) { tracked[i] = ptr; ++allocs; ++live; return ptr; }
    }
    CHECK(false);
    return ptr;
}
void chain_probe_free(void *ptr) {
    bool found = false;
    for(unsigned i = 0; i < 4; ++i) {
        if(tracked[i] == ptr) { tracked[i] = NULL; found = true; break; }
    }
    CHECK(found);
    if(found) { ++frees; --live; }
    free(ptr);
}
void __wrap_cdrom_media_monitor_report_result(int result, cdrom_request_backend_t backend) {
    CHECK(result != ERR_OK && backend == CDROM_REQUEST_BACKEND_DIRECT);
    ++reports;
}

int __wrap_gdrom_direct_read_sectors_dma_request(
        cdrom_request_t *request, void *destination, uint32_t fad, size_t sectors,
        gdrom_direct_sector_type_t type, uint32_t timeout, gdrom_direct_result_t *result) {
    size_t start = fad - 150;
    size_t expected = total_sectors - start;
    size_t bytes = sectors * sector_size;
    uint8_t *base = fairness && (uintptr_t)destination >= (uintptr_t)buffer_b
        && (uintptr_t)destination < (uintptr_t)buffer_b + total_sectors * sector_size
        ? buffer_b : buffer;
    cdrom_request_status_t status;
    if(expected > 16) expected = 16;
    if(manual_segments) expected = 2;
    CHECK(type == wanted_type && sectors == expected && timeout > 0);
    CHECK(destination == base + start * sector_size && !((uintptr_t)destination & 31));
    CHECK(!cdrom_request_get_status(request, &status));
    CHECK(status.backend == CDROM_REQUEST_BACKEND_DIRECT
          && status.state == CDROM_REQUEST_RUNNING
          && status.requested_bytes == total_sectors * sector_size);
    if(calls < 8) { budgets[calls] = timeout; sequence[calls] = base == buffer ? 1 : 2; }
    ++calls;
    if(calls == fail_command) bytes /= 2;
    for(size_t i = 0; i < bytes; ++i)
        ((uint8_t *)destination)[i] = (uint8_t)((start * sector_size + i) * 13 + 7);
    cdrom_request_update_direct_progress(request, bytes);
    memset(result, 0, sizeof(*result));
    result->phase = GDROM_DIRECT_PHASE_COMPLETE;
    result->transferred = result->dma_transferred = bytes;
    ticks += time_step;
    if(calls == cancel_command) CHECK(!cdrom_request_cancel(request));
    if(calls == fail_command) { errno = EIO; return -1; }
    return 0;
}

static void completed(cdrom_request_t *request, const cdrom_request_status_t *status, void *data) {
    (void)request;
    CHECK(data == buffer && status->backend == CDROM_REQUEST_BACKEND_DIRECT);
    CHECK(live == 0); /* Finalization precedes terminal/callback publication. */
    ++callbacks;
}
static void prepare(gdrom_direct_sector_type_t type, size_t sectors) {
    CHECK(!live);
    wanted_type = type; total_sectors = sectors;
    sector_size = type == GDROM_DIRECT_SECTOR_RAW2352 ? 2352 : 2048;
    calls = callbacks = allocs = frees = reports = 0;
    fail_command = cancel_command = time_step = 0;
    fail_alloc = fairness = manual_segments = false;
    private_finalized = 0;
    ticks = 100;
    memset(storage, 0xa5, sizeof(storage));
    memset(storage_b, 0xa5, sizeof(storage_b));
}
static cdrom_request_status_t finish(cdrom_request_t *request, bool callback) {
    cdrom_request_status_t status = { 0 };
    CHECK(request != NULL);
    if(!request) return status;
    CHECK(!cdrom_request_wait(request, 5000, &status));
    if(callback) CHECK(!cdrom_request_wait_callback(request, 5000));
    CHECK(!cdrom_request_destroy(request));
    return status;
}
static void verify_buffer(uint8_t *base, size_t bytes) {
    bool okay = true;
    for(size_t i = 0; i < total_sectors * sector_size; ++i)
        if(base[i] != (i < bytes ? (uint8_t)(i * 13 + 7) : 0xa5)) okay = false;
    CHECK(okay && base[-1] == 0xa5 && base[total_sectors * sector_size] == 0xa5);
}
static void run_read(gdrom_direct_sector_type_t type, size_t sectors,
                     unsigned fail_at, unsigned cancel_at, uint32_t step) {
    gdrom_direct_result_t trace;
    prepare(type, sectors);
    fail_command = fail_at; cancel_command = cancel_at; time_step = step;
    cdrom_request_t *request = gdrom_direct_read_sectors_dma_async(
        buffer, 150, sectors, type, 100, &trace, completed, buffer);
    cdrom_request_status_t status = finish(request, true);
    size_t expected_bytes = sectors * sector_size;
    unsigned expected_calls = (sectors + 15) / 16;
    cdrom_request_state_t state = CDROM_REQUEST_COMPLETE;
    if(fail_at) {
        expected_calls = fail_at;
        expected_bytes = ((fail_at - 1) * 16 + 8) * sector_size;
        state = CDROM_REQUEST_ERROR;
    }
    else if(cancel_at) {
        expected_calls = cancel_at; expected_bytes = cancel_at * 16 * sector_size;
        state = CDROM_REQUEST_CANCELLED;
    }
    else if(step) {
        expected_calls = (100 + step - 1) / step;
        expected_bytes = expected_calls * 16 * sector_size;
        state = CDROM_REQUEST_TIMED_OUT;
    }
    CHECK(status.state == state && calls == expected_calls);
    CHECK(status.completed_bytes == expected_bytes && status.io_completed_bytes == expected_bytes
          && status.remaining_bytes == sectors * sector_size - expected_bytes);
    CHECK(allocs == 1 && frees == 1 && !live && callbacks == 1);
    for(unsigned i = 0; i < calls; ++i) CHECK(budgets[i] == 100 - i * step);
    CHECK(trace.transferred <= 16 * sector_size && trace.transferred == trace.dma_transferred);
    verify_buffer(buffer, expected_bytes);
}

static int partial_executor(cdrom_request_t *request, void *data) {
    cdrom_request_status_t status;
    size_t bytes = *(size_t *)data;
    cdrom_request_update_direct_progress(request, bytes);
    CHECK(!cdrom_request_get_status(request, &status));
    CHECK(status.state == CDROM_REQUEST_RUNNING
          && status.completed_bytes == (bytes < 4704 ? bytes : 4704)
          && status.io_completed_bytes == (bytes < 4704 ? bytes : 4704));
    return ERR_SYS;
}

static int block_executor(cdrom_request_t *request, void *data) {
    (void)request; (void)data;
    sem_signal(&entered);
    sem_wait(&release_worker);
    return ERR_OK;
}
static cdrom_request_t *block_queue(void) {
    cdrom_request_t *blocker = cdrom_request_submit_executor(CD_CMD_SEEK, NULL, 0,
        0, 0, 0, 0, block_executor, NULL, NULL, NULL, NULL);
    CHECK(blocker && !sem_wait_timed(&entered, 1000));
    return blocker;
}
static int bad_continue(cdrom_request_t *request,
        const cdrom_request_dma_segment_t *previous,
        cdrom_request_dma_segment_t *next, void *data) {
    (void)request; (void)data;
    *next = *previous;
    next->buffer = (uint8_t *)previous->buffer + previous->io_bytes;
    next->params.start_sec += previous->params.num_sec;
    next->params.num_sec = 1; /* Invalid raw DMA tail must not reach transport. */
    next->io_bytes = next->data_bytes = 2352;
    return 1;
}
static void private_finalize(cdrom_request_t *request,
        const cdrom_request_status_t *status, void *data) {
    (void)request; (void)data;
    CHECK(status->state == CDROM_REQUEST_ERROR);
    ++private_finalized;
}
int main(void) {
    gdrom_direct_result_t trace;
    cdrom_request_t *r, *blocker;
    cdrom_request_status_t status;
    sem_init(&entered, 0); sem_init(&release_worker, 0);
    prepare(GDROM_DIRECT_SECTOR_RAW2352, 18);
    /* Shutdown admission still releases the newly allocated chain state. */
    cdrom_request_system_shutdown();
    CHECK(!gdrom_direct_read_sectors_dma_async(buffer, 150, 18, wanted_type,
          100, NULL, NULL, NULL) && errno == ENODEV);
    CHECK(allocs == 1 && frees == 1 && !live && !calls);
    CHECK(!cdrom_request_system_init());
    run_read(GDROM_DIRECT_SECTOR_MODE1, 17, 0, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_MODE2_FORM1, 33, 0, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 18, 0, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 32, 0, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 34, 0, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 34, 2, 0, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 34, 0, 1, 0);
    run_read(GDROM_DIRECT_SECTOR_RAW2352, 34, 0, 0, 60);

    prepare(GDROM_DIRECT_SECTOR_RAW2352, 34);
    blocker = block_queue();
    memset(&trace, 0xa5, sizeof(trace));
    r = gdrom_direct_read_sectors_dma_async(buffer, 150, 34, wanted_type,
        100, &trace, completed, buffer);
    CHECK(r && !cdrom_request_cancel(r));
    sem_signal(&release_worker);
    finish(blocker, false);
    status = finish(r, true);
    CHECK(status.state == CDROM_REQUEST_CANCELLED && !calls && !live
          && allocs == 1 && frees == 1 && callbacks == 1);
    CHECK(trace.transferred == 0xa5a5a5a5u);

    prepare(GDROM_DIRECT_SECTOR_RAW2352, 18);
    blocker = block_queue();
    r = gdrom_direct_read_sectors_dma_async(buffer, 150, 18, wanted_type,
        100, NULL, NULL, NULL);
    ticks += 1000; /* Initial queue residence is outside the operation budget. */
    sem_signal(&release_worker);
    finish(blocker, false);
    CHECK(finish(r, false).state == CDROM_REQUEST_COMPLETE);
    CHECK(budgets[0] == 100 && calls == 2 && allocs == frees && !live);

    prepare(GDROM_DIRECT_SECTOR_RAW2352, 32);
    fairness = true;
    blocker = block_queue();
    r = gdrom_direct_read_sectors_dma_async(buffer, 150, 32, wanted_type,
        100, NULL, NULL, NULL);
    cdrom_request_t *second = gdrom_direct_read_sectors_dma_async(buffer_b, 150, 32,
        wanted_type, 100, NULL, NULL, NULL);
    sem_signal(&release_worker);
    finish(blocker, false);
    CHECK(finish(r, false).state == CDROM_REQUEST_COMPLETE);
    CHECK(finish(second, false).state == CDROM_REQUEST_COMPLETE);
    CHECK(calls == 4 && sequence[0] == 1 && sequence[1] == 2
          && sequence[2] == 1 && sequence[3] == 2);
    CHECK(allocs == 2 && frees == 2 && !live);
    verify_buffer(buffer, 32 * 2352); verify_buffer(buffer_b, 32 * 2352);

    prepare(GDROM_DIRECT_SECTOR_RAW2352, 18);
    fail_alloc = true;
    CHECK(!gdrom_direct_read_sectors_dma_async(buffer, 150, 18, wanted_type,
          100, NULL, NULL, NULL) && errno == ENOMEM);
    CHECK(!live && !calls);
    fail_alloc = false;
    CHECK(!gdrom_direct_read_sectors_dma_async(buffer, 150, 33, wanted_type,
          100, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!gdrom_direct_read_sectors_dma_async(buffer, 0xfffff0, 18, wanted_type,
          100, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!gdrom_direct_read_sectors_dma_async((void *)(0x8c000000u + HW_MEMSIZE - 4096),
          150, 18, wanted_type, 100, NULL, NULL, NULL) && errno == EFAULT);
    CHECK(!allocs && !calls);

    /* A segment's wire count and byte accounting must agree with its format. */
    cdrom_request_dma_segment_t segment;
    CHECK(!cdrom_request_dma_segment_init_sized(&segment, buffer, 150, 2, 2048,
                                               0, 4096, true));
    CHECK(!cdrom_request_submit_direct_dma_chain(&segment, wanted_type, 4096, 4096,
          4096, 100, NULL, NULL, NULL, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!cdrom_request_dma_segment_init_sized(&segment, buffer, 150, 1, 2352,
                                               0, 2352, true));
    CHECK(!cdrom_request_submit_direct_dma_chain(&segment, wanted_type, 2352, 2352,
          2352, 100, NULL, NULL, NULL, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!cdrom_request_dma_segment_init_sized(&segment, buffer, 150, 18, 2352,
                                               0, 18 * 2352, true));
    CHECK(!cdrom_request_submit_direct_dma_chain(&segment, wanted_type, 18 * 2352,
          18 * 2352, 18 * 2352, 100, NULL, NULL, NULL, NULL, NULL, NULL) && errno == EINVAL);
    CHECK(!calls);
    prepare(GDROM_DIRECT_SECTOR_RAW2352, 4);
    manual_segments = true;
    CHECK(!cdrom_request_dma_segment_init_sized(&segment, buffer, 150, 2, 2352,
                                               0, 4704, true));
    r = cdrom_request_submit_direct_dma_chain(&segment, wanted_type, 9408, 9408,
        9408, 100, bad_continue, NULL, private_finalize, NULL, NULL, NULL);
    status = finish(r, false);
    CHECK(status.state == CDROM_REQUEST_ERROR && status.completed_bytes == 4704);
    CHECK(calls == 1 && private_finalized == 1);
    verify_buffer(buffer, 4704);
    /* Custom executors (including bounded DMA) have no submitted DMA segment.
       Partial progress must still survive an error, and be clamped to capacity. */
    for(unsigned over = 0; over < 2; ++over) {
        size_t bytes = over ? SIZE_MAX : 2352;
        r = cdrom_request_submit_executor(CD_CMD_DMAREAD, &bytes, sizeof(bytes),
            4704, 4704, 4704, 100, partial_executor, NULL, NULL, NULL, NULL);
        status = finish(r, false);
        CHECK(status.state == CDROM_REQUEST_ERROR
              && status.completed_bytes == (over ? 4704 : 2352)
              && status.io_completed_bytes == (over ? 4704 : 2352)
              && status.remaining_bytes == (over ? 0 : 2352));
    }
    cdrom_request_system_shutdown();
    sem_destroy(&entered); sem_destroy(&release_worker);
    printf("DIRECT-DMA-CHAIN: %s checks=%u\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
