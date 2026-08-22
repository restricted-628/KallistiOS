/* KallistiOS ##version##

   fiber-service-queue.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/vblank.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE_STACK_SIZE 8192u
#define TEST_TIMEOUT_MS 2000u

static alignas(32) uint8_t service_stack[SERVICE_STACK_SIZE];
static fiber_service_executor_t *executor;
static fiber_service_t *service;
static volatile unsigned sequence;
static volatile unsigned irq_posts;
static volatile uint64_t timeout_elapsed;
static volatile bool failed;
static volatile bool cancelled;

static void service_entry(fiber_service_t *self, void *data) {
    const fiber_service_message_t expected[] = {
        { 1, 0x11 },
        { 2, 0x22 },
        { 3, 0x33 },
    };
    fiber_service_message_t message;
    uint64_t started;

    (void)data;
    for(size_t i = 0; i < 2; ++i) {
        if(fiber_service_receive(self, &message, 0) < 0 ||
           message.tag != expected[i].tag ||
           message.value != expected[i].value)
            failed = true;
    }

    sequence = 2;
    if(fiber_service_receive(self, &message,
                             timer_ms_gettime64() + 1000) < 0 ||
       message.tag != expected[2].tag || message.value != expected[2].value)
        failed = true;
    sequence = 3;

    started = timer_ms_gettime64();
    errno = 0;
    if(fiber_service_receive(self, &message, started + 40) == 0 ||
       errno != ETIMEDOUT)
        failed = true;
    timeout_elapsed = timer_ms_gettime64() - started;
    if(timeout_elapsed < 30 || timeout_elapsed > 500)
        failed = true;
    sequence = 4;

    errno = 0;
    if(fiber_service_receive(self, &message, 0) < 0 && errno == ECANCELED)
        cancelled = true;
    else
        failed = true;
    sequence = 5;
}

static void irq_post_handler(uint32_t code, void *data) {
    fiber_service_t *destination = data;
    const fiber_service_message_t message = { 3, 0x33 };

    (void)code;
    if(!irq_posts && fiber_service_post(destination, &message) == 0)
        ++irq_posts;
}

static bool wait_for_sequence(unsigned value) {
    uint64_t deadline = timer_ms_gettime64() + TEST_TIMEOUT_MS;

    while(sequence < value && timer_ms_gettime64() < deadline)
        thd_pass();
    return sequence >= value;
}

int main(int argc, char **argv) {
    const fiber_service_message_t first = { 1, 0x11 };
    const fiber_service_message_t second = { 2, 0x22 };
    const fiber_service_message_t overflow = { 99, 0x99 };
    fiber_service_queue_info_t info;
    int vblank_handle = -1;

    (void)argc;
    (void)argv;

    executor = fiber_service_executor_create();
    if(!executor)
        goto startup_failed;

    service = fiber_service_add(executor, service_stack, sizeof(service_stack),
                                service_entry, NULL);
    if(!service || fiber_service_queue_configure(service, 2) < 0 ||
       fiber_service_post(service, &first) < 0 ||
       fiber_service_post(service, &second) < 0)
        goto startup_failed;

    errno = 0;
    if(fiber_service_post(service, &overflow) == 0 || errno != EAGAIN)
        failed = true;

    if(fiber_service_executor_start(executor, NULL) < 0 ||
       !wait_for_sequence(2))
        goto startup_failed;

    vblank_handle = vblank_handler_add(irq_post_handler, service);
    if(vblank_handle < 0 || !wait_for_sequence(4) || irq_posts != 1)
        failed = true;
    if(vblank_handle >= 0)
        vblank_handler_remove(vblank_handle);

    if(fiber_service_get_queue_info(service, &info) < 0 ||
       info.capacity != 2 || info.queued != 0 ||
       info.high_watermark != 2 || info.posted != 3 ||
       info.received != 3 || info.rejected != 1)
        failed = true;

    if(fiber_service_executor_destroy(executor) < 0)
        failed = true;
    executor = NULL;

    if(failed || sequence != 5 || !cancelled) {
        printf("Fiber mailbox failed: sequence=%u irq=%u timeout=%llu\n",
               sequence, irq_posts, (unsigned long long)timeout_elapsed);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERQUEUE posted=%llu received=%llu rejected=%llu "
           "irq=%u cancelled=%u\n",
           (unsigned long long)info.posted,
           (unsigned long long)info.received,
           (unsigned long long)info.rejected, irq_posts, cancelled);
    return EXIT_SUCCESS;

startup_failed:
    if(vblank_handle >= 0)
        vblank_handler_remove(vblank_handle);
    printf("Unable to run fiber mailbox probe: %s\n", strerror(errno));
    if(executor)
        fiber_service_executor_destroy(executor);
    return EXIT_FAILURE;
}
