/* KallistiOS ##version##

   fiber.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber.h>

#include <arch/mmu.h>
#include <dc/sq.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIBER_STACK_SIZE 8192u

static alignas(32) uint8_t stack_a[FIBER_STACK_SIZE];
static alignas(32) uint8_t stack_b[FIBER_STACK_SIZE];
static alignas(32) uint8_t sq_source[64];
static alignas(32) uint8_t sq_destination[64];
static kfiber_t *main_fiber;
static kfiber_t *fiber_a;
static kfiber_t *fiber_b;
static mmucontext_t *expected_mmu;
static _Thread_local uint32_t tls_cookie;
static volatile bool helper_stop;
static volatile unsigned helper_runs;
static unsigned sequence;
static unsigned callback_count;
static bool failed;

static bool address_in_stack(const void *address, const uint8_t *stack) {
    uintptr_t value = (uintptr_t)address;
    uintptr_t base = (uintptr_t)stack;

    return value >= base && value < base + FIBER_STACK_SIZE;
}

static void switch_callback(kfiber_t *from, kfiber_t *to, void *data) {
    unsigned *count = data;

    ++*count;
    if(fiber_current() != from ||
       fiber_get_state(from) != KFIBER_STATE_RUNNING ||
       fiber_get_state(to) != KFIBER_STATE_READY)
        failed = true;
}

static void entry_a(void *data) {
    register float fp_cookie __asm__("fr12") = 123.25f;
    uint32_t stack_marker;

    __asm__ volatile("" : "+f"(fp_cookie));

    if(data != (void *)stack_a ||
       !address_in_stack(&stack_marker, stack_a) ||
       mmu_cxt_current != expected_mmu || tls_cookie != 0x55aa55aau)
        failed = true;

    thd_pass();

    if(sequence != 0)
        failed = true;

    sequence = 1;
    if(fiber_switch(fiber_b) < 0)
        failed = true;

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != 123.25f || sequence != 2)
        failed = true;

    sequence = 3;
    if(fiber_switch(main_fiber) < 0)
        failed = true;

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != 123.25f || sequence != 4)
        failed = true;

    sequence = 5;
}

static void entry_b(void *data) {
    register float fp_cookie __asm__("fr12") = -77.5f;
    uint32_t stack_marker;

    __asm__ volatile("" : "+f"(fp_cookie));

    if(data != (void *)stack_b ||
       !address_in_stack(&stack_marker, stack_b) ||
       mmu_cxt_current != expected_mmu || tls_cookie != 0x55aa55aau)
        failed = true;

    thd_pass();

    if(sequence != 1)
        failed = true;

    sequence = 2;
    if(fiber_switch(fiber_a) < 0)
        failed = true;

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != -77.5f || sequence != 3)
        failed = true;

    sequence = 4;
}

static void *helper_thread(void *data) {
    (void)data;

    while(!helper_stop) {
        ++helper_runs;
        thd_pass();
    }

    return NULL;
}

int main(int argc, char **argv) {
    kthread_t *helper;
    irq_mask_t old_irq;
    int switch_result;
    int switch_errno;
    uint32_t *sq;

    (void)argc;
    (void)argv;

    expected_mmu = mmu_cxt_current;
    tls_cookie = 0x55aa55aau;

    main_fiber = fiber_attach();
    fiber_a = fiber_create(stack_a, sizeof(stack_a), entry_a, stack_a);
    fiber_b = fiber_create(stack_b, sizeof(stack_b), entry_b, stack_b);
    if(!main_fiber || !fiber_a || !fiber_b) {
        printf("Unable to create fibers: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if(fiber_get_data(fiber_a) != stack_a ||
       fiber_get_state(main_fiber) != KFIBER_STATE_RUNNING ||
       fiber_get_state(fiber_a) != KFIBER_STATE_READY ||
       fiber_set_switch_callback(switch_callback, &callback_count) < 0) {
        printf("Fiber setup contract failed\n");
        return EXIT_FAILURE;
    }

    helper = thd_create(false, helper_thread, NULL);
    if(!helper) {
        printf("Unable to create scheduler helper\n");
        return EXIT_FAILURE;
    }

    old_irq = irq_disable();
    errno = 0;
    switch_result = fiber_switch(fiber_a);
    switch_errno = errno;
    irq_restore(old_irq);
    if(switch_result == 0 || switch_errno != EBUSY)
        failed = true;

    sq = sq_lock(stack_a);
    errno = 0;
    if(!sq || fiber_switch(fiber_a) == 0 || errno != EBUSY)
        failed = true;
    sq_unlock();

    for(size_t i = 0; i < sizeof(sq_source); ++i)
        sq_source[i] = (uint8_t)(i ^ 0x5au);
    sq_cpy(sq_destination, sq_source, sizeof(sq_source));
    if(memcmp(sq_destination, sq_source, sizeof(sq_source)))
        failed = true;

    if(fiber_switch(fiber_a) < 0 || sequence != 3)
        failed = true;
    if(fiber_switch(fiber_b) < 0 || sequence != 4 ||
       fiber_get_state(fiber_b) != KFIBER_STATE_FINISHED)
        failed = true;

    errno = 0;
    if(fiber_switch(fiber_b) == 0 || errno != EBUSY)
        failed = true;

    if(fiber_switch(fiber_a) < 0 || sequence != 5 ||
       fiber_get_state(fiber_a) != KFIBER_STATE_FINISHED)
        failed = true;

    if(callback_count != 8 || helper_runs == 0 ||
       mmu_cxt_current != expected_mmu || tls_cookie != 0x55aa55aau)
        failed = true;

    if(fiber_set_switch_callback(NULL, NULL) < 0 ||
       fiber_destroy(fiber_a) < 0 || fiber_destroy(fiber_b) < 0)
        failed = true;

    errno = 0;
    if(fiber_destroy(main_fiber) == 0 || errno != EBUSY)
        failed = true;

    helper_stop = true;
    thd_join(helper, NULL);

    if(failed) {
        printf("Fiber API test failed: sequence=%u callbacks=%u helper=%u\n",
               sequence, callback_count, helper_runs);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERAPI scheduler=%u callbacks=%u\n",
           helper_runs, callback_count);
    return EXIT_SUCCESS;
}
