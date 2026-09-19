/* KallistiOS ##version##

   fiber-context-probe.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_STACK_SIZE 8192u

typedef struct __attribute__((aligned(8))) probe_context {
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t r13;
    uint32_t r14;
    uint32_t r15;
    uint32_t fr12;
    uint32_t fr13;
    uint32_t fr14;
    uint32_t fr15;
    uint32_t pr;
    uint32_t sr;
} probe_context_t;

typedef struct probe_fiber {
    probe_context_t context;
    void (*entry)(void);
    bool finished;
} probe_fiber_t;

extern void probe_context_switch(probe_context_t *from,
                                 const probe_context_t *to);

static alignas(32) uint8_t stack_a[PROBE_STACK_SIZE];
static alignas(32) uint8_t stack_b[PROBE_STACK_SIZE];
static probe_fiber_t main_fiber;
static probe_fiber_t fiber_a;
static probe_fiber_t fiber_b;
static probe_fiber_t *current_fiber;
static volatile unsigned sequence;
static volatile bool failed;
static volatile bool helper_stop;
static volatile unsigned helper_runs;
static volatile unsigned bounds_queries;

/* The low-level probe deliberately does not link the public fiber runtime.
   Provide the same optional scheduler hook for its example-local contexts. */
static bool probe_stack_bounds(const kthread_t *owner, uintptr_t sp,
                               uintptr_t *base_out, size_t *size_out) {
    uintptr_t base;

    if(owner != thd_get_current() || current_fiber == &main_fiber)
        return false;

    base = current_fiber == &fiber_a ? (uintptr_t)stack_a
                                    : (uintptr_t)stack_b;
    if(sp < base || sp - base > PROBE_STACK_SIZE)
        return false;

    ++bounds_queries;
    if(base_out)
        *base_out = base;
    if(size_out)
        *size_out = PROBE_STACK_SIZE;
    return true;
}

static void switch_to(probe_fiber_t *to) {
    probe_fiber_t *from = current_fiber;
    irq_mask_t old_irq = irq_disable();

    current_fiber = to;
    probe_context_switch(&from->context, &to->context);
    irq_restore(old_irq);
}

static void fiber_trampoline(void) __attribute__((noreturn));

static void fiber_trampoline(void) {
    current_fiber->entry();
    current_fiber->finished = true;
    switch_to(&main_fiber);
    abort();
}

static void context_init(probe_fiber_t *fiber, void *stack,
                         size_t stack_size, void (*entry)(void), uint32_t sr) {
    uintptr_t stack_top = ((uintptr_t)stack + stack_size) & ~(uintptr_t)7;

    memset(fiber, 0, sizeof(*fiber));
    fiber->context.r15 = stack_top;
    fiber->context.pr = (uintptr_t)fiber_trampoline;
    fiber->context.sr = sr;
    fiber->entry = entry;
}

static void entry_a(void) {
    register float fp_cookie __asm__("fr12") = 123.25f;
    uint32_t stack_marker;

    __asm__ volatile("" : "+f"(fp_cookie));

    if((uintptr_t)&stack_marker < (uintptr_t)stack_a ||
       (uintptr_t)&stack_marker >= (uintptr_t)stack_a + sizeof(stack_a))
        failed = true;

    thd_pass();

    if(sequence != 0)
        failed = true;

    sequence = 1;
    switch_to(&fiber_b);

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != 123.25f)
        failed = true;

    if(sequence != 2)
        failed = true;

    sequence = 3;
    switch_to(&main_fiber);

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != 123.25f)
        failed = true;

    if(sequence != 4)
        failed = true;

    sequence = 5;
}

static void entry_b(void) {
    register float fp_cookie __asm__("fr12") = -77.5f;
    uint32_t stack_marker;

    __asm__ volatile("" : "+f"(fp_cookie));

    if((uintptr_t)&stack_marker < (uintptr_t)stack_b ||
       (uintptr_t)&stack_marker >= (uintptr_t)stack_b + sizeof(stack_b))
        failed = true;

    thd_pass();

    if(sequence != 1)
        failed = true;

    sequence = 2;
    switch_to(&fiber_a);

    __asm__ volatile("" : "+f"(fp_cookie));
    if(fp_cookie != -77.5f)
        failed = true;

    if(sequence != 3)
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
    irq_mask_t old_irq;
    uint32_t initial_sr;
    kthread_t *helper;

    (void)argc;
    (void)argv;

    old_irq = irq_disable();
    initial_sr = old_irq;
    irq_restore(old_irq);

    helper = thd_create(false, helper_thread, NULL);
    if(!helper) {
        printf("Unable to create scheduler helper\n");
        return EXIT_FAILURE;
    }

    memset(&main_fiber, 0, sizeof(main_fiber));
    main_fiber.context.sr = initial_sr;
    current_fiber = &main_fiber;
    _thd_continuation_stack_resolver_set(probe_stack_bounds);
    context_init(&fiber_a, stack_a, sizeof(stack_a), entry_a, initial_sr);
    context_init(&fiber_b, stack_b, sizeof(stack_b), entry_b, initial_sr);

    switch_to(&fiber_a);
    if(sequence != 3 || fiber_a.finished || fiber_b.finished)
        failed = true;

    switch_to(&fiber_b);
    if(sequence != 4 || !fiber_b.finished)
        failed = true;

    switch_to(&fiber_a);
    if(sequence != 5 || !fiber_a.finished)
        failed = true;

    if(helper_runs == 0 || bounds_queries == 0)
        failed = true;

    helper_stop = true;
    thd_join(helper, NULL);

    if(failed) {
        printf("Fiber context probe failed at sequence %u\n", sequence);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERCTX scheduler=%u bounds=%u\n",
           helper_runs, bounds_queries);
    return EXIT_SUCCESS;
}
