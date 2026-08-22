/* KallistiOS ##version##

   fiber-service-sync.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SERVICE_STACK_SIZE 8192u

static alignas(32) uint8_t service_stacks[2][SERVICE_STACK_SIZE];
static fiber_service_executor_t *executor;
static fiber_service_t *service_a;
static fiber_service_t *service_b;
static kfiber_event_t *event;
static kfiber_mutex_t *mutex;
static volatile unsigned sequence;
static volatile bool failed;
static const fiber_service_message_t test_message = { 0x51, 0x5151 };

static void service_a_entry(fiber_service_t *service, void *data) {
    (void)data;

    event = fiber_event_create(false);
    mutex = fiber_mutex_create();
    if(!event || !mutex || fiber_mutex_lock(mutex) < 0)
        failed = true;

    sequence = 1;
    if(fiber_service_wait(service, 0) < 0 || sequence != 2)
        failed = true;

    if(fiber_mutex_unlock(mutex) < 0)
        failed = true;
    sequence = 3;

    if(fiber_event_wait(event) < 0 || sequence != 5)
        failed = true;

    /* The service wake that arrived during the event wait must still be
       pending after the event itself completes. */
    if(fiber_service_wait(service, 0) < 0 || sequence != 5)
        failed = true;

    if(fiber_mutex_destroy(mutex) < 0 || fiber_event_destroy(event) < 0)
        failed = true;
    mutex = NULL;
    event = NULL;
    sequence = 6;
}

static void service_b_entry(fiber_service_t *service, void *data) {
    fiber_service_message_t message;

    (void)data;

    if(sequence != 1 || !event || !mutex)
        failed = true;
    sequence = 2;

    if(fiber_mutex_lock(mutex) < 0 || sequence != 3)
        failed = true;

    if(fiber_service_receive(service, &message, 0) < 0 ||
       message.tag != test_message.tag || message.value != test_message.value)
        failed = true;
    sequence = 4;

    if(fiber_service_wait(service, 0) < 0 || sequence != 4)
        failed = true;
    if(fiber_mutex_unlock(mutex) < 0 || fiber_event_set(event) < 0)
        failed = true;
    sequence = 5;
}

static bool wait_for_terminal(void) {
    uint64_t deadline = timer_ms_gettime64() + 2000;

    while(timer_ms_gettime64() < deadline) {
        if(fiber_service_get_state(service_a) == FIBER_SERVICE_FINISHED &&
           fiber_service_get_state(service_b) == FIBER_SERVICE_FINISHED)
            return true;
        thd_pass();
    }

    return false;
}

static bool wait_for_sequence(unsigned value) {
    uint64_t deadline = timer_ms_gettime64() + 2000;

    while(sequence < value && timer_ms_gettime64() < deadline)
        thd_pass();
    return sequence >= value;
}

static void pass_executor(unsigned count) {
    while(count--)
        thd_pass();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    executor = fiber_service_executor_create();
    if(!executor)
        return EXIT_FAILURE;

    service_a = fiber_service_add(executor, service_stacks[0],
                                  sizeof(service_stacks[0]),
                                  service_a_entry, NULL);
    service_b = fiber_service_add(executor, service_stacks[1],
                                  sizeof(service_stacks[1]),
                                  service_b_entry, NULL);
    if(!service_a || !service_b ||
       fiber_service_queue_configure(service_b, 1) < 0 ||
       fiber_service_wake(service_a) < 0 ||
       fiber_service_wake(service_b) < 0 ||
       fiber_service_executor_start(executor, NULL) < 0 ||
       !wait_for_sequence(2))
        failed = true;

    /* A mailbox post must not complete B's pending mutex acquisition. */
    if(!failed && fiber_service_post(service_b, &test_message) < 0)
        failed = true;
    pass_executor(8);
    if(sequence != 2 ||
       fiber_service_get_state(service_b) != FIBER_SERVICE_WAITING)
        failed = true;

    if(!failed && fiber_service_wake(service_a) < 0)
        failed = true;
    if(!wait_for_sequence(4))
        failed = true;

    /* A service wake must not complete A's pending event wait. */
    if(!failed && fiber_service_wake(service_a) < 0)
        failed = true;
    pass_executor(8);
    if(sequence != 4 ||
       fiber_service_get_state(service_a) != FIBER_SERVICE_WAITING)
        failed = true;

    if(!failed && fiber_service_wake(service_b) < 0)
        failed = true;
    if(!wait_for_terminal())
        failed = true;

    if(fiber_service_executor_destroy(executor) < 0)
        failed = true;
    executor = NULL;

    if(failed || sequence != 6) {
        printf("Fiber service synchronization failed at sequence %u\n",
               sequence);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERSERVICESYNC sequence=%u isolated=2 preserved=1\n",
           sequence);
    return EXIT_SUCCESS;
}
