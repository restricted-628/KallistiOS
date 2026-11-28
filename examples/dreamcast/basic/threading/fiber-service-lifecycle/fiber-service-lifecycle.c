/* KallistiOS ##version##

   Fiber Service Executor startup, shutdown, and mailbox regression.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber.h>
#include <kos/fiber_service.h>
#include <arch/fiber.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_TIMEOUT_MS 2000u

static alignas(32) uint8_t service_stack[8192], second_stack[8192];
static const fiber_service_message_t first_message = { 1, 42 };
static const fiber_service_message_t late_message = { 2, 43 };
static volatile bool startup_entered;
static volatile bool stop_waiting;
static volatile bool stop_returned;
static volatile bool startup_received;
static volatile bool stop_cancelled;
static bool force_early_dispatch;
static bool creation_excluded;
static unsigned carrier_creations;
static unsigned fail_fiber_creation;
static fiber_service_t *stop_after_retire;
static bool checking_retired_fiber;
static int rollback_stop_result;
static unsigned retired_cancel_calls;
static volatile bool rollback_woke;
static volatile bool rollback_received;

static bool wait_for(const volatile bool *flag) {
    uint64_t deadline = timer_ms_gettime64() + TEST_TIMEOUT_MS;

    while(!*flag && timer_ms_gettime64() < deadline)
        thd_sleep(1);

    return *flag;
}

kthread_t *__real_thd_create_ex(const kthread_attr_t *attr,
                              void *(*entry)(void *), void *param);

kthread_t *__wrap_thd_create_ex(const kthread_attr_t *attr,
                              void *(*entry)(void *), void *param) {
    kthread_t *thread = __real_thd_create_ex(attr, entry, param);
    ++carrier_creations;

    if(thread && force_early_dispatch) {
        irq_mask_t previous = irq_disable();

        creation_excluded = !arch_fiber_irq_state_switchable(previous);
        irq_restore(previous);

        /* Give the real carrier a turn before its handle returns to start().
           This is possible only when the caller leaves IRQs enabled here. */
        if(!creation_excluded)
            (void)wait_for(&startup_entered);
    }

    return thread;
}

kfiber_t *__real_fiber_create(void *stack, size_t size,
                             kfiber_entry_t entry, void *arg);
int __real_fiber_destroy(kfiber_t *fiber);
int __real__fiber_cancel_wait(kfiber_t *fiber, int result);

kfiber_t *__wrap_fiber_create(void *stack, size_t size,
                             kfiber_entry_t entry, void *arg) {
    if(fail_fiber_creation && --fail_fiber_creation == 0) {
        errno = ENOMEM;
        return NULL;
    }
    return __real_fiber_create(stack, size, entry, arg);
}

int __wrap_fiber_destroy(kfiber_t *fiber) {
    int result = __real_fiber_destroy(fiber);

    if(result == 0 && stop_after_retire) {
        fiber_service_t *service = stop_after_retire;
        stop_after_retire = NULL;

        /* Model stop arriving after the child is freed, before the startup
           cleanup caller continues. Detect a stale reference without using it. */
        checking_retired_fiber = true;
        rollback_stop_result = fiber_service_request_stop(service);
        checking_retired_fiber = false;
        errno = ERANGE;
    }
    return result;
}

int __wrap__fiber_cancel_wait(kfiber_t *fiber, int result) {
    if(checking_retired_fiber) {
        ++retired_cancel_calls;
        return 0;
    }
    return __real__fiber_cancel_wait(fiber, result);
}

static fiber_service_executor_t *setup_service(fiber_service_entry_t entry,
                                               fiber_service_t **service) {
    fiber_service_executor_t *executor = fiber_service_executor_create();

    if(!executor)
        return NULL;

    *service = fiber_service_add(executor, service_stack, sizeof(service_stack),
                                 entry, NULL);
    if(!*service || fiber_service_queue_configure(*service, 2) < 0) {
        fiber_service_executor_destroy(executor);
        return NULL;
    }

    return executor;
}

static void startup_entry(fiber_service_t *service, void *data) {
    fiber_service_message_t message;
    (void)data;

    startup_received = fiber_service_receive(service, &message, 0) == 0 &&
                       message.tag == first_message.tag &&
                       message.value == first_message.value;
    startup_entered = true;
}

static bool test_first_dispatch(void) {
    fiber_service_t *service;
    fiber_service_executor_t *executor = setup_service(startup_entry, &service);
    bool ok;

    if(!executor)
        return false;

    ok = fiber_service_post(service, &first_message) == 0;
    force_early_dispatch = true;
    ok = ok && fiber_service_executor_start(executor, NULL) == 0;
    force_early_dispatch = false;
    ok = ok && wait_for(&startup_entered) && startup_received && creation_excluded;
    return fiber_service_executor_destroy(executor) == 0 && ok;
}

static void waiting_entry(fiber_service_t *service, void *data) {
    fiber_service_message_t message;
    (void)data;

    stop_waiting = true;
    stop_cancelled = fiber_service_receive(service, &message, 0) < 0 &&
                     errno == ECANCELED;
    stop_returned = true;
}

static bool test_post_after_stop(void) {
    fiber_service_t *service;
    fiber_service_executor_t *executor = setup_service(waiting_entry, &service);
    fiber_service_queue_info_t info;
    bool ok;

    if(!executor)
        return false;

    ok = fiber_service_wake(service) == 0 &&
         fiber_service_executor_start(executor, NULL) == 0 &&
         wait_for(&stop_waiting);

    if(ok) {
        /* Hold scheduling off so post sees a live service, before it can
           return and cause the terminal-state check to reject the post. */
        irq_disable_scoped();

        ok = fiber_service_post(service, &first_message) == 0 &&
             fiber_service_request_stop(service) == 0 &&
             fiber_service_post(service, &late_message) == -1 &&
             errno == ECANCELED &&
             fiber_service_get_queue_info(service, &info) == 0 &&
             info.queued == 1 && info.posted == 1 &&
             info.received == 0 && info.rejected == 0;
    }

    if(stop_waiting) {
        bool returned = wait_for(&stop_returned);
        ok = ok && returned && stop_cancelled;
    }

    return fiber_service_executor_destroy(executor) == 0 && ok;
}

static bool test_detached_attributes(void) {
    const kthread_attr_t detached = { .create_detached = true };
    fiber_service_t *service;
    fiber_service_executor_t *executor = setup_service(startup_entry, &service);
    unsigned before = carrier_creations;
    bool ok;

    if(!executor)
        return false;

    startup_entered = false;
    startup_received = false;
    ok = fiber_service_executor_start(executor, &detached) == -1 &&
         errno == EINVAL &&
         carrier_creations == before &&
         fiber_service_executor_get_thread(executor) == NULL &&
         fiber_service_get_state(service) == FIBER_SERVICE_CREATED;

    /* The rejection must leave the same executor ready for a normal start. */
    ok = ok && fiber_service_post(service, &first_message) == 0 &&
         fiber_service_executor_start(executor, NULL) == 0 &&
         wait_for(&startup_entered) && startup_received;
    return fiber_service_executor_destroy(executor) == 0 && ok;
}

static void rollback_wake_entry(fiber_service_t *service, void *data) {
    (void)service;
    (void)data;
    rollback_woke = true;
}

static void rollback_mailbox_entry(fiber_service_t *service, void *data) {
    fiber_service_message_t message;
    (void)data;

    rollback_received = fiber_service_receive(service, &message, 0) == 0 &&
                        message.tag == first_message.tag &&
                        message.value == first_message.value;
}

static bool test_startup_rollback(bool stop_during_cleanup) {
    fiber_service_executor_t *executor = fiber_service_executor_create();
    fiber_service_t *first, *second;
    bool ok;

    if(!executor)
        return false;

    first = fiber_service_add(executor, service_stack, sizeof(service_stack),
                              rollback_wake_entry, NULL);
    second = fiber_service_add(executor, second_stack, sizeof(second_stack),
                               rollback_mailbox_entry, NULL);
    if(!first || !second || fiber_service_queue_configure(second, 1) < 0 ||
       fiber_service_wake(first) < 0 ||
       fiber_service_post(second, &first_message) < 0) {
        fiber_service_executor_destroy(executor);
        return false;
    }

    rollback_woke = false;
    rollback_received = false;
    rollback_stop_result = -1;
    retired_cancel_calls = 0;
    stop_after_retire = stop_during_cleanup ? first : NULL;
    fail_fiber_creation = 2;
    int result = fiber_service_executor_start(executor, NULL);
    int failure = errno;
    bool injected = fail_fiber_creation == 0 && stop_after_retire == NULL;
    stop_after_retire = NULL;
    fail_fiber_creation = 0;

    ok = result == -1 && failure == ENOMEM && injected &&
         fiber_service_executor_get_thread(executor) == NULL &&
         fiber_service_get_state(first) == FIBER_SERVICE_CREATED &&
         fiber_service_get_state(second) == FIBER_SERVICE_CREATED &&
         !rollback_woke && !rollback_received;

    if(stop_during_cleanup)
        ok = ok && rollback_stop_result == 0 && retired_cancel_calls == 0 &&
             fiber_service_stop_requested(first);

    if(result == -1 && fiber_service_executor_start(executor, NULL) == 0) {
        bool received = wait_for(&rollback_received);
        bool woke;

        if(stop_during_cleanup) {
            /* The dispatcher visits the stopped service before the mailbox
               service. It should finish without calling the stopped entry. */
            woke = !rollback_woke &&
                   fiber_service_get_state(first) == FIBER_SERVICE_FINISHED;
        }
        else {
            woke = wait_for(&rollback_woke);
        }
        ok = ok && received && woke;
    }
    else {
        ok = false;
    }

    return fiber_service_executor_destroy(executor) == 0 && ok;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    bool dispatch = test_first_dispatch();
    bool stop = test_post_after_stop();
    bool retry = test_startup_rollback(false);
    bool retire = test_startup_rollback(true);
    bool detached = test_detached_attributes();

    printf("KOSFIBERLIFECYCLE dispatch=%u stop=%u detached=%u retry=%u retire=%u\n",
           (unsigned)dispatch, (unsigned)stop, (unsigned)detached,
           (unsigned)retry, (unsigned)retire);
    return dispatch && stop && detached && retry && retire ?
           EXIT_SUCCESS : EXIT_FAILURE;
}
