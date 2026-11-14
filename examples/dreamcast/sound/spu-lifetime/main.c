/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */

#include <kos.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

/* The engine model is deliberately separate from software-visible state.
   SUSPENDED/requested stop does not mean the engine acknowledged retirement. */
static alignas(32) unsigned char buffer[32];
static g2_dma_callback_t engine_callback;
static void *engine_data;
static g2_dma_status_t engine;
static bool engine_busy;
static bool stop_requested;
static unsigned int refused, submissions, callbacks, callback_errors;
static semaphore_t callback_entered = SEM_INITIALIZER(0);
static semaphore_t callback_release = SEM_INITIALIZER(0);
static semaphore_t shutdown_started = SEM_INITIALIZER(0);
static semaphore_t shutdown_finished = SEM_INITIALIZER(0);
static int shutdown_result;

struct callback_input {
    bool live;
    bool park;
    bool reenter;
    int expected_result;
    spu_transfer_request_t *followup;
};

#define CHECK(expr) do { if(!(expr)) { \
    printf("SPU-LIFETIME: FAIL line=%d errno=%d: %s\n", \
           __LINE__, errno, #expr); \
    return -1; \
} } while(0)

#define CALLBACK_CHECK(expr) do { if(!(expr)) { \
    irq_disable_scoped(); \
    ++callback_errors; \
    printf("SPU-LIFETIME: callback FAIL line=%d: %s\n", __LINE__, #expr); \
} } while(0)

int __wrap_g2_dma_transfer(void *sh4, void *g2, size_t bytes, uint32_t block,
                           g2_dma_callback_t callback, void *data,
                           uint32_t dir, uint32_t mode, uint32_t channel,
                           uint32_t sh4_channel) {
    (void)g2; (void)dir; (void)mode; (void)sh4_channel;
    CHECK(sh4 == buffer && bytes == sizeof(buffer) && !block);
    CHECK(channel == G2_DMA_CHAN_SPU);
    irq_disable_scoped();
    CHECK(!engine_busy && !engine_callback);
    ++engine.sequence;
    ++submissions;
    engine.state = G2_DMA_STATE_RUNNING;
    engine.requested_bytes = engine.remaining_bytes = bytes;
    engine.result = EINPROGRESS;
    engine_callback = callback;
    engine_data = data;
    engine_busy = true;
    stop_requested = false;
    return 0;
}

int __wrap_g2_dma_get_status(uint32_t channel, g2_dma_status_t *status) {
    CHECK(channel == G2_DMA_CHAN_SPU && status);
    irq_disable_scoped();
    *status = engine;
    return 0;
}

int __wrap_g2_dma_cancel(uint32_t channel) {
    CHECK(channel == G2_DMA_CHAN_SPU);
    irq_disable_scoped();
    stop_requested = true;
    ++refused;
    /* No synthetic acknowledgement, START clear or callback on this request. */
    errno = EBUSY;
    return -1;
}

static int complete_engine(void) {
    g2_dma_callback_t callback;
    void *data;
    irq_disable_scoped();
    CHECK(engine_busy && engine_callback);
    engine_busy = false;
    engine.state = G2_DMA_STATE_COMPLETE;
    engine.result = 0;
    engine.remaining_bytes = 0;
    callback = engine_callback;
    data = engine_data;
    engine_callback = NULL;
    engine_data = NULL;
    /* IRQ exclusion models publication before the completion callback. */
    callback(data);
    return 0;
}

static int wait_engine(unsigned int expected_submissions, bool wait_stop) {
    uint64_t limit = timer_ms_gettime64() + 2000;
    for(;;) {
        bool ready;
        irq_mask_t old = irq_disable();
        ready = engine_busy && submissions == expected_submissions &&
                (!wait_stop || stop_requested);
        irq_restore(old);
        if(ready) return 0;
        CHECK(timer_ms_gettime64() < limit);
        thd_sleep(1);
    }
}

static void completed(spu_transfer_request_t *request,
                      const spu_transfer_status_t *status, void *data) {
    struct callback_input *input = data;
    spu_transfer_status_t current;
    CALLBACK_CHECK(input && input->live);
    CALLBACK_CHECK(status->result == input->expected_result);
    if(input->reenter) {
        CALLBACK_CHECK(spu_transfer_get_status(request, &current) == 0);
        CALLBACK_CHECK(current.state == SPU_TRANSFER_COMPLETE);
        CALLBACK_CHECK(spu_transfer_cancel(request) < 0 && errno == EALREADY);
        CALLBACK_CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
        CALLBACK_CHECK(spu_transfer_wait_callback(request, 1) < 0 &&
                       errno == EDEADLK);
        CALLBACK_CHECK(spu_shutdown() < 0 && errno == EDEADLK);
        /* A callback may enqueue more work; it must not wait for its own
           dispatcher to deliver another callback. */
        CALLBACK_CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer),
                       SPU_TRANSFER_DMA, 0, NULL, NULL, &input->followup) == 0);
    }
    if(input->park) {
        sem_signal(&callback_entered);
        CALLBACK_CHECK(sem_wait(&callback_release) == 0);
        /* This cookie stands for callback-owned input/VFS context. The probe
           retains it until dispatcher retirement, not just DMA completion. */
        CALLBACK_CHECK(input->live);
    }
    irq_disable_scoped();
    ++callbacks;
}

/* cancel=0: wait timeout only; cancel=1: cancellation; cancel=2: execution
   deadline. All three keep an active engine until explicit natural completion. */
static int run_active_case(int cancel, bool suspended) {
    spu_transfer_request_t *request;
    spu_transfer_status_t status;
    struct callback_input input = {
        .live = true,
        .expected_result = cancel == 1 ? ECANCELED : cancel == 2 ? ETIMEDOUT : 0
    };
    unsigned int expected = submissions + 1;
    unsigned int prior_callbacks = callbacks;

    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                            cancel == 2 ? 40 : 0, completed, &input,
                            &request) == 0);
    CHECK(wait_engine(expected, false) == 0);
    if(suspended) {
        irq_disable_scoped();
        engine.state = G2_DMA_STATE_SUSPENDED;
        /* Only software state changes. Physical progress remains possible. */
    }
    if(cancel == 1) CHECK(spu_transfer_cancel(request) == 0);
    if(cancel) CHECK(wait_engine(expected, true) == 0);
    CHECK(spu_transfer_wait(request, 20, &status) < 0 && errno == ETIMEDOUT);
    CHECK(status.state == SPU_TRANSFER_RUNNING);
    CHECK(callbacks == prior_callbacks);
    CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
    CHECK(complete_engine() == 0);
    int result = spu_transfer_wait(request, 1000, &status);
    CHECK(input.expected_result ? result < 0 && errno == input.expected_result :
                                  result == 0);
    CHECK(status.state == (cancel == 1 ? SPU_TRANSFER_CANCELLED :
                           cancel == 2 ? SPU_TRANSFER_TIMED_OUT :
                                         SPU_TRANSFER_COMPLETE));
    CHECK(spu_transfer_wait_callback(request, 1000) == 0);
    CHECK(callbacks == prior_callbacks + 1 && !callback_errors);
    CHECK(spu_transfer_destroy(request) == 0);
    input.live = false;
    printf("SPU-LIFETIME: active cancel=%d suspended=%d PASS\n", cancel, suspended);
    return 0;
}

static int run_queued_cancel(void) {
    spu_transfer_request_t *active, *queued;
    spu_transfer_status_t status;
    struct callback_input input = {.live = true, .expected_result = ECANCELED};
    unsigned int expected = submissions + 1;
    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, NULL, NULL, &active) == 0);
    CHECK(wait_engine(expected, false) == 0);
    CHECK(spu_memload_async(0x40020, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, completed, &input, &queued) == 0);
    CHECK(spu_transfer_get_status(queued, &status) == 0);
    CHECK(status.state == SPU_TRANSFER_QUEUED);
    CHECK(spu_transfer_cancel(queued) == 0);
    CHECK(spu_transfer_wait(queued, 1000, &status) < 0 && errno == ECANCELED);
    CHECK(status.completed_bytes == 0);
    CHECK(spu_transfer_wait_callback(queued, 1000) == 0);
    CHECK(submissions == expected && !callback_errors);
    CHECK(spu_transfer_destroy(queued) == 0);
    input.live = false;
    CHECK(complete_engine() == 0);
    CHECK(spu_transfer_wait(active, 1000, &status) == 0);
    CHECK(spu_transfer_destroy(active) == 0);
    puts("SPU-LIFETIME: queued cancellation PASS");
    return 0;
}

static int run_callback_retirement(void) {
    spu_transfer_request_t *request;
    spu_transfer_status_t status;
    struct callback_input input = {.live = true, .park = true, .reenter = true};
    unsigned int expected = submissions + 1;
    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, completed, &input, &request) == 0);
    CHECK(wait_engine(expected, false) == 0);
    CHECK(complete_engine() == 0);
    CHECK(sem_wait_timed(&callback_entered, 1000) == 0);
    CHECK(spu_transfer_wait(request, 1000, &status) == 0);
    CHECK(status.callback_pending);
    CHECK(spu_transfer_wait_callback(request, 20) < 0 && errno == ETIMEDOUT);
    CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
    CHECK(wait_engine(expected + 1, false) == 0);
    CHECK(complete_engine() == 0);
    CHECK(spu_transfer_wait(input.followup, 1000, &status) == 0);
    CHECK(spu_transfer_destroy(input.followup) == 0);
    sem_signal(&callback_release);
    CHECK(spu_transfer_wait_callback(request, 1000) == 0);
    CHECK(!callback_errors);
    CHECK(spu_transfer_destroy(request) == 0);
    input.live = false;
    puts("SPU-LIFETIME: callback reentry and input retention PASS");
    return 0;
}

static void *shutdown_thread(void *unused) {
    (void)unused;
    sem_signal(&shutdown_started);
    shutdown_result = spu_shutdown();
    sem_signal(&shutdown_finished);
    return NULL;
}

/* A transfer can be terminal while its application callback is still queued
   behind another callback. Neither state permits the queued cookie to retire. */
static int run_queued_callback_retirement(void) {
    spu_transfer_request_t *first, *queued;
    spu_transfer_status_t status;
    struct callback_input parked_input = {.live = true, .park = true};
    struct callback_input queued_input = {.live = true};
    unsigned int expected = submissions + 1;
    unsigned int prior_callbacks = callbacks;

    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, completed, &parked_input, &first) == 0);
    CHECK(wait_engine(expected, false) == 0);
    CHECK(complete_engine() == 0);
    CHECK(sem_wait_timed(&callback_entered, 1000) == 0);
    CHECK(spu_memload_async(0x40020, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, completed, &queued_input, &queued) == 0);
    CHECK(wait_engine(expected + 1, false) == 0);
    CHECK(complete_engine() == 0);
    CHECK(spu_transfer_wait(queued, 1000, &status) == 0);
    CHECK(status.state == SPU_TRANSFER_COMPLETE);
    CHECK(callbacks == prior_callbacks && queued_input.live);
    CHECK(spu_transfer_wait_callback(queued, 20) < 0 && errno == ETIMEDOUT);
    CHECK(spu_transfer_get_status(queued, &status) == 0);
    CHECK(status.callback_pending);
    CHECK(spu_transfer_destroy(queued) < 0 && errno == EBUSY);
    sem_signal(&callback_release);
    CHECK(spu_transfer_wait_callback(first, 1000) == 0);
    CHECK(spu_transfer_wait_callback(queued, 1000) == 0);
    CHECK(callbacks == prior_callbacks + 2 && !callback_errors);
    CHECK(spu_transfer_destroy(first) == 0);
    CHECK(spu_transfer_destroy(queued) == 0);
    parked_input.live = queued_input.live = false;
    puts("SPU-LIFETIME: queued callback input retention PASS");
    return 0;
}

static int run_shutdown_drain(void) {
    spu_transfer_request_t *request, *rejected = NULL;
    spu_transfer_status_t status;
    struct callback_input input = {
        .live = true, .park = true, .expected_result = ECANCELED
    };
    unsigned int expected = submissions + 1;
    CHECK(spu_memload_async(0x40000, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, completed, &input, &request) == 0);
    CHECK(wait_engine(expected, false) == 0);
    kthread_t *thread = thd_create(false, shutdown_thread, NULL);
    CHECK(thread);
    CHECK(sem_wait_timed(&shutdown_started, 1000) == 0);
    CHECK(wait_engine(expected, true) == 0);
    CHECK(sem_wait_timed(&shutdown_finished, 20) < 0 && errno == ETIMEDOUT);
    CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
    CHECK(spu_memload_async(0x40020, buffer, sizeof(buffer), SPU_TRANSFER_DMA,
                           0, NULL, NULL, &rejected) < 0 && errno == ENODEV);
    CHECK(!rejected);
    CHECK(complete_engine() == 0);
    CHECK(sem_wait_timed(&callback_entered, 1000) == 0);
    CHECK(spu_transfer_wait(request, 1000, &status) < 0 && errno == ECANCELED);
    CHECK(sem_wait_timed(&shutdown_finished, 20) < 0 && errno == ETIMEDOUT);
    CHECK(spu_transfer_destroy(request) < 0 && errno == EBUSY);
    sem_signal(&callback_release);
    CHECK(sem_wait_timed(&shutdown_finished, 1000) == 0);
    CHECK(thd_join(thread, NULL) == 0);
    CHECK(shutdown_result == 0 && !callback_errors);
    CHECK(spu_transfer_wait_callback(request, 1000) == 0);
    CHECK(spu_transfer_destroy(request) == 0);
    input.live = false;
    puts("SPU-LIFETIME: shutdown admission/engine/callback drain PASS");
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if(run_active_case(0, false) < 0 || run_active_case(1, false) < 0 ||
       run_active_case(2, false) < 0 || run_active_case(1, true) < 0 ||
       run_queued_cancel() < 0 || run_callback_retirement() < 0 ||
       run_queued_callback_retirement() < 0 || run_shutdown_drain() < 0)
        return EXIT_FAILURE;
    printf("SPU-LIFETIME: PASS cases=8 callbacks=%u submissions=%u\n",
           callbacks, submissions);
    return EXIT_SUCCESS;
}
