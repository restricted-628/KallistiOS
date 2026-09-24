/* KallistiOS ##version##

   tmu1-channel.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

#define TMU1_TCR (*(volatile uint16_t *)0xffd8001c)
#define TMU1_TCOR (*(volatile uint32_t *)0xffd80014)
#define TMU1_TCNT (*(volatile uint32_t *)0xffd80018)

#define WAIT_TIMEOUT_MS 500u
#define TARGET_CALLBACKS 10u

static volatile uint32_t callback_count;
static volatile int callback_stop_result = -1;

static void periodic_callback(timer_channel_t *channel, void *data) {
    volatile uint32_t *count = data;

    ++*count;
    if(*count == TARGET_CALLBACKS)
        callback_stop_result = timer_channel_stop(channel);
}

int main(int argc, char **argv) {
    timer_channel_config_t config;
    timer_channel_info_t info;
    timer_channel_t *channel = NULL;
    timer_channel_t *second;
    uint64_t interval_ns;
    uint64_t deadline;
    uint32_t interval_ticks;
    uint32_t elapsed;
    uint32_t saved_tcor, saved_tcnt;
    uint16_t saved_tcr;
    irq_cb_t saved_handler;
    unsigned saved_priority;
    int failed = 0;

    (void)argc;
    (void)argv;

    if(timer_channel_ns_to_ticks(TIMER_CHANNEL_CLOCK_DIV_4, 1000000,
                                 &interval_ticks) < 0 ||
       timer_channel_ticks_to_ns(TIMER_CHANNEL_CLOCK_DIV_4, interval_ticks,
                                 &interval_ns) < 0 ||
       interval_ns < 1000000 ||
       timer_channel_elapsed_ticks(2, 8, 10, &elapsed) < 0 || elapsed != 4) {
        printf("TMU1-CHANNEL: conversion failure\n");
        return EXIT_FAILURE;
    }

    /* Standalone probe: TMU1 must not already be in use. Create a real pending
       underflow without an interrupt, then verify claim leaves it untouched. */
    if(timer_running(TMU1) || timer_prime(TMU1, 1000, 0) < 0 ||
       timer_start(TMU1) < 0)
        return EXIT_FAILURE;
    deadline = timer_ms_gettime64() + WAIT_TIMEOUT_MS;
    while(!(TMU1_TCR & (1u << 8)) && timer_ms_gettime64() < deadline)
        thd_pass();
    timer_stop(TMU1);
    errno = 0;
    second = timer_channel_claim(TMU1);
    if(second || errno != EBUSY || !(TMU1_TCR & (1u << 8))) {
        if(second)
            timer_channel_release(second);
        puts("TMU1-CHANNEL: FAIL pending underflow ownership");
        return EXIT_FAILURE;
    }
    timer_clear(TMU1);
    if(timer_prime(TMU1, 123, 0) < 0)
        return EXIT_FAILURE;
    saved_tcor = TMU1_TCOR; saved_tcnt = TMU1_TCNT; saved_tcr = TMU1_TCR;
    saved_handler = irq_get_handler(EXC_TMU1_TUNI1);
    saved_priority = irq_get_priority(IRQ_SRC_TMU1);

    channel = timer_channel_claim(TMU1);
    if(!channel) {
        printf("TMU1-CHANNEL: claim failed errno=%d\n", errno);
        return EXIT_FAILURE;
    }

    errno = 0;
    second = timer_channel_claim(TMU1);
    if(second || errno != EBUSY)
        failed = 1;

    errno = 0;
    if(timer_prime(TMU1, 60, 0) != -1 || errno != EBUSY)
        failed = 1;

    config = (timer_channel_config_t) {
        .clock = TIMER_CHANNEL_CLOCK_DIV_4,
        .period_ticks = interval_ticks,
        .irq_priority = 10,
        .callback = periodic_callback,
        .callback_data = (void *)&callback_count,
    };

    if(timer_channel_configure(channel, &config) < 0 ||
       timer_channel_start(channel) < 0)
        failed = 1;

    deadline = timer_ms_gettime64() + WAIT_TIMEOUT_MS;
    while(!failed && callback_count < TARGET_CALLBACKS &&
          timer_ms_gettime64() < deadline)
        thd_pass();

    if(callback_count != TARGET_CALLBACKS || callback_stop_result != 0 ||
       timer_channel_get_info(channel, &info) < 0 || info.running ||
       info.expirations != TARGET_CALLBACKS ||
       info.period_ticks != interval_ticks)
        failed = 1;

    if(timer_channel_release(channel) < 0) {
        failed = 1;
        channel = NULL;
    }
    else {
        channel = NULL;
    }

    irq_cb_t restored = irq_get_handler(EXC_TMU1_TUNI1);
    if(TMU1_TCOR != saved_tcor || TMU1_TCNT != saved_tcnt || TMU1_TCR != saved_tcr ||
       restored.hdl != saved_handler.hdl || restored.data != saved_handler.data ||
       irq_get_priority(IRQ_SRC_TMU1) != saved_priority || timer_running(TMU1))
        failed = 1;

    if(timer_prime(TMU1, 60, 0) < 0 || timer_start(TMU1) < 0 ||
       timer_stop(TMU1) < 0)
        failed = 1;

    printf("TMU1-CHANNEL: %s callbacks=%lu ticks=%lu interval_ns=%llu pending=1 restore=1\n",
           failed ? "FAIL" : "PASS", (unsigned long)callback_count,
           (unsigned long)interval_ticks,
           (unsigned long long)interval_ns);

    if(channel)
        timer_channel_release(channel);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
