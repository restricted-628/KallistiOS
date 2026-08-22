/* KallistiOS ##version##

   vblank-handler-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/vblank.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

bool vblank_test_irq_context;

static asic_evt_handler dispatch_handler;
static void *dispatch_data;
static unsigned int allocation_count;
static unsigned int free_count;
static unsigned int allocator_calls_in_irq;
static unsigned int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

void *vblank_test_malloc(size_t bytes) {
    if(vblank_test_irq_context)
        ++allocator_calls_in_irq;
    ++allocation_count;
    return malloc(bytes);
}

void vblank_test_free(void *ptr) {
    if(vblank_test_irq_context)
        ++allocator_calls_in_irq;
    ++free_count;
    free(ptr);
}

asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code,
                                              asic_evt_handler handler,
                                              void *data) {
    asic_evt_handler_entry_t previous = { dispatch_handler, dispatch_data };

    CHECK(code == ASIC_EVT_PVR_VBLANK_BEGIN);
    dispatch_handler = handler;
    dispatch_data = data;
    return previous;
}

void asic_evt_remove_handler(uint16_t code) {
    CHECK(code == ASIC_EVT_PVR_VBLANK_BEGIN);
    dispatch_handler = NULL;
    dispatch_data = NULL;
}

void asic_evt_disable(uint16_t code, uint8_t irqlevel) {
    CHECK(code == ASIC_EVT_PVR_VBLANK_BEGIN);
    CHECK(irqlevel == ASIC_IRQ_DEFAULT);
}

void asic_evt_enable(uint16_t code, uint8_t irqlevel) {
    CHECK(code == ASIC_EVT_PVR_VBLANK_BEGIN);
    CHECK(irqlevel == ASIC_IRQ_DEFAULT);
}

static uint8_t events[32];
static size_t event_count;
static int self_handle;
static int victim_handle;

static void record(uint8_t event) {
    if(event_count < sizeof(events))
        events[event_count] = event;
    ++event_count;
}

static void high_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(1);
}

static void self_removing_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(2);
    CHECK(vblank_handler_remove(self_handle) == 0);
}

static void default_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(3);
}

static void equal_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(4);
}

static void low_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(5);
}

static void removing_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(6);
    CHECK(vblank_handler_remove(victim_handle) == 0);
}

static void victim_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(7);
}

static void tail_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    record(8);
}

static void dispatch(void) {
    CHECK(dispatch_handler != NULL);
    vblank_test_irq_context = true;
    dispatch_handler(ASIC_EVT_PVR_VBLANK_BEGIN, dispatch_data);
    vblank_test_irq_context = false;
}

static void check_events(const uint8_t *expected, size_t count) {
    size_t index;

    CHECK(event_count == count);
    for(index = 0; index < count && index < event_count; ++index)
        CHECK(events[index] == expected[index]);
}

static void test_priority_and_self_removal(void) {
    static const uint8_t first[] = { 1, 2, 3, 4, 5 };
    static const uint8_t second[] = { 1, 3, 4, 5 };
    int high;
    int default_id;
    int equal;
    int low;
    int trigger;
    unsigned int frees_before;

    high = vblank_handler_add_prio(high_handler, NULL, 16);
    self_handle = vblank_handler_add_prio(self_removing_handler, NULL, 64);
    default_id = vblank_handler_add(default_handler, NULL);
    equal = vblank_handler_add_prio(equal_handler, NULL,
                                    VBLANK_PRIORITY_DEFAULT);
    low = vblank_handler_add_prio(low_handler, NULL, 224);
    CHECK(high > 0 && self_handle > 0 && default_id > 0 && equal > 0 &&
          low > 0);

    frees_before = free_count;
    event_count = 0;
    dispatch();
    check_events(first, sizeof(first));
    CHECK(free_count == frees_before);
    CHECK(allocator_calls_in_irq == 0);

    event_count = 0;
    dispatch();
    check_events(second, sizeof(second));

    /* A thread-context registration reclaims the self-removed record before
       allocating its own node. */
    trigger = vblank_handler_add_prio(tail_handler, NULL, 240);
    CHECK(trigger > 0);
    CHECK(free_count == frees_before + 1);

    CHECK(vblank_handler_remove(high) == 0);
    CHECK(vblank_handler_remove(default_id) == 0);
    CHECK(vblank_handler_remove(equal) == 0);
    CHECK(vblank_handler_remove(low) == 0);
    CHECK(vblank_handler_remove(trigger) == 0);
}

static void test_remove_later_handler(void) {
    static const uint8_t expected[] = { 6, 8 };
    int remover;
    int tail;
    unsigned int frees_before = free_count;

    remover = vblank_handler_add_prio(removing_handler, NULL, 10);
    victim_handle = vblank_handler_add_prio(victim_handler, NULL, 20);
    tail = vblank_handler_add_prio(tail_handler, NULL, 30);
    CHECK(remover > 0 && victim_handle > 0 && tail > 0);

    event_count = 0;
    dispatch();
    check_events(expected, sizeof(expected));
    CHECK(free_count == frees_before);
    CHECK(allocator_calls_in_irq == 0);

    CHECK(vblank_handler_remove(remover) == 0);
    CHECK(free_count == frees_before + 2);
    CHECK(vblank_handler_remove(tail) == 0);
}

static void test_errors(void) {
    errno = 0;
    CHECK(vblank_handler_add_prio(NULL, NULL, 0) < 0 && errno == EINVAL);

    vblank_test_irq_context = true;
    errno = 0;
    CHECK(vblank_handler_add(high_handler, NULL) < 0 && errno == EPERM);
    errno = 0;
    CHECK(vblank_shutdown() < 0 && errno == EPERM);
    vblank_test_irq_context = false;

    errno = 0;
    CHECK(vblank_handler_remove(-1) < 0 && errno == ENOENT);
}

int main(void) {
    CHECK(vblank_init() == 0);
    test_errors();
    test_priority_and_self_removal();
    test_remove_later_handler();
    CHECK(allocator_calls_in_irq == 0);
    CHECK(vblank_shutdown() == 0);
    CHECK(allocation_count == free_count);

    if(failures) {
        fprintf(stderr, "%u VBlank handler checks failed\n", failures);
        return EXIT_FAILURE;
    }

    puts("VBlank handler tests passed");
    return EXIT_SUCCESS;
}
