/* KallistiOS ##version##

   kernel/thread/fiber_sync.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/fiber_sync.h>

#include <kos/fiber.h>
#include <kos/irq.h>

#include <sys/queue.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "fiber_internal.h"

#define KFIBER_EVENT_MAGIC 0x46455654u
#define KFIBER_MUTEX_MAGIC 0x464d5558u

typedef struct kfiber_waiter {
    kfiber_t *fiber;
    bool queued;
    TAILQ_ENTRY(kfiber_waiter) list;
} kfiber_waiter_t;

TAILQ_HEAD(kfiber_waiter_list, kfiber_waiter);

struct kfiber_event {
    uint32_t magic;
    void *runtime_cookie;
    bool signaled;
    struct kfiber_waiter_list waiters;
};

struct kfiber_mutex {
    uint32_t magic;
    void *runtime_cookie;
    kfiber_t *owner;
    struct kfiber_waiter_list waiters;
};

static bool event_valid(const kfiber_event_t *event) {
    if(!event || event->magic != KFIBER_EVENT_MAGIC) {
        errno = EINVAL;
        return false;
    }

    return true;
}

static bool mutex_valid(const kfiber_mutex_t *mutex) {
    if(!mutex || mutex->magic != KFIBER_MUTEX_MAGIC) {
        errno = EINVAL;
        return false;
    }

    return true;
}

typedef struct event_waiter {
    kfiber_waiter_t waiter;
    kfiber_event_t *event;
} event_waiter_t;

typedef struct mutex_waiter {
    kfiber_waiter_t waiter;
    kfiber_mutex_t *mutex;
} mutex_waiter_t;

static void event_waiter_cancel(void *data) {
    event_waiter_t *waiter = data;

    if(waiter->waiter.queued) {
        TAILQ_REMOVE(&waiter->event->waiters, &waiter->waiter, list);
        waiter->waiter.queued = false;
    }
}

static void mutex_waiter_cancel(void *data) {
    mutex_waiter_t *waiter = data;

    if(waiter->waiter.queued) {
        TAILQ_REMOVE(&waiter->mutex->waiters, &waiter->waiter, list);
        waiter->waiter.queued = false;
    }
}

kfiber_event_t *fiber_event_create(bool signaled) {
    kfiber_event_t *event;
    void *runtime_cookie;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime_cookie = _fiber_runtime_cookie();
    if(!runtime_cookie)
        return NULL;

    event = calloc(1, sizeof(*event));
    if(!event) {
        errno = ENOMEM;
        return NULL;
    }

    event->magic = KFIBER_EVENT_MAGIC;
    event->runtime_cookie = runtime_cookie;
    event->signaled = signaled;
    TAILQ_INIT(&event->waiters);
    return event;
}

int fiber_event_destroy(kfiber_event_t *event) {
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!event_valid(event))
        return -1;
    if(_fiber_runtime_cookie() != event->runtime_cookie) {
        errno = EXDEV;
        return -1;
    }

    old_irq = irq_disable();
    if(!TAILQ_EMPTY(&event->waiters)) {
        irq_restore(old_irq);
        errno = EBUSY;
        return -1;
    }

    event->magic = 0;
    irq_restore(old_irq);
    free(event);
    return 0;
}

int fiber_event_wait(kfiber_event_t *event) {
    event_waiter_t waiter;
    irq_mask_t old_irq;
    kfiber_t *current;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!event_valid(event))
        return -1;

    old_irq = irq_disable();
    if(event->signaled) {
        irq_restore(old_irq);
        return 0;
    }

    current = _fiber_wait_current_locked(event->runtime_cookie, old_irq);
    if(!current) {
        irq_restore(old_irq);
        return -1;
    }

    waiter.waiter.fiber = current;
    waiter.waiter.queued = true;
    waiter.event = event;
    TAILQ_INSERT_TAIL(&event->waiters, &waiter.waiter, list);
    return _fiber_park_current_locked(&waiter, event_waiter_cancel,
                                      &waiter, old_irq);
}

int fiber_event_set(kfiber_event_t *event) {
    irq_mask_t old_irq;
    kfiber_waiter_t *waiter;

    if(!event_valid(event))
        return -1;

    old_irq = irq_disable();
    event->signaled = true;
    while((waiter = TAILQ_FIRST(&event->waiters))) {
        event_waiter_t *event_waiter = (event_waiter_t *)waiter;

        TAILQ_REMOVE(&event->waiters, waiter, list);
        waiter->queued = false;
        (void)_fiber_wake_locked(waiter->fiber, event_waiter, 0);
    }
    irq_restore(old_irq);
    return 0;
}

int fiber_event_clear(kfiber_event_t *event) {
    irq_mask_t old_irq;

    if(!event_valid(event))
        return -1;

    old_irq = irq_disable();
    event->signaled = false;
    irq_restore(old_irq);
    return 0;
}

bool fiber_event_is_set(const kfiber_event_t *event) {
    irq_mask_t old_irq;
    bool signaled;

    if(!event_valid(event))
        return false;

    old_irq = irq_disable();
    signaled = event->signaled;
    irq_restore(old_irq);
    return signaled;
}

kfiber_mutex_t *fiber_mutex_create(void) {
    kfiber_mutex_t *mutex;
    void *runtime_cookie;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime_cookie = _fiber_runtime_cookie();
    if(!runtime_cookie)
        return NULL;

    mutex = calloc(1, sizeof(*mutex));
    if(!mutex) {
        errno = ENOMEM;
        return NULL;
    }

    mutex->magic = KFIBER_MUTEX_MAGIC;
    mutex->runtime_cookie = runtime_cookie;
    TAILQ_INIT(&mutex->waiters);
    return mutex;
}

int fiber_mutex_destroy(kfiber_mutex_t *mutex) {
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!mutex_valid(mutex))
        return -1;
    if(_fiber_runtime_cookie() != mutex->runtime_cookie) {
        errno = EXDEV;
        return -1;
    }

    old_irq = irq_disable();
    if(mutex->owner || !TAILQ_EMPTY(&mutex->waiters)) {
        irq_restore(old_irq);
        errno = EBUSY;
        return -1;
    }

    mutex->magic = 0;
    irq_restore(old_irq);
    free(mutex);
    return 0;
}

int fiber_mutex_lock(kfiber_mutex_t *mutex) {
    mutex_waiter_t waiter;
    irq_mask_t old_irq;
    kfiber_t *current;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!mutex_valid(mutex))
        return -1;
    if(_fiber_runtime_cookie() != mutex->runtime_cookie) {
        errno = EXDEV;
        return -1;
    }

    current = fiber_current();
    if(!current)
        return -1;

    old_irq = irq_disable();
    if(!mutex->owner) {
        mutex->owner = current;
        _fiber_sync_acquired(current);
        irq_restore(old_irq);
        return 0;
    }
    if(mutex->owner == current) {
        irq_restore(old_irq);
        errno = EDEADLK;
        return -1;
    }
    if(!_fiber_wait_current_locked(mutex->runtime_cookie, old_irq)) {
        irq_restore(old_irq);
        return -1;
    }

    waiter.waiter.fiber = current;
    waiter.waiter.queued = true;
    waiter.mutex = mutex;
    TAILQ_INSERT_TAIL(&mutex->waiters, &waiter.waiter, list);
    return _fiber_park_current_locked(&waiter, mutex_waiter_cancel,
                                      &waiter, old_irq);
}

int fiber_mutex_trylock(kfiber_mutex_t *mutex) {
    irq_mask_t old_irq;
    kfiber_t *current;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!mutex_valid(mutex))
        return -1;
    if(_fiber_runtime_cookie() != mutex->runtime_cookie) {
        errno = EXDEV;
        return -1;
    }

    current = fiber_current();
    if(!current)
        return -1;

    old_irq = irq_disable();
    if(mutex->owner) {
        bool recursive = mutex->owner == current;

        irq_restore(old_irq);
        errno = recursive ? EDEADLK : EBUSY;
        return -1;
    }

    mutex->owner = current;
    _fiber_sync_acquired(current);
    irq_restore(old_irq);
    return 0;
}

int fiber_mutex_unlock(kfiber_mutex_t *mutex) {
    irq_mask_t old_irq;
    kfiber_t *current;
    kfiber_waiter_t *waiter;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!mutex_valid(mutex))
        return -1;
    if(_fiber_runtime_cookie() != mutex->runtime_cookie) {
        errno = EXDEV;
        return -1;
    }

    current = fiber_current();
    if(!current)
        return -1;

    old_irq = irq_disable();
    if(mutex->owner != current) {
        irq_restore(old_irq);
        errno = EPERM;
        return -1;
    }

    _fiber_sync_released(current);
    waiter = TAILQ_FIRST(&mutex->waiters);
    if(!waiter) {
        mutex->owner = NULL;
        irq_restore(old_irq);
        return 0;
    }

    {
        mutex_waiter_t *mutex_waiter = (mutex_waiter_t *)waiter;

        TAILQ_REMOVE(&mutex->waiters, waiter, list);
        waiter->queued = false;
        mutex->owner = waiter->fiber;
        _fiber_sync_acquired(waiter->fiber);
        (void)_fiber_wake_locked(waiter->fiber, mutex_waiter, 0);
    }
    irq_restore(old_irq);
    return 0;
}
