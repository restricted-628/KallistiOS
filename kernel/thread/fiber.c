/* KallistiOS ##version##

   kernel/thread/fiber.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/fiber.h>

#include <arch/fiber.h>
#include <arch/stack.h>

#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/once.h>
#include <kos/thread.h>
#include <kos/tls.h>

#include "fiber_internal.h"

#include <sys/queue.h>

#include <errno.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct fiber_runtime fiber_runtime_t;

struct kfiber {
    arch_fiber_context_t context;
    fiber_runtime_t *runtime;
    void *stack;
    size_t stack_size;
    kfiber_entry_t entry;
    void *data;
    kfiber_state_t state;
    void *wait_key;
    kfiber_wait_cancel_t wait_cancel;
    void *wait_cancel_data;
    int wait_result;
    kfiber_wait_observer_t wait_observer;
    void *wait_observer_data;
    unsigned int owned_sync_count;
    TAILQ_ENTRY(kfiber) list;
};

TAILQ_HEAD(kfiber_list, kfiber);

struct fiber_runtime {
    kthread_t *owner;
    kfiber_t main;
    kfiber_t *current;
    struct kfiber_list fibers;
    kfiber_switch_cb_t switch_callback;
    void *switch_callback_data;
    bool switching;
};

static kthread_once_t fiber_key_once = KTHREAD_ONCE_INIT;
static _Atomic kthread_key_t fiber_key = -1;

static bool fiber_stack_bounds(const kthread_t *owner, uintptr_t sp,
                               uintptr_t *base_out, size_t *size_out);

static void fiber_runtime_destroy(void *data) {
    fiber_runtime_t *runtime = data;
    kfiber_t *fiber;

    while((fiber = TAILQ_FIRST(&runtime->fibers))) {
        (void)_fiber_cancel_wait(fiber, ECANCELED);
        if(fiber->owned_sync_count)
            dbglog(DBG_DEAD,
                   "fiber: thread exited while a fiber owned synchronization\n");
        TAILQ_REMOVE(&runtime->fibers, fiber, list);
        free(fiber);
    }

    if(runtime->main.owned_sync_count)
        dbglog(DBG_DEAD,
               "fiber: thread exited while its main fiber owned synchronization\n");

    free(runtime);
}

static void fiber_key_initialize(void) {
    kthread_key_t key;

    if(kthread_key_create(&key, fiber_runtime_destroy) == 0) {
        _thd_continuation_stack_resolver_set(fiber_stack_bounds);
        atomic_store(&fiber_key, key);
    }
}

static fiber_runtime_t *fiber_runtime_get(void) {
    kthread_key_t key = atomic_load(&fiber_key);

    /* Once a key exists, lookup stays lock-free. This matters for the bounded
       pre-switch callback, which runs with interrupts masked. */
    if(key < 0) {
        if(kthread_once(&fiber_key_once, fiber_key_initialize) < 0)
            return NULL;
        key = atomic_load(&fiber_key);
    }

    if(key < 0) {
        errno = ENOMEM;
        return NULL;
    }

    return kthread_getspecific(key);
}

static bool fiber_is_owned(const kfiber_t *fiber,
                           const fiber_runtime_t *runtime) {
    if(!fiber || !runtime) {
        errno = EINVAL;
        return false;
    }

    if(fiber->runtime != runtime || runtime->owner != thd_get_current()) {
        errno = EXDEV;
        return false;
    }

    return true;
}

static bool stack_ranges_overlap(uintptr_t a, size_t a_size,
                                 uintptr_t b, size_t b_size) {
    return a < b + b_size && b < a + a_size;
}

static bool fiber_stack_available(const fiber_runtime_t *runtime,
                                  uintptr_t base, size_t size) {
    const kfiber_t *fiber;

    if(stack_ranges_overlap(base, size, (uintptr_t)runtime->main.stack,
                            runtime->main.stack_size))
        return false;

    TAILQ_FOREACH(fiber, &runtime->fibers, list) {
        if(stack_ranges_overlap(base, size, (uintptr_t)fiber->stack,
                                fiber->stack_size))
            return false;
    }

    return true;
}

static void fiber_select(fiber_runtime_t *runtime, kfiber_t *from,
                         kfiber_t *to, kfiber_state_t from_state) {
    runtime->switching = true;

    if(runtime->switch_callback)
        runtime->switch_callback(from, to, runtime->switch_callback_data);

    from->state = from_state;
    to->state = KFIBER_STATE_RUNNING;
    runtime->current = to;
    runtime->switching = false;
}

static void fiber_entry_trampoline(void) __attribute__((noreturn));

static void fiber_entry_trampoline(void) {
    fiber_runtime_t *runtime = fiber_runtime_get();
    kfiber_t *fiber;
    kfiber_t *main_fiber;
    irq_mask_t old_irq;

    if(!runtime || !runtime->current)
        abort();

    fiber = runtime->current;
    main_fiber = &runtime->main;
    fiber->entry(fiber->data);

    if(!arch_fiber_cooperative_state_switchable() ||
       fiber->owned_sync_count) {
        dbglog(DBG_DEAD,
               "fiber: entry returned while cooperative state remained owned\n");
        abort();
    }

    old_irq = irq_disable();
    fiber_select(runtime, fiber, main_fiber, KFIBER_STATE_FINISHED);
    arch_fiber_context_switch(&fiber->context, &main_fiber->context);

    /* A finished fiber can never be selected again. */
    irq_restore(old_irq);
    abort();
}

kfiber_t *fiber_attach(void) {
    fiber_runtime_t *runtime;
    kthread_t *owner;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    if(runtime)
        return &runtime->main;
    if(atomic_load(&fiber_key) < 0)
        return NULL;

    owner = thd_get_current();
    if(!owner || !owner->stack || !owner->stack_size) {
        errno = EINVAL;
        return NULL;
    }

    runtime = calloc(1, sizeof(*runtime));
    if(!runtime) {
        errno = ENOMEM;
        return NULL;
    }

    runtime->owner = owner;
    runtime->main.runtime = runtime;
    runtime->main.stack = owner->stack;
    runtime->main.stack_size = owner->stack_size;
    runtime->main.state = KFIBER_STATE_RUNNING;
    runtime->current = &runtime->main;
    TAILQ_INIT(&runtime->fibers);

    if(kthread_setspecific(atomic_load(&fiber_key), runtime) < 0) {
        free(runtime);
        return NULL;
    }

    return &runtime->main;
}

kfiber_t *fiber_create(void *stack, size_t stack_size,
                       kfiber_entry_t entry, void *data) {
    fiber_runtime_t *runtime;
    kfiber_t *fiber;
    uintptr_t stack_base = (uintptr_t)stack;
    uintptr_t stack_top;
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    if(!runtime) {
        if(atomic_load(&fiber_key) >= 0)
            errno = EINVAL;
        return NULL;
    }

    if(!stack || !entry || stack_size < KOS_FIBER_STACK_MIN ||
       (stack_base & (THD_STACK_ALIGNMENT - 1)) ||
       (stack_size & (THD_STACK_ALIGNMENT - 1)) ||
       stack_size > UINTPTR_MAX - stack_base) {
        errno = EINVAL;
        return NULL;
    }

    if(runtime->switching) {
        errno = EBUSY;
        return NULL;
    }

    if(!fiber_stack_available(runtime, stack_base, stack_size)) {
        errno = EINVAL;
        return NULL;
    }

    fiber = calloc(1, sizeof(*fiber));
    if(!fiber) {
        errno = ENOMEM;
        return NULL;
    }

    stack_top = stack_base + stack_size;
    old_irq = irq_disable();
    arch_fiber_context_init(&fiber->context, stack_top,
                            (uintptr_t)fiber_entry_trampoline, old_irq);
    fiber->runtime = runtime;
    fiber->stack = stack;
    fiber->stack_size = stack_size;
    fiber->entry = entry;
    fiber->data = data;
    fiber->state = KFIBER_STATE_READY;
    TAILQ_INSERT_TAIL(&runtime->fibers, fiber, list);
    irq_restore(old_irq);

    return fiber;
}

int fiber_destroy(kfiber_t *fiber) {
    fiber_runtime_t *runtime;
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    runtime = fiber_runtime_get();
    if(!fiber_is_owned(fiber, runtime))
        return -1;

    if(runtime->switching || fiber == &runtime->main ||
       fiber == runtime->current || fiber->owned_sync_count) {
        errno = EBUSY;
        return -1;
    }

    if(fiber->state == KFIBER_STATE_WAITING)
        (void)_fiber_cancel_wait(fiber, ECANCELED);

    old_irq = irq_disable();
    TAILQ_REMOVE(&runtime->fibers, fiber, list);
    irq_restore(old_irq);
    free(fiber);
    return 0;
}

int fiber_switch(kfiber_t *to) {
    fiber_runtime_t *runtime;
    kfiber_t *from;
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    runtime = fiber_runtime_get();
    if(!fiber_is_owned(to, runtime))
        return -1;

    from = runtime->current;
    if(to == from)
        return 0;

    if(runtime->switching || to->state != KFIBER_STATE_READY) {
        errno = EBUSY;
        return -1;
    }

    old_irq = irq_disable();
    if(!arch_fiber_irq_state_switchable(old_irq) ||
       !arch_fiber_cooperative_state_switchable()) {
        irq_restore(old_irq);
        errno = EBUSY;
        return -1;
    }

    fiber_select(runtime, from, to, KFIBER_STATE_READY);
    arch_fiber_context_switch(&from->context, &to->context);
    irq_restore(old_irq);
    return 0;
}

kfiber_t *fiber_current(void) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    return runtime ? runtime->current : NULL;
}

kfiber_t *fiber_main(void) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    return runtime ? &runtime->main : NULL;
}

void *fiber_get_data(const kfiber_t *fiber) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    if(!fiber_is_owned(fiber, runtime))
        return NULL;

    return fiber->data;
}

kfiber_state_t fiber_get_state(const kfiber_t *fiber) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return KFIBER_STATE_INVALID;
    }

    runtime = fiber_runtime_get();
    if(!fiber_is_owned(fiber, runtime))
        return KFIBER_STATE_INVALID;

    return fiber->state;
}

int fiber_set_switch_callback(kfiber_switch_cb_t callback, void *data) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    runtime = fiber_runtime_get();
    if(!runtime) {
        if(atomic_load(&fiber_key) >= 0)
            errno = EINVAL;
        return -1;
    }

    if(runtime->switching) {
        errno = EBUSY;
        return -1;
    }

    runtime->switch_callback = callback;
    runtime->switch_callback_data = data;
    return 0;
}

void *_fiber_runtime_cookie(void) {
    fiber_runtime_t *runtime;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    runtime = fiber_runtime_get();
    if(!runtime)
        errno = EINVAL;
    return runtime;
}

kfiber_t *_fiber_wait_current_locked(void *runtime_cookie,
                                     irq_mask_t saved_irq) {
    fiber_runtime_t *runtime = fiber_runtime_get();
    kfiber_t *current;

    if(!runtime || runtime != runtime_cookie ||
       runtime->owner != thd_get_current()) {
        errno = EXDEV;
        return NULL;
    }

    current = runtime->current;
    if(current == &runtime->main) {
        errno = EDEADLK;
        return NULL;
    }

    if(runtime->switching || current->state != KFIBER_STATE_RUNNING ||
       !arch_fiber_irq_state_switchable(saved_irq) ||
       !arch_fiber_cooperative_state_switchable()) {
        errno = EBUSY;
        return NULL;
    }

    return current;
}

static bool fiber_stack_bounds(const kthread_t *owner, uintptr_t sp,
                               uintptr_t *base_out, size_t *size_out) {
    kthread_key_t key = atomic_load(&fiber_key);
    const kthread_tls_kv_t *entry;
    const fiber_runtime_t *runtime = NULL;
    const kfiber_t *fiber;
    uintptr_t base;

    /* This hook is reached from scheduler or unwinder paths, including with
       interrupts masked. It must never initialize TLS or allocate. */
    if(!owner || key < 0)
        return false;

    LIST_FOREACH(entry, &owner->tls_list, kv_list) {
        if(entry->key == key) {
            runtime = entry->data;
            break;
        }
    }

    if(!runtime || runtime->owner != owner || !runtime->current)
        return false;

    fiber = runtime->current;
    base = (uintptr_t)fiber->stack;
    if(!fiber->stack || !fiber->stack_size || sp < base ||
       sp - base > fiber->stack_size)
        return false;

    if(base_out)
        *base_out = base;
    if(size_out)
        *size_out = fiber->stack_size;
    return true;
}

int _fiber_park_current_locked(void *wait_key, kfiber_wait_cancel_t cancel,
                               void *cancel_data, irq_mask_t saved_irq) {
    fiber_runtime_t *runtime = fiber_runtime_get();
    kfiber_t *current;
    int result;

    if(!runtime || !wait_key || !cancel) {
        irq_restore(saved_irq);
        errno = EINVAL;
        return -1;
    }

    current = runtime->current;
    current->wait_key = wait_key;
    current->wait_cancel = cancel;
    current->wait_cancel_data = cancel_data;
    current->wait_result = 0;

    fiber_select(runtime, current, &runtime->main, KFIBER_STATE_WAITING);
    if(current->wait_observer)
        current->wait_observer(current, true, current->wait_observer_data);
    arch_fiber_context_switch(&current->context, &runtime->main.context);

    result = current->wait_result;
    irq_restore(saved_irq);
    if(result) {
        errno = result;
        return -1;
    }

    return 0;
}

bool _fiber_wake_locked(kfiber_t *fiber, void *wait_key, int result) {
    if(!fiber || fiber->state != KFIBER_STATE_WAITING ||
       fiber->wait_key != wait_key)
        return false;

    fiber->wait_key = NULL;
    fiber->wait_cancel = NULL;
    fiber->wait_cancel_data = NULL;
    fiber->wait_result = result;
    fiber->state = KFIBER_STATE_READY;
    if(fiber->wait_observer)
        fiber->wait_observer(fiber, false, fiber->wait_observer_data);
    return true;
}

int _fiber_cancel_wait(kfiber_t *fiber, int result) {
    irq_mask_t old_irq;
    void *wait_key;

    if(!fiber) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();
    if(fiber->state != KFIBER_STATE_WAITING) {
        irq_restore(old_irq);
        return 0;
    }

    wait_key = fiber->wait_key;
    fiber->wait_cancel(fiber->wait_cancel_data);
    (void)_fiber_wake_locked(fiber, wait_key, result);
    irq_restore(old_irq);
    return 1;
}

int _fiber_set_wait_observer(kfiber_t *fiber,
                             kfiber_wait_observer_t observer, void *data) {
    fiber_runtime_t *runtime = fiber_runtime_get();

    if(!fiber_is_owned(fiber, runtime))
        return -1;
    if(fiber->state == KFIBER_STATE_WAITING) {
        errno = EBUSY;
        return -1;
    }

    fiber->wait_observer = observer;
    fiber->wait_observer_data = data;
    return 0;
}

void _fiber_sync_acquired(kfiber_t *fiber) {
    ++fiber->owned_sync_count;
}

void _fiber_sync_released(kfiber_t *fiber) {
    if(!fiber->owned_sync_count) {
        dbglog(DBG_DEAD, "fiber: synchronization ownership underflow\n");
        abort();
    }

    --fiber->owned_sync_count;
}
