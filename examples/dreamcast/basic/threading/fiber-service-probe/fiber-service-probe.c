/* KallistiOS ##version##

   fiber-service-probe.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber_service.h>

#include <arch/mmu.h>
#include <dc/sq.h>
#include <dc/vblank.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SERVICE_STACK_SIZE 8192u
#define TEST_TIMEOUT_MS 2000u

static alignas(32) uint8_t service_stacks[3][SERVICE_STACK_SIZE];
static fiber_service_executor_t *executor;
static fiber_service_t *service_a;
static fiber_service_t *service_b;
static fiber_service_t *service_c;
static kthread_t *executor_thread;
static mmucontext_t *expected_mmu;
static _Thread_local uint32_t service_tls;
static volatile unsigned irq_wakes;
static volatile unsigned service_a_runs;
static volatile unsigned service_b_runs;
static volatile bool service_c_waiting;
static volatile bool service_c_cancelled;
static volatile uint64_t service_b_elapsed;
static volatile bool failed;

static bool wait_for_state(fiber_service_t *service,
                           fiber_service_state_t state) {
    uint64_t deadline = timer_ms_gettime64() + TEST_TIMEOUT_MS;

    while(fiber_service_get_state(service) != state &&
          timer_ms_gettime64() < deadline)
        thd_pass();

    return fiber_service_get_state(service) == state;
}

static bool wait_for_run_and_wait(fiber_service_t *service,
                                  const volatile unsigned *runs,
                                  unsigned expected) {
    uint64_t deadline = timer_ms_gettime64() + TEST_TIMEOUT_MS;

    while((*runs < expected ||
           fiber_service_get_state(service) != FIBER_SERVICE_WAITING) &&
          timer_ms_gettime64() < deadline)
        thd_pass();

    return *runs >= expected &&
           fiber_service_get_state(service) == FIBER_SERVICE_WAITING;
}

static bool wait_for_c_wait(void) {
    uint64_t deadline = timer_ms_gettime64() + TEST_TIMEOUT_MS;

    while(!service_c_waiting && timer_ms_gettime64() < deadline)
        thd_pass();

    return service_c_waiting &&
           fiber_service_get_state(service_c) == FIBER_SERVICE_WAITING;
}

static void check_executor_identity(void) {
    if(thd_get_current() != executor_thread ||
       mmu_cxt_current != expected_mmu)
        failed = true;
}

static void service_a_entry(fiber_service_t *service, void *data) {
    uint32_t *sq;

    (void)data;
    check_executor_identity();
    service_tls = 0x51ce51ceu;

    sq = sq_lock(service_stacks[0]);
    errno = 0;
    if(!sq || fiber_service_wait(service, 0) == 0 || errno != EBUSY ||
       fiber_service_get_state(service) != FIBER_SERVICE_RUNNING)
        failed = true;
    sq_unlock();

    ++service_a_runs;
    if(fiber_service_wait(service, 0) < 0)
        return;
    check_executor_identity();
    if(service_tls != 0x51ce51ceu)
        failed = true;
    ++service_a_runs;
}

static void service_b_entry(fiber_service_t *service, void *data) {
    uint64_t started = timer_ms_gettime64();

    (void)data;
    check_executor_identity();
    if(service_tls != 0x51ce51ceu)
        failed = true;

    ++service_b_runs;
    if(fiber_service_wait(service, started + 50) < 0)
        return;
    service_b_elapsed = timer_ms_gettime64() - started;
    ++service_b_runs;
}

static void service_c_entry(fiber_service_t *service, void *data) {
    (void)data;
    check_executor_identity();
    if(service_tls != 0x51ce51ceu)
        failed = true;
    service_c_waiting = true;

    if(fiber_service_wait(service, 0) < 0 && errno == ECANCELED &&
       fiber_service_stop_requested(service))
        service_c_cancelled = true;
}

static void irq_wake_handler(uint32_t code, void *data) {
    fiber_service_t *service = data;

    (void)code;
    if(!irq_wakes && fiber_service_wake(service) == 0)
        ++irq_wakes;
}

int main(int argc, char **argv) {
    int vblank_handle;

    (void)argc;
    (void)argv;

    expected_mmu = mmu_cxt_current;
    executor = fiber_service_executor_create();
    if(!executor)
        goto startup_failed;

    service_a = fiber_service_add(executor, service_stacks[0],
                                  sizeof(service_stacks[0]),
                                  service_a_entry, NULL);
    service_b = fiber_service_add(executor, service_stacks[1],
                                  sizeof(service_stacks[1]),
                                  service_b_entry, NULL);
    service_c = fiber_service_add(executor, service_stacks[2],
                                  sizeof(service_stacks[2]),
                                  service_c_entry, NULL);
    if(!service_a || !service_b || !service_c ||
       fiber_service_executor_start(executor, NULL) < 0)
        goto startup_failed;

    executor_thread = fiber_service_executor_get_thread(executor);
    if(!executor_thread)
        failed = true;

    fiber_service_wake(service_a);
    if(!wait_for_run_and_wait(service_a, &service_a_runs, 1))
        failed = true;

    vblank_handle = vblank_handler_add(irq_wake_handler, service_a);
    if(vblank_handle < 0 ||
       !wait_for_state(service_a, FIBER_SERVICE_FINISHED) ||
       service_a_runs != 2 || irq_wakes != 1)
        failed = true;
    if(vblank_handle >= 0)
        vblank_handler_remove(vblank_handle);

    fiber_service_wake(service_b);
    if(!wait_for_state(service_b, FIBER_SERVICE_FINISHED) ||
       service_b_runs != 2 || service_b_elapsed < 40 ||
       service_b_elapsed > 500)
        failed = true;

    fiber_service_wake(service_c);
    if(!wait_for_c_wait())
        failed = true;

    if(fiber_service_executor_destroy(executor) < 0 ||
       !service_c_cancelled)
        failed = true;
    executor = NULL;

    if(failed) {
        printf("Fiber service probe failed: irq=%u b=%u/%llu shutdown=%u\n",
               irq_wakes, service_b_runs,
               (unsigned long long)service_b_elapsed,
               service_c_cancelled);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERSVC irq=%u deadline=%llu shutdown=%u\n",
           irq_wakes, (unsigned long long)service_b_elapsed,
           service_c_cancelled);
    return EXIT_SUCCESS;

startup_failed:
    printf("Unable to start fiber service probe: %s\n", strerror(errno));
    if(executor)
        fiber_service_executor_destroy(executor);
    return EXIT_FAILURE;
}
