/* KallistiOS ##version##

   fiber-runtime-probe.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber.h>
#include <dc/matrix.h>

#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_SIZE 8192u
/* Compare control state, not ABI-clobbered T/Q/M arithmetic flags. */
#define SR_CONTROL 0x700080f0u
#define SR_TASK    0x40000000u
#define COOKIE    0x12345678u

enum {
    FAIL_IDENTITY = 1u, FAIL_SR = 2u, FAIL_STACK = 4u,
    FAIL_REGS = 8u, FAIL_MATRIX = 16u, FAIL_PREEMPT = 32u,
    FAIL_API = 64u
};

static alignas(32) uint8_t stacks[2][STACK_SIZE];
static kfiber_t *children[2], *main_fiber;
static kthread_t *carrier;
static kthread_key_t key;
static _Thread_local uint32_t tls_cookie;
static struct _reent *carrier_reent;
static uint32_t *carrier_tls;
static int *carrier_errno;
static uint32_t carrier_gbr;
static unsigned failures;
static volatile bool stop_helper, preempt_phase;
static volatile unsigned samples[2];
static uint32_t last_raw_sp[2], last_logical_sp[2];
static volatile int active_child = -1;
static unsigned helper_failures;
static matrix_t matrices[4] __attribute__((aligned(32)));

static uint32_t read_sr(void) {
    uint32_t value;
    __asm__ volatile("stc sr,%0" : "=r"(value));
    return value;
}

static uint32_t read_gbr(void) {
    uint32_t value;
    __asm__ volatile("stc gbr,%0" : "=r"(value));
    return value;
}

static bool matrix_matches(unsigned which) {
    matrix_t actual __attribute__((aligned(32)));
    mat_store(&actual);
    return memcmp(actual, matrices[which], sizeof(actual)) == 0;
}

static void check_carrier(int expected_errno) {
    if(thd_get_current() != carrier || read_gbr() != carrier_gbr ||
       &tls_cookie != carrier_tls || tls_cookie != COOKIE ||
       _impure_ptr != carrier_reent || &errno != carrier_errno ||
       errno != expected_errno || kthread_getspecific(key) != carrier)
        failures |= FAIL_IDENTITY;
    if((read_sr() & SR_CONTROL) != SR_TASK)
        failures |= FAIL_SR;
}

static void child_entry(void *data) {
    unsigned id = (uintptr_t)data;
    register uint32_t gp_cookie __asm__("r8") = COOKIE + id;
    register float fp_cookie __asm__("fr12") = 123.25f + id;
    uint32_t marker;
    unsigned before;
    uint64_t deadline;

    __asm__ volatile("" : "+r"(gp_cookie), "+f"(fp_cookie));
    if((uintptr_t)&marker < (uintptr_t)stacks[id] ||
       (uintptr_t)&marker >= (uintptr_t)stacks[id] + STACK_SIZE)
        failures |= FAIL_STACK;
    check_carrier(EDOM);
    mat_load(&matrices[id]);

    /* No yield/block in this phase: helper progress requires preemption of
       this child continuation. The helper samples the saved carrier context. */
    active_child = id;
    before = samples[id];
    deadline = timer_ms_gettime64() + 2000;
    preempt_phase = true;
    while(samples[id] - before < 2 && timer_ms_gettime64() < deadline)
        __asm__ volatile("nop");
    preempt_phase = false;
    if(samples[id] - before < 2)
        failures |= FAIL_PREEMPT;
    check_carrier(EDOM);
    if(!matrix_matches(id))
        failures |= FAIL_MATRIX;

    /* Forced-return path, first runnable helper activation, and a real wait. */
    thd_pass();
    check_carrier(EDOM);
    thd_sleep(10);
    /* Current genwait timeout writes EAGAIN into the carrier's errno. */
    check_carrier(EAGAIN);
    errno = EDOM;
    if(!matrix_matches(id))
        failures |= FAIL_MATRIX;
    __asm__ volatile("" : "+r"(gp_cookie), "+f"(fp_cookie));
    if(gp_cookie != COOKIE + id || fp_cookie != 123.25f + id)
        failures |= FAIL_REGS;

    active_child = -1;
    if(fiber_switch(main_fiber) < 0)
        failures |= FAIL_API;

    /* Both the main fiber and the other child replaced XMTRX meanwhile. */
    check_carrier(EDOM);
    if(!matrix_matches(id))
        failures |= FAIL_MATRIX;
    __asm__ volatile("" : "+r"(gp_cookie), "+f"(fp_cookie));
    if(gp_cookie != COOKIE + id || fp_cookie != 123.25f + id)
        failures |= FAIL_REGS;
}

static void *helper_entry(void *data) {
    register uint32_t gp_cookie __asm__("r8") = ~COOKIE;
    register float fp_cookie __asm__("fr12") = -77.5f;
    kthread_t *self = thd_get_current();
    (void)data;

    __asm__ volatile("" : "+r"(gp_cookie), "+f"(fp_cookie));
    if((read_sr() & SR_CONTROL) != SR_TASK)
        helper_failures |= FAIL_SR;
    tls_cookie = ~COOKIE;
    if(kthread_setspecific(key, self) < 0)
        helper_failures |= FAIL_API;
    errno = ERANGE;

    while(!stop_helper) {
        irq_mask_t old_irq = irq_disable();
        int id = active_child;

        if(preempt_phase && id >= 0) {
            const irq_context_t *ctx = &carrier->context;
            uintptr_t base;
            size_t size;
            uint32_t raw_sp = CONTEXT_SP(*ctx);
            uint32_t sp = irq_context_stack_pointer(ctx);

            if(!_thd_continuation_stack_bounds(carrier, sp, &base, &size) ||
               base != (uintptr_t)stacks[id] || size != STACK_SIZE ||
               sp < base || sp >= base + size)
                helper_failures |= FAIL_STACK;
            /* Retain both observations; a soft-gUSA marker is not an SP. */
            last_raw_sp[id] = raw_sp;
            last_logical_sp[id] = sp;
            if(ctx->gbr != carrier_gbr ||
               (ctx->sr & SR_CONTROL) != SR_TASK)
                helper_failures |= FAIL_IDENTITY;
            ++samples[id];
        }
        irq_restore(old_irq);

        mat_load(&matrices[3]);
        thd_pass();
        __asm__ volatile("" : "+r"(gp_cookie), "+f"(fp_cookie));
        if(gp_cookie != ~COOKIE || fp_cookie != -77.5f)
            helper_failures |= FAIL_REGS;
        if(!matrix_matches(3))
            helper_failures |= FAIL_MATRIX;
        if(tls_cookie != ~COOKIE || &tls_cookie == carrier_tls ||
           _impure_ptr == carrier_reent || &errno == carrier_errno ||
           errno != ERANGE || kthread_getspecific(key) != self)
            helper_failures |= FAIL_IDENTITY;
        if((read_sr() & SR_CONTROL) != SR_TASK)
            helper_failures |= FAIL_SR;
    }
    return NULL;
}

int main(int argc, char **argv) {
    kthread_t *helper;
    (void)argc;
    (void)argv;

    for(unsigned i = 0; i < 4; ++i)
        for(unsigned r = 0; r < 4; ++r)
            for(unsigned c = 0; c < 4; ++c)
                matrices[i][r][c] = (float)(i * 32 + r * 4 + c + 1);

    carrier = thd_get_current();
    carrier_gbr = read_gbr();
    carrier_reent = _impure_ptr;
    carrier_tls = &tls_cookie;
    carrier_errno = &errno;
    tls_cookie = COOKIE;
    if(kthread_key_create(&key, NULL) < 0 ||
       kthread_setspecific(key, carrier) < 0)
        return EXIT_FAILURE;

    mat_load(&matrices[2]);
    main_fiber = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!main_fiber)
        return EXIT_FAILURE;
    for(unsigned i = 0; i < 2; ++i) {
        children[i] = fiber_create(stacks[i], STACK_SIZE, child_entry,
                                   (void *)(uintptr_t)i);
        if(!children[i])
            return EXIT_FAILURE;
    }

    helper = thd_create(false, helper_entry, NULL);
    if(!helper)
        return EXIT_FAILURE;
    errno = EDOM;

    for(unsigned pass = 0; pass < 2; ++pass) {
        for(unsigned i = 0; i < 2; ++i) {
            if(fiber_switch(children[i]) < 0)
                failures |= FAIL_API;
            check_carrier(EDOM);
            if(!matrix_matches(2))
                failures |= FAIL_MATRIX;
            if(fiber_get_state(children[i]) !=
               (pass ? KFIBER_STATE_FINISHED : KFIBER_STATE_READY))
                failures |= FAIL_API;
        }
    }

    stop_helper = true;
    if(thd_join(helper, NULL) < 0)
        failures |= FAIL_API;
    failures |= helper_failures;
    for(unsigned i = 0; i < 2; ++i)
        if(fiber_destroy(children[i]) < 0)
            failures |= FAIL_API;
    if(kthread_key_delete(key) < 0)
        failures |= FAIL_API;

    printf("KOSFIBERRUNTIME sp raw=%08lx,%08lx logical=%08lx,%08lx\n",
           (unsigned long)last_raw_sp[0], (unsigned long)last_raw_sp[1],
           (unsigned long)last_logical_sp[0], (unsigned long)last_logical_sp[1]);
    printf("KOSFIBERRUNTIME core preempt=%u,%u failures=0x%x %s\n",
           samples[0], samples[1], failures, failures ? "FAIL" : "PASS");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
