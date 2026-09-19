/* KallistiOS ##version##

   kernel/thread/fiber_service.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/fiber_service.h>

#include <arch/stack.h>

#include <kos/fiber.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/timer.h>

#include <sys/queue.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fiber_internal.h"

typedef enum executor_state {
    EXECUTOR_CREATED,
    EXECUTOR_STARTING,
    EXECUTOR_RUNNING,
    EXECUTOR_STOPPING
} executor_state_t;

struct fiber_service {
    fiber_service_executor_t *executor;
    kfiber_t *fiber;
    void *stack;
    size_t stack_size;
    fiber_service_entry_t entry;
    void *data;
    volatile fiber_service_state_t state;
    uint64_t deadline_ms;
    volatile bool wake_pending;
    volatile bool stop_requested;
    fiber_service_message_t *messages;
    size_t message_capacity;
    size_t message_head;
    size_t message_count;
    size_t message_high_watermark;
    uint64_t messages_posted;
    uint64_t messages_received;
    uint64_t messages_rejected;
    TAILQ_ENTRY(fiber_service) list;
};

TAILQ_HEAD(fiber_service_list, fiber_service);

struct fiber_service_executor {
    struct fiber_service_list services;
    kthread_t *thread;
    kfiber_t *main_fiber;
    semaphore_t work;
    semaphore_t started;
    volatile executor_state_t state;
    volatile bool stopping;
    volatile int start_result;
    volatile int start_error;
};

static bool stack_ranges_overlap(uintptr_t a, size_t a_size,
                                 uintptr_t b, size_t b_size) {
    return a < b + b_size && b < a + a_size;
}

static bool service_is_current(const fiber_service_t *service) {
    return service && service->executor &&
           thd_get_current() == service->executor->thread &&
           fiber_current() == service->fiber;
}

static void service_wait_observer(kfiber_t *fiber, bool waiting, void *data) {
    fiber_service_t *service = data;

    (void)fiber;
    if(service->state == FIBER_SERVICE_FINISHED ||
       service->state == FIBER_SERVICE_FAILED ||
       service->state == FIBER_SERVICE_STOPPED)
        return;

    service->state = waiting ? FIBER_SERVICE_WAITING : FIBER_SERVICE_READY;
    if(!waiting)
        sem_signal(&service->executor->work);
}

static void service_entry(void *data) {
    fiber_service_t *service = data;

    if(!service->stop_requested)
        service->entry(service, service->data);

    irq_disable_scoped();
    service->state = FIBER_SERVICE_FINISHED;
    service->deadline_ms = 0;
}

static int create_service_fibers(fiber_service_executor_t *executor) {
    fiber_service_t *service;

    executor->main_fiber = fiber_attach();
    if(!executor->main_fiber)
        return -1;

    TAILQ_FOREACH(service, &executor->services, list) {
        service->fiber = fiber_create(service->stack, service->stack_size,
                                      service_entry, service);
        if(!service->fiber)
            goto fail;
        if(_fiber_set_wait_observer(service->fiber, service_wait_observer,
                                    service) < 0)
            goto fail;

        if(service->wake_pending) {
            /* A pre-start wake admits the service for its first dispatch. */
            service->wake_pending = false;
            service->state = FIBER_SERVICE_READY;
        }
        else {
            service->state = service->message_count ?
                FIBER_SERVICE_READY : FIBER_SERVICE_WAITING;
        }
    }

    return 0;

fail:
    TAILQ_FOREACH(service, &executor->services, list) {
        if(service->fiber) {
            (void)_fiber_cancel_wait(service->fiber, ECANCELED);
            (void)_fiber_set_wait_observer(service->fiber, NULL, NULL);
            fiber_destroy(service->fiber);
            service->fiber = NULL;
        }
        service->state = FIBER_SERVICE_CREATED;
    }

    return -1;
}

static void destroy_service_fibers(fiber_service_executor_t *executor) {
    fiber_service_t *service;

    TAILQ_FOREACH(service, &executor->services, list) {
        if(service->fiber) {
            fiber_destroy(service->fiber);
            service->fiber = NULL;
        }

        if(service->state != FIBER_SERVICE_FINISHED &&
           service->state != FIBER_SERVICE_FAILED)
            service->state = FIBER_SERVICE_STOPPED;
    }
}

static bool all_services_terminal(
    const fiber_service_executor_t *executor) {
    const fiber_service_t *service;

    TAILQ_FOREACH(service, &executor->services, list) {
        if(service->state != FIBER_SERVICE_FINISHED &&
           service->state != FIBER_SERVICE_FAILED)
            return false;
    }

    return true;
}

static bool service_make_ready(fiber_service_t *service, uint64_t now,
                               bool stopping) {
    bool ready = false;
    kfiber_state_t fiber_state;

    irq_disable_scoped();

    if(service->state == FIBER_SERVICE_FINISHED ||
       service->state == FIBER_SERVICE_FAILED ||
       service->state == FIBER_SERVICE_STOPPED)
        return false;

    fiber_state = fiber_get_state(service->fiber);
    if(stopping) {
        (void)_fiber_cancel_wait(service->fiber, ECANCELED);
        service->stop_requested = true;
        service->wake_pending = false;
        service->deadline_ms = 0;
        service->state = FIBER_SERVICE_READY;
    }
    else if(service->state == FIBER_SERVICE_WAITING &&
            (service->wake_pending || service->message_count) &&
            fiber_state == KFIBER_STATE_READY) {
        /* A service-level wake is pending data, not completion of a mutex or
           event wait. A synchronization wake changes service state to READY,
           so only a genuine service wait reaches this consumption path. */
        service->wake_pending = false;
        service->deadline_ms = 0;
        service->state = FIBER_SERVICE_READY;
    }
    else if(service->state == FIBER_SERVICE_WAITING &&
            fiber_state == KFIBER_STATE_READY && service->deadline_ms &&
            service->deadline_ms <= now) {
        service->deadline_ms = 0;
        service->state = FIBER_SERVICE_READY;
    }

    if(service->state == FIBER_SERVICE_READY &&
       fiber_get_state(service->fiber) == KFIBER_STATE_READY) {
        service->state = FIBER_SERVICE_RUNNING;
        ready = true;
    }

    return ready;
}

static uint64_t next_service_deadline(
    const fiber_service_executor_t *executor) {
    const fiber_service_t *service;
    uint64_t deadline = 0;

    irq_disable_scoped();

    TAILQ_FOREACH(service, &executor->services, list) {
        if(service->state == FIBER_SERVICE_WAITING && service->deadline_ms &&
           (!deadline || service->deadline_ms < deadline))
            deadline = service->deadline_ms;
    }

    return deadline;
}

static unsigned deadline_timeout(uint64_t deadline, uint64_t now) {
    uint64_t remaining;

    if(!deadline)
        return 0;
    if(deadline <= now)
        return 1;

    remaining = deadline - now;
    return remaining > UINT_MAX ? UINT_MAX : (unsigned)remaining;
}

static void *service_executor_thread(void *data) {
    fiber_service_executor_t *executor = data;
    fiber_service_t *service;
    uint64_t now;
    uint64_t deadline;
    bool ran_service;

    if(create_service_fibers(executor) < 0) {
        executor->start_result = -1;
        executor->start_error = errno;
        sem_signal(&executor->started);
        return NULL;
    }

    executor->start_result = 0;
    executor->state = EXECUTOR_RUNNING;
    sem_signal(&executor->started);

    for(;;) {
        now = timer_ms_gettime64();
        ran_service = false;

        TAILQ_FOREACH(service, &executor->services, list) {
            if(!service_make_ready(service, now, executor->stopping))
                continue;

            ran_service = true;
            if(fiber_switch(service->fiber) < 0) {
                irq_disable_scoped();
                service->state = FIBER_SERVICE_FAILED;
            }
        }

        if(executor->stopping && all_services_terminal(executor))
            break;

        if(ran_service) {
            /* A perpetually ready service must not starve other KOS threads. */
            thd_pass();
            continue;
        }

        now = timer_ms_gettime64();
        deadline = next_service_deadline(executor);
        (void)sem_wait_timed(&executor->work,
                             deadline_timeout(deadline, now));
    }

    destroy_service_fibers(executor);
    return NULL;
}

fiber_service_executor_t *fiber_service_executor_create(void) {
    fiber_service_executor_t *executor = calloc(1, sizeof(*executor));

    if(!executor) {
        errno = ENOMEM;
        return NULL;
    }

    TAILQ_INIT(&executor->services);
    sem_init(&executor->work, 0);
    sem_init(&executor->started, 0);
    executor->state = EXECUTOR_CREATED;
    return executor;
}

fiber_service_t *fiber_service_add(fiber_service_executor_t *executor,
                                   void *stack, size_t stack_size,
                                   fiber_service_entry_t entry, void *data) {
    fiber_service_t *service;
    fiber_service_t *other;
    uintptr_t stack_base = (uintptr_t)stack;

    if(!executor) {
        errno = EINVAL;
        return NULL;
    }
    if(executor->state != EXECUTOR_CREATED) {
        errno = EBUSY;
        return NULL;
    }

    if(!stack || !entry || stack_size < KOS_FIBER_STACK_MIN ||
       (stack_base & (THD_STACK_ALIGNMENT - 1)) ||
       (stack_size & (THD_STACK_ALIGNMENT - 1)) ||
       stack_size > UINTPTR_MAX - stack_base) {
        errno = EINVAL;
        return NULL;
    }

    TAILQ_FOREACH(other, &executor->services, list) {
        if(stack_ranges_overlap(stack_base, stack_size,
                                (uintptr_t)other->stack,
                                other->stack_size)) {
            errno = EINVAL;
            return NULL;
        }
    }

    service = calloc(1, sizeof(*service));
    if(!service) {
        errno = ENOMEM;
        return NULL;
    }

    service->executor = executor;
    service->stack = stack;
    service->stack_size = stack_size;
    service->entry = entry;
    service->data = data;
    service->state = FIBER_SERVICE_CREATED;
    TAILQ_INSERT_TAIL(&executor->services, service, list);
    return service;
}

int fiber_service_queue_configure(fiber_service_t *service, size_t capacity) {
    fiber_service_message_t *messages;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!service || !service->executor || !capacity ||
       capacity > SIZE_MAX / sizeof(*messages)) {
        errno = EINVAL;
        return -1;
    }
    if(service->executor->state != EXECUTOR_CREATED || service->messages) {
        errno = EBUSY;
        return -1;
    }

    messages = calloc(capacity, sizeof(*messages));
    if(!messages) {
        errno = ENOMEM;
        return -1;
    }

    service->messages = messages;
    service->message_capacity = capacity;
    return 0;
}

int fiber_service_executor_start(fiber_service_executor_t *executor,
                                 const kthread_attr_t *attr) {
    kthread_attr_t default_attr = {
        .label = "[fiber-services]",
    };

    if(!executor || executor->state != EXECUTOR_CREATED ||
       TAILQ_EMPTY(&executor->services)) {
        errno = EINVAL;
        return -1;
    }

    executor->state = EXECUTOR_STARTING;
    executor->start_result = -1;
    executor->start_error = 0;
    executor->thread = thd_create_ex(attr ? attr : &default_attr,
                                     service_executor_thread, executor);
    if(!executor->thread) {
        executor->state = EXECUTOR_CREATED;
        return -1;
    }

    sem_wait(&executor->started);
    if(executor->start_result < 0) {
        int saved_errno = executor->start_error;

        thd_join(executor->thread, NULL);
        executor->thread = NULL;
        executor->state = EXECUTOR_CREATED;
        errno = saved_errno;
        return -1;
    }

    return 0;
}

int fiber_service_executor_destroy(fiber_service_executor_t *executor) {
    fiber_service_t *service;

    if(!executor) {
        errno = EINVAL;
        return -1;
    }

    if(executor->thread == thd_get_current()) {
        errno = EDEADLK;
        return -1;
    }

    if(executor->thread) {
        {
            irq_disable_scoped();
            executor->state = EXECUTOR_STOPPING;
            executor->stopping = true;
            TAILQ_FOREACH(service, &executor->services, list) {
                service->stop_requested = true;
                service->wake_pending = true;
            }
        }

        sem_signal(&executor->work);
        if(thd_join(executor->thread, NULL) < 0)
            return -1;
        executor->thread = NULL;
    }

    while((service = TAILQ_FIRST(&executor->services))) {
        TAILQ_REMOVE(&executor->services, service, list);
        free(service->messages);
        free(service);
    }

    sem_destroy(&executor->started);
    sem_destroy(&executor->work);
    free(executor);
    return 0;
}

int fiber_service_wake(fiber_service_t *service) {
    bool signal = false;

    if(!service || !service->executor) {
        errno = EINVAL;
        return -1;
    }

    {
        irq_disable_scoped();

        if(service->state == FIBER_SERVICE_FINISHED ||
           service->state == FIBER_SERVICE_FAILED ||
           service->state == FIBER_SERVICE_STOPPED ||
           service->executor->stopping) {
            errno = ECANCELED;
            return -1;
        }

        if(!service->wake_pending) {
            service->wake_pending = true;
            signal = true;
        }
    }

    if(signal)
        sem_signal(&service->executor->work);
    return 0;
}

int fiber_service_request_stop(fiber_service_t *service) {
    bool signal = false;
    bool resumed = false;

    if(!service || !service->executor) {
        errno = EINVAL;
        return -1;
    }

    {
        irq_disable_scoped();

        if(service->state == FIBER_SERVICE_FINISHED ||
           service->state == FIBER_SERVICE_FAILED ||
           service->state == FIBER_SERVICE_STOPPED) {
            errno = ECANCELED;
            return -1;
        }

        service->stop_requested = true;
        if(!service->wake_pending) {
            service->wake_pending = true;
            signal = true;
        }
    }

    if(service->fiber)
        resumed = _fiber_cancel_wait(service->fiber, ECANCELED) > 0;

    if(signal && !resumed)
        sem_signal(&service->executor->work);
    return 0;
}

int fiber_service_post(fiber_service_t *service,
                       const fiber_service_message_t *message) {
    irq_mask_t old_irq;
    size_t tail;
    bool signal;

    if(!service || !service->executor || !message || !service->messages) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();
    if(service->state == FIBER_SERVICE_FINISHED ||
       service->state == FIBER_SERVICE_FAILED ||
       service->state == FIBER_SERVICE_STOPPED ||
       service->executor->stopping) {
        irq_restore(old_irq);
        errno = ECANCELED;
        return -1;
    }
    if(service->message_count == service->message_capacity) {
        ++service->messages_rejected;
        irq_restore(old_irq);
        errno = EAGAIN;
        return -1;
    }

    signal = service->message_count == 0;
    tail = service->message_head + service->message_count;
    if(tail >= service->message_capacity)
        tail -= service->message_capacity;
    service->messages[tail] = *message;
    ++service->message_count;
    ++service->messages_posted;
    if(service->message_count > service->message_high_watermark)
        service->message_high_watermark = service->message_count;
    irq_restore(old_irq);

    if(signal)
        sem_signal(&service->executor->work);
    return 0;
}

int fiber_service_receive(fiber_service_t *service,
                          fiber_service_message_t *message,
                          uint64_t deadline_ms) {
    irq_mask_t old_irq;

    if(!service_is_current(service)) {
        errno = EPERM;
        return -1;
    }
    if(!message || !service->messages) {
        errno = EINVAL;
        return -1;
    }

    for(;;) {
        old_irq = irq_disable();
        if(service->stop_requested || service->executor->stopping) {
            irq_restore(old_irq);
            errno = ECANCELED;
            return -1;
        }
        if(service->message_count) {
            *message = service->messages[service->message_head];
            if(++service->message_head == service->message_capacity)
                service->message_head = 0;
            --service->message_count;
            ++service->messages_received;
            irq_restore(old_irq);
            return 0;
        }
        irq_restore(old_irq);

        if(deadline_ms && timer_ms_gettime64() >= deadline_ms) {
            errno = ETIMEDOUT;
            return -1;
        }
        if(fiber_service_wait(service, deadline_ms) < 0)
            return -1;
    }
}

int fiber_service_get_queue_info(const fiber_service_t *service,
                                 fiber_service_queue_info_t *info) {
    irq_mask_t old_irq;

    if(!service || !info || !service->messages) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();
    info->capacity = service->message_capacity;
    info->queued = service->message_count;
    info->high_watermark = service->message_high_watermark;
    info->posted = service->messages_posted;
    info->received = service->messages_received;
    info->rejected = service->messages_rejected;
    irq_restore(old_irq);
    return 0;
}

int fiber_service_wait(fiber_service_t *service, uint64_t deadline_ms) {
    irq_mask_t old_irq;

    if(!service_is_current(service)) {
        errno = EPERM;
        return -1;
    }

    old_irq = irq_disable();
    if(service->stop_requested || service->executor->stopping) {
        irq_restore(old_irq);
        errno = ECANCELED;
        return -1;
    }

    if(service->wake_pending || service->message_count) {
        service->wake_pending = false;
        irq_restore(old_irq);
        return 0;
    }

    service->deadline_ms = deadline_ms;
    service->state = FIBER_SERVICE_WAITING;
    irq_restore(old_irq);

    if(fiber_switch(service->executor->main_fiber) < 0) {
        int saved_errno = errno;

        old_irq = irq_disable();
        service->deadline_ms = 0;
        service->state = FIBER_SERVICE_RUNNING;
        irq_restore(old_irq);
        errno = saved_errno;
        return -1;
    }

    if(service->stop_requested || service->executor->stopping) {
        errno = ECANCELED;
        return -1;
    }

    return 0;
}

int fiber_service_yield(fiber_service_t *service) {
    irq_mask_t old_irq;

    if(!service_is_current(service)) {
        errno = EPERM;
        return -1;
    }

    old_irq = irq_disable();
    if(service->stop_requested || service->executor->stopping) {
        irq_restore(old_irq);
        errno = ECANCELED;
        return -1;
    }

    service->state = FIBER_SERVICE_READY;
    irq_restore(old_irq);

    if(fiber_switch(service->executor->main_fiber) < 0) {
        int saved_errno = errno;

        old_irq = irq_disable();
        service->state = FIBER_SERVICE_RUNNING;
        irq_restore(old_irq);
        errno = saved_errno;
        return -1;
    }

    if(service->stop_requested || service->executor->stopping) {
        errno = ECANCELED;
        return -1;
    }

    return 0;
}

bool fiber_service_stop_requested(const fiber_service_t *service) {
    bool requested;

    if(!service)
        return true;

    irq_disable_scoped();
    requested = service->stop_requested || service->executor->stopping;
    return requested;
}

fiber_service_state_t fiber_service_get_state(const fiber_service_t *service) {
    fiber_service_state_t state;

    if(!service)
        return FIBER_SERVICE_FAILED;

    irq_disable_scoped();
    state = service->state;
    return state;
}

kthread_t *fiber_service_executor_get_thread(
    const fiber_service_executor_t *executor) {
    return executor ? executor->thread : NULL;
}
