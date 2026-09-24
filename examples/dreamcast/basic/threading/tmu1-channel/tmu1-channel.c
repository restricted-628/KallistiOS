/* KallistiOS ##version##

   tmu1-channel.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

    if(timer_prime(TMU1, 60, 0) < 0 || timer_start(TMU1) < 0 ||
       timer_stop(TMU1) < 0)
        failed = 1;

    printf("TMU1-CHANNEL: %s callbacks=%lu ticks=%lu interval_ns=%llu\n",
           failed ? "FAIL" : "PASS", (unsigned long)callback_count,
           (unsigned long)interval_ticks,
           (unsigned long long)interval_ns);

    if(channel)
        timer_channel_release(channel);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
