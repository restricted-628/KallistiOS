/* KallistiOS ##version##

   vblank-priority.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/vblank.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WAIT_TIMEOUT_MS 2000u
#define EXPECTED_EVENTS 7u

static volatile uint32_t event_count;
static volatile uint8_t event_order[EXPECTED_EVENTS];
static volatile uint32_t high_count;
static volatile uint32_t equal_count;
static volatile uint32_t default_count;
static volatile uint32_t low_count;
static volatile int self_remove_result = -1;
static int equal_handle = -1;

static void record_event(uint8_t value) {
    uint32_t slot = event_count;

    if(slot < EXPECTED_EVENTS)
        event_order[slot] = value;
    event_count = slot + 1;
}

static void high_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    ++high_count;
    record_event(1);
}

static void equal_self_removing_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    ++equal_count;
    record_event(2);
    self_remove_result = vblank_handler_remove(equal_handle);
}

static void default_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    ++default_count;
    record_event(3);
}

static void low_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
    ++low_count;
    record_event(4);
}

int main(int argc, char **argv) {
    static const uint8_t expected[EXPECTED_EVENTS] = { 1, 2, 3, 4, 1, 3, 4 };
    uint64_t deadline;
    int high_handle;
    int default_handle;
    int low_handle;
    unsigned int i;
    int failed = 0;

    (void)argc;
    (void)argv;

    high_handle = vblank_handler_add_prio(high_handler, NULL, 16);
    equal_handle = vblank_handler_add_prio(equal_self_removing_handler,
                                           NULL, 64);
    default_handle = vblank_handler_add(default_handler, NULL);
    low_handle = vblank_handler_add_prio(low_handler, NULL, 224);
    if(high_handle < 0 || equal_handle < 0 || default_handle < 0 ||
       low_handle < 0) {
        printf("VBLANK-PRIORITY: registration failed\n");
        return EXIT_FAILURE;
    }

    deadline = timer_ms_gettime64() + WAIT_TIMEOUT_MS;
    while(event_count < EXPECTED_EVENTS && timer_ms_gettime64() < deadline)
        thd_pass();

    if(event_count < EXPECTED_EVENTS || high_count < 2 || equal_count != 1 ||
       default_count < 2 || low_count < 2 || self_remove_result != 0)
        failed = 1;

    for(i = 0; i < EXPECTED_EVENTS; ++i) {
        if(event_order[i] != expected[i])
            failed = 1;
    }

    if(vblank_handler_remove(high_handle) < 0 ||
       vblank_handler_remove(default_handle) < 0 ||
       vblank_handler_remove(low_handle) < 0)
        failed = 1;

    printf("VBLANK-PRIORITY: %s events=%lu order=", failed ? "FAIL" : "PASS",
           (unsigned long)event_count);
    for(i = 0; i < EXPECTED_EVENTS; ++i)
        printf("%u%s", event_order[i], i + 1 == EXPECTED_EVENTS ? "\n" : ",");

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
