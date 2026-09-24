/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <kos/fiber.h>
#include <kos/fiber_sync.h>
#include <kos/fiber_vblank.h>
#ifdef FIBER_VBLANK_SERVICE
#include <kos/fiber_service.h>
#endif
#include <stdalign.h>
#include <errno.h>

static alignas(32) uint8_t stack[8192];
static fiber_vblank_t *dispatcher;
static volatile bool paused, released, done, aborting, failed;
static volatile unsigned int count, batches;
static uint8_t order[8];
static uint32_t elapsed[8];
static kthread_t *consumer_thread;
#ifdef FIBER_VBLANK_SERVICE
static fiber_service_t *service;
#else
static kfiber_event_t *ready;
#endif

static void wake(void *data) {
#ifdef FIBER_VBLANK_SERVICE
    if(fiber_service_wake(data) < 0) failed = true;
#else
    if(fiber_event_set(data) < 0) failed = true;
#endif
}

static int yield_consumer(void) {
#ifdef FIBER_VBLANK_SERVICE
    return fiber_service_yield(service);
#else
    return fiber_switch(fiber_main());
#endif
}

static void callback(uint32_t frames, void *data) {
    unsigned int value = (uintptr_t)data;
    if(irq_inside_int() || fiber_current() == fiber_main() ||
       thd_get_current() != consumer_thread ||
       fiber_get_attach_flags() != KFIBER_ATTACH_MATH_CONTEXT) failed = true;
    if(count >= 8) { failed = true; return; }
    order[count] = value;
    elapsed[count++] = frames;
    if(value == 1 && batches == 0) {
        /* Yield mid-batch for several frames. Later handlers must not overtake
           this one, and accumulated frames must become just one next batch. */
        paused = true;
        while(!released && !aborting)
            if(yield_consumer() < 0) { failed = true; return; }
    }
    if(value == 4 && ++batches == 2) {
        if(fiber_vblank_stop(dispatcher) < 0) failed = true;
        /* We are still inside dispatch: destruction must remain prohibited. */
        if(fiber_vblank_destroy(dispatcher) == 0 || errno != EBUSY) failed = true;
        done = true;
    }
}

#ifdef FIBER_VBLANK_SERVICE
static void consume(fiber_service_t *self, void *data) {
    (void)data;
    consumer_thread = thd_get_current();
    while(!done && !aborting && !fiber_service_stop_requested(self)) {
        int result = fiber_vblank_dispatch(dispatcher);
        if(result < 0) { failed = true; break; }
        if(done) break;
        /* Check the retained batch counter after each yield, since a yield
           may consume a service wake hint while a callback is still running. */
        if(result) {
            if(fiber_service_yield(self) < 0) break;
        }
        else if(fiber_service_wait(self, 0) < 0) break;
    }
}
#else
static void consume(void *data) {
    (void)data;
    consumer_thread = thd_get_current();
    while(!done && !aborting) {
        if(fiber_event_wait(ready) < 0) { failed = true; break; }
        /* Clear BEFORE taking a batch, preserving IRQ hints during callbacks. */
        if(fiber_event_clear(ready) < 0 || fiber_vblank_dispatch(dispatcher) < 0) {
            failed = true;
            break;
        }
    }
}
#endif

int main(void) {
    dispatcher = fiber_vblank_create(4);
    if(!dispatcher ||
       fiber_vblank_add(dispatcher, 0, callback, (void *)1) < 0 ||
       fiber_vblank_add(dispatcher, 128, callback, (void *)2) < 0 ||
       fiber_vblank_add(dispatcher, 128, callback, (void *)3) < 0 ||
       fiber_vblank_add(dispatcher, 255, callback, (void *)4) < 0) return 1;
#ifdef FIBER_VBLANK_SERVICE
    fiber_service_executor_t *executor =
        fiber_service_executor_create_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!executor) return 1;
    service = fiber_service_add(executor, stack, sizeof(stack), consume, NULL);
    if(!service || fiber_service_executor_start(executor, NULL) < 0 ||
       fiber_vblank_start(dispatcher, wake, service) < 0) return 1;
#else
    if(!fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT)) return 1;
    ready = fiber_event_create(false);
    kfiber_t *consumer = fiber_create(stack, sizeof(stack), consume, NULL);
    if(!ready || !consumer || fiber_vblank_start(dispatcher, wake, ready) < 0)
        return 1;
#endif
    uint64_t deadline = timer_ms_gettime64() + 3000;
    while(!done && !failed && timer_ms_gettime64() < deadline) {
#ifndef FIBER_VBLANK_SERVICE
        if(fiber_get_state(consumer) == KFIBER_STATE_READY &&
           fiber_switch(consumer) < 0) failed = true;
#endif
        if(paused && !released) {
            thd_sleep(80);
            if(count != 1) failed = true;
            released = true;
        }
        thd_sleep(1);
    }
    aborting = true;
    if(fiber_vblank_stop(dispatcher) < 0) failed = true;
#ifdef FIBER_VBLANK_SERVICE
    if(fiber_service_executor_destroy(executor) < 0) return 1;
#else
    if(fiber_event_set(ready) < 0) return 1;
    while(fiber_get_state(consumer) == KFIBER_STATE_READY)
        if(fiber_switch(consumer) < 0) return 1;
    if(fiber_get_state(consumer) != KFIBER_STATE_FINISHED ||
       fiber_destroy(consumer) < 0 || fiber_event_destroy(ready) < 0) return 1;
#endif
    if(!done || count != 8 || batches != 2 || elapsed[4] < 2) failed = true;
    for(unsigned int i = 0; i < 8; ++i) {
        const uint8_t expected[] = {1,3,2,4};
        if(order[i] != expected[i % 4] || elapsed[i] != elapsed[i / 4 * 4])
            failed = true;
    }
    if(fiber_vblank_destroy(dispatcher) < 0) failed = true;
#ifdef FIBER_VBLANK_SERVICE
    const char *mode = "service";
#else
    const char *mode = "core";
#endif
    printf("FIBER-VBLANK: %s mode=%s order=13241324 batches=%u coalesced=%lu\n",
           failed ? "FAIL" : "PASS", mode, batches, (unsigned long)elapsed[4]);
    return failed ? 1 : 0;
}
