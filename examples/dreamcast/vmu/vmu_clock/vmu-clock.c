/* KallistiOS ##version##

   vmu-clock.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/maple/vmu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WAIT_TIMEOUT_MS 2000u

static volatile int callback_done;
static vmu_clock_status_t callback_status;

static void clock_complete(maple_device_t *dev,
                           const vmu_clock_status_t *status,
                           void *user_data) {
    (void)dev;
    (void)user_data;

    callback_status = *status;
    callback_done = 1;
}

int main(int argc, char **argv) {
    const vmu_clock_time_t valid_leap_day = {
        .year = 2024, .month = 2, .day = 29, .weekday = 4
    };
    const vmu_clock_time_t invalid_leap_day = {
        .year = 2023, .month = 2, .day = 29, .weekday = 3
    };
    const vmu_clock_time_t wrong_weekday = {
        .year = 2024, .month = 2, .day = 29, .weekday = 3
    };
    const vmu_clock_time_t valid_sunday = {
        .year = 2024, .month = 3, .day = 3, .weekday = 0
    };
    const vmu_clock_time_t valid_century_leap_day = {
        .year = 2000, .month = 2, .day = 29, .weekday = 2
    };
    const vmu_clock_time_t invalid_century_leap_day = {
        .year = 2100, .month = 2, .day = 29, .weekday = 1
    };
    maple_device_t *dev;
    vmu_clock_status_t status;
    vmu_clock_status_t completed;
    vmu_clock_time_t synchronous_time;
    irq_mask_t irq;
    uint64_t deadline;
    int callback_completed;
    int failed = 0;

    (void)argc;
    (void)argv;

    if(vmu_clock_time_is_valid(&valid_leap_day) != 1 ||
       vmu_clock_time_is_valid(&invalid_leap_day) != 0 ||
       vmu_clock_time_is_valid(&wrong_weekday) != 0 ||
       vmu_clock_time_is_valid(&valid_sunday) != 1 ||
       vmu_clock_time_is_valid(&valid_century_leap_day) != 1 ||
       vmu_clock_time_is_valid(&invalid_century_leap_day) != 0) {
        printf("VMU-CLOCK: FAIL civil-time validation\n");
        return EXIT_FAILURE;
    }

    dev = maple_enum_type(0, MAPLE_FUNC_CLOCK);
    if(!dev) {
        printf("VMU-CLOCK: SKIP no clock device (civil-time tests passed)\n");
        return EXIT_SUCCESS;
    }

    if(vmu_clock_set_time_async(dev, &invalid_leap_day) != MAPLE_EINVALID ||
       vmu_clock_is_ready(dev) != 1 ||
       vmu_clock_set_completion_handler(dev, clock_complete, NULL) < 0)
        failed = 1;

    if(!failed && vmu_clock_get_time_async(dev) != MAPLE_EOK)
        failed = 1;

    deadline = timer_ms_gettime64() + WAIT_TIMEOUT_MS;
    while(!failed && !callback_done && timer_ms_gettime64() < deadline)
        thd_pass();

    /* Copy callback-owned state while its IRQ source is fenced. */
    irq = irq_disable();
    callback_completed = callback_done;
    completed = callback_status;
    irq_restore(irq);

    if(!callback_completed || completed.busy ||
       completed.operation != VMU_CLOCK_OPERATION_GET ||
       completed.result != MAPLE_EOK ||
       !completed.time_valid || completed.submitted_sequence == 0 ||
       completed.completed_sequence != completed.submitted_sequence ||
       vmu_clock_time_is_valid(&completed.time) != 1 ||
       vmu_clock_get_status(dev, &status) < 0 || status.busy ||
       status.completed_sequence != completed.completed_sequence)
        failed = 1;

    if(!failed && (vmu_clock_get_time(dev, &synchronous_time) != MAPLE_EOK ||
                   vmu_clock_time_is_valid(&synchronous_time) != 1 ||
                   synchronous_time.year != completed.time.year ||
                   synchronous_time.month != completed.time.month ||
                   synchronous_time.day != completed.time.day))
        failed = 1;

    if(vmu_clock_set_completion_handler(dev, NULL, NULL) < 0)
        failed = 1;

    printf("VMU-CLOCK: %s date=%04u-%02u-%02u %02u:%02u:%02u seq=%lu\n",
           failed ? "FAIL" : "PASS", completed.time.year,
           completed.time.month, completed.time.day, completed.time.hour,
           completed.time.minute, completed.time.second,
           (unsigned long)completed.completed_sequence);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
