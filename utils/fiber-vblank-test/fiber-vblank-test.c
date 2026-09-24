/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos/fiber.h>
#include <kos/irq.h>
#include <dc/vblank.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool test_in_irq;
irq_mask_t test_mask;
static unsigned int checks, allocations, releases, irq_allocations;
static bool fail_malloc;
static kfiber_t *current = (kfiber_t *)2;
static asic_evt_handler irq_handler;
static void *irq_data;

#define CHECK(c) do { ++checks; if(!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while(0)

void *test_malloc(size_t bytes) {
    if(test_in_irq) ++irq_allocations;
    if(fail_malloc) { fail_malloc = false; return NULL; }
    ++allocations;
    return malloc(bytes);
}
static void *test_calloc(size_t count, size_t bytes) {
    if(test_in_irq) ++irq_allocations;
    ++allocations;
    return calloc(count, bytes);
}
void test_free(void *ptr) {
    if(test_in_irq) ++irq_allocations;
    if(ptr) ++releases;
    free(ptr);
}

/* Include the actual adapter so saturation can be injected without billions
   of synthetic IRQs. No alternate implementation of dispatch is tested. */
#define calloc test_calloc
#define free test_free
#include "../../addons/libfiber_vblank/fiber_vblank.c"
#undef calloc
#undef free

kfiber_t *fiber_current(void) { return current; }
kfiber_t *fiber_main(void) { return (kfiber_t *)1; }
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code,
    asic_evt_handler handler, void *data) {
    (void)code;
    irq_handler = handler;
    irq_data = data;
    return (asic_evt_handler_entry_t){ NULL, NULL };
}
void asic_evt_remove_handler(uint16_t code) { (void)code; irq_handler = NULL; }
void asic_evt_enable(uint16_t code, uint8_t level) { (void)code; (void)level; }
void asic_evt_disable(uint16_t code, uint8_t level) { (void)code; (void)level; }

static void tick(void) {
    test_in_irq = true;
    irq_handler(ASIC_EVT_PVR_VBLANK_BEGIN, irq_data);
    test_in_irq = false;
}
static fiber_vblank_t *dispatcher;
static unsigned int wakes, count;
static uint8_t order[16];
static uint32_t observed[16];
static bool inject, stop_in_callback;

static void wake(void *data) {
    CHECK(test_in_irq);
    CHECK(data == &wakes);
    ++wakes;
}
static void callback(uint32_t frames, void *data) {
    CHECK(!test_in_irq && test_mask == 0);
    CHECK(fiber_current() != fiber_main());
    CHECK(count < sizeof(order));
    order[count] = (uintptr_t)data;
    observed[count++] = frames;
    CHECK(fiber_vblank_dispatch(dispatcher) < 0 && errno == EBUSY);
    CHECK(fiber_vblank_destroy(dispatcher) < 0 && errno == EBUSY);
    if(inject) { inject = false; tick(); tick(); }
    if(stop_in_callback) {
        CHECK(fiber_vblank_stop(dispatcher) == 0);
        CHECK(fiber_vblank_destroy(dispatcher) < 0 && errno == EBUSY);
    }
}

int main(void) {
    CHECK(vblank_init() == 0);
    CHECK(!fiber_vblank_create(0) && errno == EINVAL);
    CHECK(!fiber_vblank_create(SIZE_MAX) && errno == EINVAL);
    test_in_irq = true;
    CHECK(!fiber_vblank_create(4) && errno == EPERM);
    test_in_irq = false;
    dispatcher = fiber_vblank_create(4);
    CHECK(dispatcher);
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) < 0 && errno == EINVAL);
    CHECK(fiber_vblank_add(dispatcher, 0, NULL, NULL) < 0 && errno == EINVAL);
    CHECK(fiber_vblank_add(dispatcher, 0, callback, (void *)1) == 0);
    CHECK(fiber_vblank_add(dispatcher, 128, callback, (void *)2) == 0);
    CHECK(fiber_vblank_add(dispatcher, 128, callback, (void *)3) == 0);
    CHECK(fiber_vblank_add(dispatcher, 255, callback, (void *)4) == 0);
    CHECK(fiber_vblank_add(dispatcher, 0, callback, NULL) < 0 && errno == ENOSPC);
    fail_malloc = true;
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) < 0 && errno == ENOMEM);
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) == 0);
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) < 0 && errno == EALREADY);
    CHECK(fiber_vblank_add(dispatcher, 0, callback, NULL) < 0 && errno == EBUSY);
    CHECK(fiber_vblank_destroy(dispatcher) < 0 && errno == EBUSY);
    unsigned int baseline = allocations;
    current = NULL;
    CHECK(fiber_vblank_dispatch(dispatcher) < 0 && errno == EPERM);
    current = fiber_main();
    CHECK(fiber_vblank_dispatch(dispatcher) < 0 && errno == EPERM);
    current = (kfiber_t *)2;
    test_mask = 0xf0;
    CHECK(fiber_vblank_dispatch(dispatcher) < 0 && errno == EPERM);
    CHECK(test_mask == 0xf0);
    test_mask = 0;
    test_in_irq = true;
    CHECK(fiber_vblank_dispatch(dispatcher) < 0 && errno == EPERM);
    CHECK(fiber_vblank_stop(dispatcher) < 0 && errno == EPERM);
    CHECK(fiber_vblank_destroy(dispatcher) < 0 && errno == EPERM);
    test_in_irq = false;
    CHECK(fiber_vblank_dispatch(dispatcher) == 0);
    tick(); tick(); tick();
    CHECK(wakes == 1 && count == 0);
    inject = true;
    CHECK(fiber_vblank_dispatch(dispatcher) == 1);
    CHECK(wakes == 2 && count == 4);
    CHECK(memcmp(order, (uint8_t[]){1,3,2,4}, 4) == 0);
    for(unsigned int i = 0; i < 4; ++i) CHECK(observed[i] == 3);
    CHECK(fiber_vblank_dispatch(dispatcher) == 1);
    CHECK(count == 8 && memcmp(order, order + 4, 4) == 0);
    for(unsigned int i = 4; i < 8; ++i) CHECK(observed[i] == 2);
    CHECK(fiber_vblank_dispatch(dispatcher) == 0);
    dispatcher->pending = UINT32_MAX - 1;
    tick(); tick();
    CHECK(fiber_vblank_dispatch(dispatcher) == 1);
    for(unsigned int i = 8; i < 12; ++i) CHECK(observed[i] == UINT32_MAX);
    stop_in_callback = true;
    tick();
    CHECK(fiber_vblank_dispatch(dispatcher) == 1);
    CHECK(count == 13 && order[12] == 1);
    tick();
    CHECK(fiber_vblank_dispatch(dispatcher) == 0);
    CHECK(fiber_vblank_stop(dispatcher) == 0);
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) < 0 && errno == EALREADY);
    CHECK(allocations == baseline && irq_allocations == 0);
    CHECK(fiber_vblank_destroy(dispatcher) == 0);
    /* Shutdown before the first batch discards pending work without invoking
       user callbacks or requiring a parked consumer to be resumed here. */
    dispatcher = fiber_vblank_create(1);
    CHECK(dispatcher);
    CHECK(fiber_vblank_add(dispatcher, 0, callback, (void *)1) == 0);
    CHECK(fiber_vblank_start(dispatcher, wake, &wakes) == 0);
    tick(); tick();
    CHECK(fiber_vblank_stop(dispatcher) == 0);
    CHECK(fiber_vblank_dispatch(dispatcher) == 0 && count == 13);
    CHECK(fiber_vblank_destroy(dispatcher) == 0);
    CHECK(vblank_shutdown() == 0);
    CHECK(allocations == releases);
    printf("FIBER-VBLANK-HOST: PASS checks=%u\n", checks);
    return 0;
}
