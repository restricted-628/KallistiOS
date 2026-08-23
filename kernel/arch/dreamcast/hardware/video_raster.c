/* KallistiOS ##version##

   video_raster.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#include <dc/asic.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/irq.h>

#include "video_raster_internal.h"

typedef struct vid_raster_handler {
    TAILQ_ENTRY(vid_raster_handler) entry;
    int id;
    bool removed;
    vid_raster_callback_t callback;
    void *user_data;
} vid_raster_handler_t;

static TAILQ_HEAD(vid_raster_handler_list, vid_raster_handler) handlers;
static bool initialized;
static bool scanline_configured;
static bool scanline_reachable;
static uint16_t configured_scanline;
static int next_handler_id = 1;
static unsigned int dispatch_depth;
static asic_evt_claim_t hblank_claim = ASIC_EVT_CLAIM_INVALID;

static void initialize_once(void) {
    irq_mask_t old_irq = irq_disable();

    if(!initialized) {
        TAILQ_INIT(&handlers);
        dispatch_depth = 0;
        initialized = true;
    }

    irq_restore(old_irq);
}

/* IRQ callbacks may remove themselves or later callbacks. Storage remains
   linked until a thread-context API call can safely detach and free it. */
static void reap_removed_handlers(void) {
    struct vid_raster_handler_list removed;
    vid_raster_handler_t *handler, *next;
    irq_mask_t old_irq;

    if(irq_inside_int() || !initialized)
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

static bool active_handler_exists(void) {
    vid_raster_handler_t *handler;

    TAILQ_FOREACH(handler, &handlers, entry) {
        if(!handler->removed)
            return true;
    }

    return false;
}

static void release_claim_if_unused(void) {
    if(hblank_claim != ASIC_EVT_CLAIM_INVALID && !active_handler_exists()) {
        (void)asic_evt_release(hblank_claim);
        hblank_claim = ASIC_EVT_CLAIM_INVALID;
    }
}

static void raster_irq_handler(uint32_t code, void *data) {
    vid_scanout_status_t status;
    vid_raster_handler_t *handler;

    (void)code;
    (void)data;

    if(vid_get_scanout_status(&status) < 0)
        return;

    ++dispatch_depth;
    TAILQ_FOREACH(handler, &handlers, entry) {
        if(!handler->removed)
            handler->callback(&status, handler->user_data);
    }
    --dispatch_depth;
}

int vid_raster_set_scanline(uint16_t scanline) {
    uint32_t position;
    irq_mask_t old_irq;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(!vid_mode) {
        errno = ENODEV;
        return -1;
    }

    if(scanline > vid_mode->scanlines) {
        errno = ERANGE;
        return -1;
    }

    initialize_once();
    old_irq = irq_disable();

    /* The comparator shares its register with the horizontal blank position.
       Preserve that position while selecting one exact-line event. */
    position = PVR_GET(PVR_HPOS_IRQ);
    position &= ~(PVR_HPOS_IRQ_SCANLINE | PVR_HPOS_IRQ_MODE);
    position |= FIELD_PREP(PVR_HPOS_IRQ_SCANLINE, scanline);
    position |= FIELD_PREP(PVR_HPOS_IRQ_MODE, PVR_HPOS_IRQ_MODE_EXACT);
    PVR_SET(PVR_HPOS_IRQ, position);

    configured_scanline = scanline;
    scanline_configured = true;
    scanline_reachable = true;

    if(hblank_claim != ASIC_EVT_CLAIM_INVALID)
        (void)asic_evt_claim_unmask(hblank_claim);

    irq_restore(old_irq);
    return 0;
}

int vid_raster_get_scanline(uint16_t *scanline) {
    irq_mask_t old_irq;

    if(!scanline) {
        errno = EFAULT;
        return -1;
    }

    old_irq = irq_disable();
    if(!initialized || !scanline_configured) {
        irq_restore(old_irq);
        errno = ENOENT;
        return -1;
    }

    *scanline = configured_scanline;
    irq_restore(old_irq);
    return 0;
}

int vid_raster_handler_add(vid_raster_callback_t callback, void *user_data) {
    vid_raster_handler_t *handler;
    irq_mask_t old_irq;
    int saved_errno;

    if(!callback) {
        errno = EINVAL;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    initialize_once();
    reap_removed_handlers();

    handler = malloc(sizeof(*handler));
    if(!handler) {
        errno = ENOMEM;
        return -1;
    }

    old_irq = irq_disable();
    if(!scanline_configured) {
        irq_restore(old_irq);
        free(handler);
        errno = EINVAL;
        return -1;
    }

    if(!scanline_reachable) {
        irq_restore(old_irq);
        free(handler);
        errno = ERANGE;
        return -1;
    }

    if(next_handler_id == INT_MAX) {
        irq_restore(old_irq);
        free(handler);
        errno = EOVERFLOW;
        return -1;
    }

    if(hblank_claim == ASIC_EVT_CLAIM_INVALID &&
       asic_evt_claim(ASIC_EVT_PVR_HBLANK_BEGIN, ASIC_IRQ_DEFAULT,
                      raster_irq_handler, NULL, &hblank_claim) < 0) {
        saved_errno = errno;
        irq_restore(old_irq);
        free(handler);
        errno = saved_errno;
        return -1;
    }

    handler->id = next_handler_id++;
    handler->removed = false;
    handler->callback = callback;
    handler->user_data = user_data;
    TAILQ_INSERT_TAIL(&handlers, handler, entry);

    irq_restore(old_irq);
    return handler->id;
}

int vid_raster_handler_remove(int handle) {
    vid_raster_handler_t *handler;
    irq_mask_t old_irq;
    int result = -1;

    if(!initialized) {
        errno = ENOENT;
        return -1;
    }

    old_irq = irq_disable();
    TAILQ_FOREACH(handler, &handlers, entry) {
        if(handler->id == handle && !handler->removed) {
            handler->removed = true;
            result = 0;
            break;
        }
    }

    if(result == 0)
        release_claim_if_unused();

    irq_restore(old_irq);

    if(!irq_inside_int())
        reap_removed_handlers();

    if(result < 0)
        errno = ENOENT;

    return result;
}

void vid_raster_mode_changed(uint16_t maximum_scanline) {
    irq_mask_t old_irq;

    if(!initialized || !scanline_configured)
        return;

    old_irq = irq_disable();
    scanline_reachable = configured_scanline <= maximum_scanline;

    if(hblank_claim != ASIC_EVT_CLAIM_INVALID) {
        if(scanline_reachable)
            (void)asic_evt_claim_unmask(hblank_claim);
        else
            (void)asic_evt_claim_mask(hblank_claim);
    }

    irq_restore(old_irq);
}

void vid_raster_mode_change_begin(void) {
    irq_mask_t old_irq;

    if(!initialized || hblank_claim == ASIC_EVT_CLAIM_INVALID)
        return;

    old_irq = irq_disable();
    (void)asic_evt_claim_mask(hblank_claim);
    irq_restore(old_irq);
}

void vid_raster_shutdown(void) {
    struct vid_raster_handler_list removed;
    vid_raster_handler_t *handler;
    irq_mask_t old_irq;

    if(!initialized)
        return;

    TAILQ_INIT(&removed);
    old_irq = irq_disable();

    if(hblank_claim != ASIC_EVT_CLAIM_INVALID) {
        (void)asic_evt_release(hblank_claim);
        hblank_claim = ASIC_EVT_CLAIM_INVALID;
    }

    while((handler = TAILQ_FIRST(&handlers))) {
        TAILQ_REMOVE(&handlers, handler, entry);
        TAILQ_INSERT_TAIL(&removed, handler, entry);
    }

    scanline_configured = false;
    scanline_reachable = false;
    initialized = false;
    irq_restore(old_irq);

    while((handler = TAILQ_FIRST(&removed))) {
        TAILQ_REMOVE(&removed, handler, entry);
        free(handler);
    }
}
