/* KallistiOS ##version##

   pvr_events.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#include <kos/irq.h>
#include <dc/pvr.h>

#include "pvr_internal.h"

typedef struct pvr_event_handler {
    TAILQ_ENTRY(pvr_event_handler) entry;
    int id;
    uint32_t mask;
    bool removed;
    pvr_event_callback_t callback;
    void *user_data;
} pvr_event_handler_t;

static TAILQ_HEAD(pvr_event_handler_list, pvr_event_handler) handlers;
static int next_handler_id;
static unsigned int dispatch_depth;

/* Callbacks may remove themselves or handlers later in the list. Physical
   removal waits for thread context so IRQ dispatch never traverses freed
   storage and never invokes the allocator. */
static void reap_removed_handlers(void) {
    struct pvr_event_handler_list removed;
    pvr_event_handler_t *handler, *next;
    irq_mask_t old_irq;

    if(irq_inside_int())
        return;

    TAILQ_INIT(&removed);
    old_irq = irq_disable();

    if(dispatch_depth) {
        irq_restore(old_irq);
        return;
    }

    handler = TAILQ_FIRST(&handlers);
    while(handler) {
        next = TAILQ_NEXT(handler, entry);
        if(handler->removed) {
            TAILQ_REMOVE(&handlers, handler, entry);
            TAILQ_INSERT_TAIL(&removed, handler, entry);
        }
        handler = next;
    }

    irq_restore(old_irq);

    while((handler = TAILQ_FIRST(&removed))) {
        TAILQ_REMOVE(&removed, handler, entry);
        free(handler);
    }
}

void pvr_event_init(void) {
    TAILQ_INIT(&handlers);
    next_handler_id = 0;
    dispatch_depth = 0;
}

void pvr_event_shutdown(void) {
    struct pvr_event_handler_list removed;
    pvr_event_handler_t *handler;
    irq_mask_t old_irq;

    TAILQ_INIT(&removed);
    old_irq = irq_disable();

    while((handler = TAILQ_FIRST(&handlers))) {
        TAILQ_REMOVE(&handlers, handler, entry);
        TAILQ_INSERT_TAIL(&removed, handler, entry);
    }

    irq_restore(old_irq);

    while((handler = TAILQ_FIRST(&removed))) {
        TAILQ_REMOVE(&removed, handler, entry);
        free(handler);
    }
}

void pvr_event_dispatch(pvr_event_t event, uint32_t detail) {
    pvr_event_handler_t *handler;

    ++dispatch_depth;
    TAILQ_FOREACH(handler, &handlers, entry) {
        if(!handler->removed && (handler->mask & event))
            handler->callback(event, detail, handler->user_data);
    }
    --dispatch_depth;
}

int pvr_event_handler_add(uint32_t event_mask,
                          pvr_event_callback_t callback, void *user_data) {
    pvr_event_handler_t *handler;
    irq_mask_t old_irq;

    if(!event_mask || (event_mask & ~PVR_EVENT_ALL) || !callback) {
        errno = EINVAL;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    reap_removed_handlers();
    handler = malloc(sizeof(*handler));
    if(!handler) {
        errno = ENOMEM;
        return -1;
    }

    old_irq = irq_disable();
    if(next_handler_id == INT_MAX) {
        irq_restore(old_irq);
        free(handler);
        errno = EOVERFLOW;
        return -1;
    }

    handler->id = next_handler_id++;
    handler->mask = event_mask;
    handler->removed = false;
    handler->callback = callback;
    handler->user_data = user_data;
    TAILQ_INSERT_TAIL(&handlers, handler, entry);

    irq_restore(old_irq);
    return handler->id;
}

int pvr_event_handler_remove(int handle) {
    pvr_event_handler_t *handler;
    irq_mask_t old_irq;
    int result = -1;

    old_irq = irq_disable();
    TAILQ_FOREACH(handler, &handlers, entry) {
        if(handler->id == handle && !handler->removed) {
            handler->removed = true;
            result = 0;
            break;
        }
    }
    irq_restore(old_irq);

    if(!irq_inside_int())
        reap_removed_handlers();

    if(result < 0)
        errno = ENOENT;

    return result;
}
