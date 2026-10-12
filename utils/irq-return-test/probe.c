/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Diagnostic-only live SR observation at the first restore boundary. */
#include <kos.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

/* last live SR, return count, bitwise AND of all observed live SR values */
volatile uint32_t probe_observations[3];
static _Thread_local uint32_t tls_pattern;
static volatile unsigned trap_calls;
static atomic_uint child_started, child_ticks;
static atomic_int child_done, failed;
static uint32_t trap_saved_sr;
extern void probe_trap(uint32_t samples[4]);

static void trap_handler(irq_t code, irq_context_t *context, void *data) {
    (void)code;
    (void)data;
    trap_saved_sr = context->sr;
    ++trap_calls;
}

static uint32_t read_sr(void) {
    uint32_t sr;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    return sr;
}

static void *child(void *arg) {
    (void)arg;
    /* Newly created contexts have BL/RB clear. TLS/errno belong to this thread. */
    if(read_sr() & 0x30000000u)
        failed = 1;
    tls_pattern = 0x2468ace0u;
    errno = EDOM;
    child_started = 1;
    /* No voluntary blocking: the main thread must resume by preemption while
       this child remains runnable. A time limit lets a bad scheduler escape. */
    uint64_t until = timer_ms_gettime64() + 2000;
    while(!child_done && timer_ms_gettime64() < until)
        ++child_ticks;
    if(!child_done || tls_pattern != 0x2468ace0u || errno != EDOM)
        failed = 1;
    return NULL;
}

int main(void) {
    uint32_t samples[4];
    irq_mask_t old = irq_disable();
    probe_observations[0] = 0;
    probe_observations[1] = 0;
    probe_observations[2] = UINT32_MAX;
    tls_pattern = 0x13579bdfu;
    errno = ERANGE;
    if(irq_set_handler(IRQ_TRAP_CODE(253), trap_handler, NULL) < 0)
        return 1;
    probe_trap(samples);
    if(trap_calls != 1 || samples[0] != samples[1] ||
       trap_saved_sr != samples[0] || samples[2] != samples[3] ||
       probe_observations[1] != 1 ||
       (probe_observations[0] & 0x30000000u) != 0x30000000u)
        failed = 1;
    irq_set_handler(IRQ_TRAP_CODE(253), NULL, NULL);
    irq_restore(old);

    kthread_t *worker = thd_create(0, child, NULL);
    if(!worker)
        return 1;
    errno = ERANGE;
    /* Voluntary blocking and first activation take the forced return path.
       While child spins, the timer must select main again. */
    thd_pass();
    if(!child_started || !child_ticks || tls_pattern != 0x13579bdfu ||
       errno != ERANGE || (read_sr() & 0x30000000u) != 0)
        failed = 1;
    child_done = 1;
    if(thd_join(worker, NULL) < 0)
        failed = 1;
    if(tls_pattern != 0x13579bdfu || (read_sr() & 0x30000000u) != 0)
        failed = 1;
    old = irq_disable();
    uint32_t count = probe_observations[1];
    uint32_t common = probe_observations[2];
    irq_restore(old);
    if(count < 3 || (common & 0x30000000u) != 0x30000000u)
        failed = 1;
    printf("IRQ return probe: %s (%lu returns, common SR=%08lx)\n",
           failed ? "FAIL" : "PASS", (unsigned long)count, (unsigned long)common);
    return failed ? 1 : 0;
}
