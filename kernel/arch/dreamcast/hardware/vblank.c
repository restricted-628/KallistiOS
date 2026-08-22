/* KallistiOS ##version##

   kernel/arch/dreamcast/hardware/vblank.c
   Copyright (C)2003 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/queue.h>

#include <kos/irq.h>
#include <dc/vblank.h>

/*
   Functions to multiplex the vblank IRQ out to N client routines.
   This module is necessary because a number of things need to hang
   off the vblank IRQ, and chaining is unreliable.
*/

/* Our list of handlers */
struct vblhnd {
    TAILQ_ENTRY(vblhnd) listent;
    int         id;
    uint8_t     priority;
    bool        removed;
    asic_evt_handler    handler;
    void *data;
};
static TAILQ_HEAD(vhlist, vblhnd) vblhnds;
static int vblid_high;
static unsigned int vblank_dispatch_depth;

/* A callback may remove itself or a later callback. Removed entries stay in
   the live list until a thread-context API call can detach them with IRQs
   disabled and free them after restoring IRQs. This protects traversal without
   calling the process allocator from the VBlank handler. */
static void vblank_reap_removed(void) {
    struct vhlist removed;
    struct vblhnd *handler, *next;
    irq_mask_t old;

    if(irq_inside_int())
        return;

    TAILQ_INIT(&removed);
    old = irq_disable();
    if(vblank_dispatch_depth) {
        irq_restore(old);
        return;
    }

    handler = TAILQ_FIRST(&vblhnds);
    while(handler) {
        next = TAILQ_NEXT(handler, listent);
        if(handler->removed) {
            TAILQ_REMOVE(&vblhnds, handler, listent);
            TAILQ_INSERT_TAIL(&removed, handler, listent);
        }
        handler = next;
    }

    irq_restore(old);

    while((handler = TAILQ_FIRST(&removed))) {
        TAILQ_REMOVE(&removed, handler, listent);
        free(handler);
    }
}

/* Our internal IRQ handler */
static void vblank_handler(uint32_t src, void *data) {
    struct vblhnd *handler;

    (void)data;

    ++vblank_dispatch_depth;
    TAILQ_FOREACH(handler, &vblhnds, listent) {
        if(!handler->removed)
            handler->handler(src, handler->data);
    }
    --vblank_dispatch_depth;
}

int vblank_handler_add_prio(asic_evt_handler hnd, void *data,
                            uint8_t priority) {
    struct vblhnd *handler, *position;
    int old;

    if(!hnd) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    vblank_reap_removed();

    handler = malloc(sizeof(*handler));

    if(!handler) {
        errno = ENOMEM;
        return -1;
    }

    /* Disable ints just in case */
    old = irq_disable();

    /* Find a new ID */
    if(vblid_high == INT_MAX) {
        irq_restore(old);
        free(handler);
        errno = EOVERFLOW;
        return -1;
    }
    handler->id = vblid_high;
    vblid_high++;

    /* Finish filling the struct */
    handler->priority = priority;
    handler->removed = false;
    handler->handler = hnd;
    handler->data = data;

    /* Preserve registration order among handlers at the same priority. */
    TAILQ_FOREACH(position, &vblhnds, listent) {
        if(!position->removed && priority < position->priority) {
            TAILQ_INSERT_BEFORE(position, handler, listent);
            break;
        }
    }
    if(!position)
        TAILQ_INSERT_TAIL(&vblhnds, handler, listent);

    /* Restore ints */
    irq_restore(old);

    return handler->id;
}

int vblank_handler_add(asic_evt_handler hnd, void *data) {
    return vblank_handler_add_prio(hnd, data, VBLANK_PRIORITY_DEFAULT);
}

int vblank_handler_remove(int handle) {
    struct vblhnd *handler;
    int old, rv;

    /* Disable ints just in case */
    old = irq_disable();

    /* Look for it */
    rv = -1;
    TAILQ_FOREACH(handler, &vblhnds, listent) {
        if(handler->id == handle && !handler->removed) {
            handler->removed = true;
            rv = 0;
            break;
        }
    }

    /* Restore ints */
    irq_restore(old);

    if(!irq_inside_int())
        vblank_reap_removed();

    if(rv < 0)
        errno = ENOENT;

    return rv;
}

int vblank_init(void) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    /* Setup our data structures */
    TAILQ_INIT(&vblhnds);
    vblid_high = 1;
    vblank_dispatch_depth = 0;

    /* Hook and enable the interrupt */
    asic_evt_set_handler(ASIC_EVT_PVR_VBLANK_BEGIN, vblank_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_VBLANK_BEGIN, ASIC_IRQ_DEFAULT);

    return 0;
}

int vblank_shutdown(void) {
    struct vblhnd * c, * n;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    /* Disable and unhook the interrupt */
    asic_evt_disable(ASIC_EVT_PVR_VBLANK_BEGIN, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_VBLANK_BEGIN);

    /* Free any allocated handlers */
    c = TAILQ_FIRST(&vblhnds);

    while(c != NULL) {
        n = TAILQ_NEXT(c, listent);
        free(c);
        c = n;
    }

    TAILQ_INIT(&vblhnds);
    vblank_dispatch_depth = 0;

    return 0;
}
