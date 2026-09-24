/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */

#include <kos.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

static alignas(32) unsigned char buffer[32];
static g2_dma_callback_t engine_callback;
static void *engine_data;
static g2_dma_status_t engine;
static unsigned int refused;
static volatile unsigned int callbacks;
static volatile int callback_result;

#define CHECK(expr) do { if(!(expr)) { \
    printf("SPU-LIFETIME: FAIL line=%d errno=%d\n", __LINE__, errno); \
    return -1; \
} } while(0)

int __wrap_g2_dma_transfer(void *sh4, void *g2, size_t bytes, uint32_t block,
                           g2_dma_callback_t callback, void *data,
                           uint32_t dir, uint32_t mode, uint32_t channel,
                           uint32_t sh4_channel) {
    (void)g2; (void)dir; (void)mode; (void)sh4_channel;
    CHECK(sh4 == buffer && bytes == sizeof(buffer) && !block);
    CHECK(channel == G2_DMA_CHAN_SPU);
    irq_disable_scoped();
    ++engine.sequence;
    engine.state = G2_DMA_STATE_RUNNING;
    engine.requested_bytes = engine.remaining_bytes = bytes;
    engine.result = EINPROGRESS;
    engine_callback = callback;
    engine_data = data;
    return 0;
}

int __wrap_g2_dma_get_status(uint32_t channel, g2_dma_status_t *status) {
    CHECK(channel == G2_DMA_CHAN_SPU && status);
    *status = engine;
    return 0;
}

int __wrap_g2_dma_cancel(uint32_t channel) {
    CHECK(channel == G2_DMA_CHAN_SPU);
    ++refused;
    errno = EBUSY;
    return -1;
}

static void complete_engine(void) {
    irq_disable_scoped();
    engine.state = G2_DMA_STATE_COMPLETE;
    engine.result = 0;
    engine.remaining_bytes = 0;
    engine_callback(engine_data);
    engine_callback = NULL;
    engine_data = NULL;
}

static void completed(spu_transfer_request_t *request,
                      const spu_transfer_status_t *status, void *data) {
    (void)request; (void)data;
    callback_result = status->result;
    ++callbacks;
}

static int run_case(bool cancel) {
    spu_transfer_request_t *request;
    spu_transfer_status_t status;
    unsigned int prior_callbacks = callbacks;
    uint64_t limit;
    int expected = cancel ? ECANCELED : ETIMEDOUT;

    engine.state = G2_DMA_STATE_IDLE;
    refused = 0;
    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                            cancel ? 0 : 100, completed, NULL, &request) == 0);
    limit = timer_ms_gettime64() + 2000;
    for(;;) {
        bool started;
        irq_mask_t old = irq_disable();
        started = engine.state == G2_DMA_STATE_RUNNING;
        irq_restore(old);
        if(started) break;
        CHECK(timer_ms_gettime64() < limit);
        thd_sleep(1);
    }
    if(cancel) CHECK(spu_transfer_cancel(request) == 0);
    for(;;) {
        unsigned int stops;
        irq_mask_t old = irq_disable();
        stops = refused;
        irq_restore(old);
        if(stops) break;
        CHECK(timer_ms_gettime64() < limit);
        thd_sleep(1);
    }

    CHECK(spu_transfer_wait(request, 20, &status) < 0 && errno == ETIMEDOUT);
    CHECK(status.state == SPU_TRANSFER_RUNNING);
    CHECK(callbacks == prior_callbacks);
    CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
    complete_engine();
    CHECK(spu_transfer_wait(request, 1000, &status) < 0 && errno == expected);
    CHECK(status.state == (cancel ? SPU_TRANSFER_CANCELLED : SPU_TRANSFER_TIMED_OUT));
    CHECK(spu_transfer_wait_callback(request, 1000) == 0);
    CHECK(callbacks == prior_callbacks + 1 && callback_result == expected);
    CHECK(spu_transfer_destroy(request) == 0);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if(run_case(true) < 0 || run_case(false) < 0) return EXIT_FAILURE;
    printf("SPU-LIFETIME: PASS cases=2 callbacks=%u\n", callbacks);
    return EXIT_SUCCESS;
}
