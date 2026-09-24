/* KallistiOS ##version##

   timer-event.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TIMEOUT_MS 2000u

static semaphore_t one_shot_done;
static semaphore_t periodic_done;
static volatile unsigned one_shot_runs;
static volatile unsigned periodic_runs;
static volatile unsigned cancelled_runs;
static volatile int self_rearm_result = -1;
static volatile int self_cancel_result = -1;
static volatile int self_destroy_result = -1;
static volatile int self_destroy_errno;

static int count_timer_threads(kthread_t *thread, void *data) {
    unsigned *count = data;

    if(strcmp(thd_get_label(thread), "[timer-events]") == 0)
        ++*count;
    return 0;
}

static unsigned timer_thread_count(void) {
    unsigned count = 0;

    irq_disable_scoped();
    thd_each(count_timer_threads, &count);
    return count;
}

static bool one_shot_callback(timer_event_t *timer, void *data) {
    (void)data;

    if(++one_shot_runs == 1)
        self_rearm_result = timer_event_arm(timer, 10, 0);
    else {
        errno = 0;
        self_destroy_result = timer_event_destroy(timer);
        self_destroy_errno = errno;
        sem_signal(&one_shot_done);
    }
    return false;
}

static bool periodic_callback(timer_event_t *timer, void *data) {
    (void)timer;
    (void)data;

    if(++periodic_runs == 3) {
        self_cancel_result = timer_event_cancel(timer);
        sem_signal(&periodic_done);
    }
    return true;
}

static bool cancelled_callback(timer_event_t *timer, void *data) {
    (void)timer;
    (void)data;
    ++cancelled_runs;
    return false;
}

int main(int argc, char **argv) {
    timer_event_t *one_shot;
    timer_event_t *periodic;
    timer_event_t *cancelled;
    timer_event_info_t one_shot_info;
    timer_event_info_t periodic_info;
    unsigned threads_before;
    unsigned threads_created;
    unsigned threads_after;
    int failed = 0;

    (void)argc;
    (void)argv;

    sem_init(&one_shot_done, 0);
    sem_init(&periodic_done, 0);
    threads_before = timer_thread_count();

    one_shot = timer_event_create(one_shot_callback, NULL);
    periodic = timer_event_create(periodic_callback, NULL);
    cancelled = timer_event_create(cancelled_callback, NULL);
    if(!one_shot || !periodic || !cancelled ||
       timer_thread_count() != threads_before) {
        printf("TIMER-EVENT: creation failed or allocated eagerly\n");
        return EXIT_FAILURE;
    }

    if(timer_event_arm(one_shot, 5, 0) < 0 ||
       timer_event_arm(periodic, 5, 10) < 0 ||
       timer_event_arm(cancelled, 1000, 0) < 0)
        failed = 1;
    threads_created = timer_thread_count();
    if(threads_created != threads_before + 1)
        failed = 1;

    if(timer_event_cancel(cancelled) < 0 ||
       sem_wait_timed(&one_shot_done, TEST_TIMEOUT_MS) < 0 ||
       sem_wait_timed(&periodic_done, TEST_TIMEOUT_MS) < 0)
        failed = 1;

    if(timer_event_get_info(one_shot, &one_shot_info) < 0 ||
       timer_event_get_info(periodic, &periodic_info) < 0 ||
       one_shot_runs != 2 || periodic_runs != 3 || cancelled_runs != 0 ||
       self_rearm_result != 0 || self_cancel_result != 0 ||
       self_destroy_result != -1 || self_destroy_errno != EDEADLK ||
       one_shot_info.armed ||
       one_shot_info.running || one_shot_info.expirations != 2 ||
       periodic_info.armed || periodic_info.running ||
       periodic_info.expirations != 3)
        failed = 1;

    if(timer_event_destroy(cancelled) < 0 ||
       timer_event_destroy(periodic) < 0 ||
       timer_event_destroy(one_shot) < 0)
        failed = 1;
    threads_after = timer_thread_count();
    if(threads_after != threads_before)
        failed = 1;

    sem_destroy(&periodic_done);
    sem_destroy(&one_shot_done);

    printf("TIMER-EVENT: %s threads=%u,%u,%u runs=%u,%u,%u\n",
           failed ? "FAIL" : "PASS", threads_before, threads_created,
           threads_after, one_shot_runs, periodic_runs, cancelled_runs);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
