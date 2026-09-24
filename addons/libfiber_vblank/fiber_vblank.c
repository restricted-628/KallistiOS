/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos/fiber_vblank.h>
#include <kos/fiber.h>
#include <kos/irq.h>
#include <dc/vblank.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>

struct entry {
    fiber_vblank_callback_t callback;
    void *data;
    uint8_t priority;
};

struct fiber_vblank {
    size_t count, capacity;
    int handle;
    bool sealed, active;
    uint32_t pending;
    fiber_vblank_wake_t wake;
    void *wake_data;
    struct entry entries[];
};

static int check(fiber_vblank_t *dispatcher) {
    if(irq_inside_int()) { errno = EPERM; return -1; }
    if(!dispatcher) { errno = EINVAL; return -1; }
    return 0;
}

static void notify(uint32_t code, void *data) {
    fiber_vblank_t *dispatcher = data;
    bool wake = dispatcher->pending == 0;
    (void)code;
    if(!dispatcher->sealed) return;
    if(dispatcher->pending != UINT32_MAX) ++dispatcher->pending;
    if(wake) dispatcher->wake(dispatcher->wake_data);
}

fiber_vblank_t *fiber_vblank_create(size_t capacity) {
    fiber_vblank_t *dispatcher;
    if(irq_inside_int()) { errno = EPERM; return NULL; }
    if(!capacity || capacity > (SIZE_MAX - sizeof(*dispatcher)) /
                               sizeof(struct entry)) {
        errno = EINVAL;
        return NULL;
    }
    dispatcher = calloc(1, sizeof(*dispatcher) + capacity * sizeof(struct entry));
    if(!dispatcher) return NULL;
    dispatcher->capacity = capacity;
    dispatcher->handle = -1;
    return dispatcher;
}

int fiber_vblank_add(fiber_vblank_t *dispatcher, uint8_t priority,
                     fiber_vblank_callback_t callback, void *data) {
    size_t position;
    if(check(dispatcher) < 0) return -1;
    if(!callback) { errno = EINVAL; return -1; }
    if(dispatcher->sealed) { errno = EBUSY; return -1; }
    if(dispatcher->count == dispatcher->capacity) {
        errno = ENOSPC;
        return -1;
    }
    position = dispatcher->count++;
    while(position && dispatcher->entries[position - 1].priority >= priority) {
        dispatcher->entries[position] = dispatcher->entries[position - 1];
        --position;
    }
    dispatcher->entries[position] = (struct entry){ callback, data, priority };
    return 0;
}

int fiber_vblank_start(fiber_vblank_t *dispatcher, fiber_vblank_wake_t wake,
                       void *data) {
    if(check(dispatcher) < 0) return -1;
    if(!wake || !dispatcher->count) { errno = EINVAL; return -1; }
    if(dispatcher->sealed) { errno = EALREADY; return -1; }
    dispatcher->wake = wake;
    dispatcher->wake_data = data;
    /* Ignore IRQs during registration. Publish the handle before enabling
       notifications, so even an immediately scheduled consumer can drain. */
    dispatcher->handle = vblank_handler_add_prio(notify, dispatcher,
                                                VBLANK_PRIORITY_DEFAULT);
    if(dispatcher->handle < 0) return -1;
    irq_mask_t old = irq_disable();
    dispatcher->sealed = true;
    irq_restore(old);
    return 0;
}

int fiber_vblank_dispatch(fiber_vblank_t *dispatcher) {
    uint32_t frames;
    irq_mask_t old;
    if(check(dispatcher) < 0) return -1;
    if(!fiber_current() || fiber_current() == fiber_main()) {
        errno = EPERM;
        return -1;
    }
    old = irq_disable();
    /* Dreamcast SR.BL and SR.IMASK: callbacks must run with IRQs enabled. */
    if(old & UINT32_C(0x100000f0)) {
        irq_restore(old);
        errno = EPERM;
        return -1;
    }
    if(dispatcher->active) {
        irq_restore(old);
        errno = EBUSY;
        return -1;
    }
    frames = dispatcher->pending;
    if(dispatcher->handle < 0 || !frames) {
        irq_restore(old);
        return 0;
    }
    dispatcher->pending = 0;
    dispatcher->active = true;
    irq_restore(old);
    for(size_t i = 0; i < dispatcher->count; ++i) {
        if(dispatcher->handle < 0) break;
        dispatcher->entries[i].callback(frames, dispatcher->entries[i].data);
    }
    old = irq_disable();
    dispatcher->active = false;
    irq_restore(old);
    return 1;
}

int fiber_vblank_stop(fiber_vblank_t *dispatcher) {
    if(check(dispatcher) < 0) return -1;
    if(dispatcher->handle >= 0) {
        if(vblank_handler_remove(dispatcher->handle) < 0) return -1;
        /* A preempted consumer may finish some callbacks before this point;
           stop guarantees cancellation only by the time it returns. */
        dispatcher->handle = -1;
        dispatcher->pending = 0;
    }
    return 0;
}

int fiber_vblank_destroy(fiber_vblank_t *dispatcher) {
    if(check(dispatcher) < 0) return -1;
    if(dispatcher->handle >= 0 || dispatcher->active) {
        errno = EBUSY;
        return -1;
    }
    free(dispatcher);
    return 0;
}
