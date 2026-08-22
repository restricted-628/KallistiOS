/* KallistiOS ##version##

   pvr_texture_request.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdlib.h>

#include <arch/dmac.h>
#include <dc/pvr.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>

#include "pvr_internal.h"

#define PVR_TXR_REQUEST_MAGIC 0x50565251u

struct pvr_txr_request {
    uint32_t magic;
    pvr_txr_request_status_t status;
    unsigned int waiters;
    bool yuv;
    bool dma_done;
    bool yuv_done;
};

/* The hardware and pvr_state.dma_lock permit one channel-2 transfer at a
   time. A checked request is therefore either admitted immediately or not
   created; there is no hidden queue or progress pump. */
static pvr_txr_request_t *active_request;

static bool request_state_terminal(pvr_txr_request_state_t state) {
    return state == PVR_TXR_REQUEST_COMPLETE
        || state == PVR_TXR_REQUEST_FAILED
        || state == PVR_TXR_REQUEST_CANCELLED;
}

static bool surface_storage_valid(const pvr_txr_surface_t *surface) {
    pvr_txr_level_info_t level;

    if(pvr_txr_surface_get_level(surface, 0, &level) < 0)
        return false;

    if(!surface->vram) {
        errno = ENODEV;
        return false;
    }

    if(surface->capacity < surface->byte_size) {
        errno = ENOSPC;
        return false;
    }

    return true;
}

/* Interrupts are already excluded by every caller. Terminal state is visible
   before either the shared DMA channel or request waiters are released. */
static void finish_request(pvr_txr_request_t *request,
                           pvr_txr_request_state_t state,
                           int result, uint32_t detail) {
    request->status.state = state;
    request->status.result = result;
    request->status.detail = detail;

    if(state == PVR_TXR_REQUEST_COMPLETE) {
        request->status.completed_bytes = request->status.requested_bytes;
        request->status.completed_macroblocks
            = request->status.requested_macroblocks;
    }

    if(active_request == request)
        active_request = NULL;

    sem_signal((semaphore_t *)&pvr_state.dma_lock);
    genwait_wake_all(request);
}

static void request_dma_complete(void *data) {
    pvr_txr_request_t *request = data;
    size_t remaining = pvr_dma_completion_remaining();
    uint32_t detail = pvr_dma_completion_detail();

    irq_disable_scoped();

    if(active_request != request
       || request->magic != PVR_TXR_REQUEST_MAGIC)
        return;

    if(!pvr_state.valid) {
        finish_request(request, PVR_TXR_REQUEST_CANCELLED, ECANCELED, 0);
        thd_schedule(true);
        return;
    }

    if(remaining > request->status.requested_bytes)
        remaining = request->status.requested_bytes;

    request->status.completed_bytes
        = request->status.requested_bytes - remaining;
    request->dma_done = true;

    if(detail) {
        finish_request(request, PVR_TXR_REQUEST_FAILED, EIO, detail);
        thd_schedule(true);
        return;
    }

    request->status.completed_bytes = request->status.requested_bytes;

    if(!request->yuv) {
        finish_request(request, PVR_TXR_REQUEST_COMPLETE, 0, 0);
        thd_schedule(true);
        return;
    }

    /* Channel-2 completion only says the converter accepted its input. The
       destination becomes usable after the separate YUV-done interrupt. */
    request->status.state = PVR_TXR_REQUEST_CONVERTING;
    if(request->yuv_done) {
        finish_request(request, PVR_TXR_REQUEST_COMPLETE, 0, 0);
        thd_schedule(true);
    }
}

void pvr_txr_yuv_complete(void) {
    pvr_txr_request_t *request;

    irq_disable_scoped();
    request = active_request;

    if(!request || !request->yuv
       || request->magic != PVR_TXR_REQUEST_MAGIC)
        return;

    if(!pvr_state.valid) {
        finish_request(request, PVR_TXR_REQUEST_CANCELLED, ECANCELED, 0);
        thd_schedule(true);
        return;
    }

    /* Keep the early-interrupt case ordered: completion is published only
       after both the converter and source DMA have reached their endpoints. */
    request->yuv_done = true;
    request->status.completed_macroblocks
        = request->status.requested_macroblocks;

    if(request->dma_done) {
        finish_request(request, PVR_TXR_REQUEST_COMPLETE, 0, 0);
        thd_schedule(true);
    }
}

void pvr_txr_request_shutdown(void) {
    pvr_txr_request_t *request;

    irq_disable_scoped();
    request = active_request;

    if(!request || request->magic != PVR_TXR_REQUEST_MAGIC)
        return;

    finish_request(request, PVR_TXR_REQUEST_CANCELLED, ECANCELED, 0);
}

static int start_request(const pvr_txr_surface_t *surface, size_t offset,
                         const void *src, size_t byte_size, bool yuv,
                         pvr_txr_yuv_format_t yuv_format,
                         pvr_txr_request_t **output) {
    pvr_txr_request_t *request;
    irq_mask_t irq_state;
    uint8_t *destination;
    int result;

    if(output)
        *output = NULL;

    if(!output || !src || !byte_size) {
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

    destination = (uint8_t *)surface->vram + offset;
    if(((uintptr_t)src & 31u) || (byte_size & 31u)
       || (!yuv && (((uintptr_t)destination & 31u) || (offset & 31u)))) {
        errno = EINVAL;
        return -1;
    }

    request = calloc(1, sizeof(*request));
    if(!request) {
        errno = ENOMEM;
        return -1;
    }

    request->magic = PVR_TXR_REQUEST_MAGIC;
    request->yuv = yuv;
    request->status.state = PVR_TXR_REQUEST_DMA;
    request->status.requested_bytes = byte_size;

    if(yuv) {
        request->status.requested_macroblocks
            = (surface->width / 16u) * (surface->height / 16u);
    }

    if(sem_trywait((semaphore_t *)&pvr_state.dma_lock) < 0) {
        request->magic = 0;
        free(request);
        errno = EBUSY;
        return -1;
    }

    /* Keep interrupts excluded from publication through hardware start. A
       stale or extremely fast YUV interrupt cannot observe a half-admitted
       request, and the DMA callback cannot beat output publication. */
    irq_state = irq_disable();
    if(!pvr_state.valid || active_request) {
        bool unavailable = !pvr_state.valid;

        irq_restore(irq_state);
        sem_signal((semaphore_t *)&pvr_state.dma_lock);
        request->magic = 0;
        free(request);
        errno = unavailable ? ENODEV : EIO;
        return -1;
    }

    active_request = request;

    if(yuv) {
        uint32_t control = ((uint32_t)yuv_format << 24)
                         | ((surface->height / 16u - 1u) << 8)
                         | (surface->width / 16u - 1u);

        /* Writing the base reinitializes the converter. The required control
           read drains the TA register write buffer before channel-2 starts. */
        PVR_SET(PVR_YUV_ADDR, (uintptr_t)surface->vram & 0x00ffffffu);
        PVR_SET(PVR_YUV_CFG, control);
        (void)PVR_GET(PVR_YUV_CFG);
        result = pvr_dma_yuv_conv(src, byte_size, false,
                                  request_dma_complete, request);
    }
    else {
        result = pvr_txr_load_dma(src, destination, byte_size, false,
                                  request_dma_complete, request);
    }

    if(result < 0) {
        int saved_errno = errno;

        active_request = NULL;
        irq_restore(irq_state);
        sem_signal((semaphore_t *)&pvr_state.dma_lock);
        request->magic = 0;
        free(request);
        errno = saved_errno;
        return -1;
    }

    *output = request;
    irq_restore(irq_state);
    return 0;
}

int pvr_txr_surface_upload_part_async(const pvr_txr_surface_t *surface,
                                      size_t offset, const void *src,
                                      size_t byte_size,
                                      pvr_txr_request_t **request) {
    if(request)
        *request = NULL;

    if(!request || !src || !byte_size) {
        errno = EINVAL;
        return -1;
    }

    if(!surface_storage_valid(surface))
        return -1;

    if(offset > surface->byte_size
       || byte_size > surface->byte_size - offset) {
        errno = EINVAL;
        return -1;
    }

    return start_request(surface, offset, src, byte_size, false,
                         PVR_TXR_YUV420, request);
}

int pvr_txr_surface_upload_async(const pvr_txr_surface_t *surface,
                                 const void *src, size_t byte_size,
                                 pvr_txr_request_t **request) {
    if(request)
        *request = NULL;

    if(!surface || byte_size != surface->byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part_async(surface, 0, src, byte_size,
                                              request);
}

int pvr_txr_surface_upload_level_async(const pvr_txr_surface_t *surface,
                                       uint32_t level, const void *src,
                                       size_t byte_size,
                                       pvr_txr_request_t **request) {
    pvr_txr_level_info_t info;

    if(request)
        *request = NULL;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    if(pvr_txr_surface_get_level(surface, level, &info) < 0)
        return -1;

    if(byte_size != info.byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part_async(surface, info.offset, src,
                                              byte_size, request);
}

int pvr_txr_surface_upload_codebook_async(const pvr_txr_surface_t *surface,
                                          const void *src, size_t byte_size,
                                          pvr_txr_request_t **request) {
    if(request)
        *request = NULL;

    if(!request || !surface || surface->layout != PVR_TXR_SURFACE_VQ
       || byte_size != surface->codebook_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part_async(surface, 0, src, byte_size,
                                              request);
}

int pvr_txr_surface_yuv_upload_async(const pvr_txr_surface_t *surface,
                                     pvr_txr_yuv_format_t format,
                                     const void *src, size_t byte_size,
                                     pvr_txr_request_t **request) {
    size_t expected_size;

    if(request)
        *request = NULL;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    if(!surface_storage_valid(surface)
       || pvr_txr_surface_yuv_input_size(surface, format,
                                         &expected_size) < 0)
        return -1;

    if(byte_size != expected_size) {
        errno = EINVAL;
        return -1;
    }

    return start_request(surface, 0, src, byte_size, true, format, request);
}

static int copy_status_locked(const pvr_txr_request_t *request,
                              pvr_txr_request_status_t *status) {
    if(request->magic != PVR_TXR_REQUEST_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    *status = request->status;

    if(active_request == request
       && request->status.state == PVR_TXR_REQUEST_DMA) {
        size_t remaining = dma_transfer_get_remaining(DMA_CHANNEL_2);

        if(remaining > status->requested_bytes)
            remaining = status->requested_bytes;
        status->completed_bytes = status->requested_bytes - remaining;
    }

    if(active_request == request && request->yuv
       && !request_state_terminal(request->status.state)) {
        uint32_t completed = PVR_GET(PVR_YUV_STAT) & 0x1fffu;

        if(completed > status->requested_macroblocks)
            completed = status->requested_macroblocks;
        status->completed_macroblocks = completed;
    }

    return 0;
}

int pvr_txr_request_get_status(const pvr_txr_request_t *request,
                               pvr_txr_request_status_t *status) {
    irq_mask_t irq_state;
    int result;

    if(status)
        *status = (pvr_txr_request_status_t) { 0 };

    if(!request || !status) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();
    result = copy_status_locked(request, status);
    irq_restore(irq_state);
    return result;
}

static void waiter_leave(pvr_txr_request_t *request) {
    irq_disable_scoped();
    --request->waiters;
}

int pvr_txr_request_wait(pvr_txr_request_t *request, uint32_t timeout,
                         pvr_txr_request_status_t *status) {
    pvr_txr_request_status_t current;
    uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    irq_mask_t irq_state;

    if(status)
        *status = (pvr_txr_request_status_t) { 0 };

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();
    if(request->magic != PVR_TXR_REQUEST_MAGIC) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }
    ++request->waiters;
    irq_restore(irq_state);

    for(;;) {
        uint32_t remaining_timeout = 0;
        int wait_result;

        /* The state test and insertion into the generic wait queue are one
           IRQ-atomic operation, preventing a completion wake from being lost. */
        irq_state = irq_disable();
        if(copy_status_locked(request, &current) < 0) {
            irq_restore(irq_state);
            waiter_leave(request);
            return -1;
        }

        if(request_state_terminal(current.state)) {
            irq_restore(irq_state);
            break;
        }

        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                irq_restore(irq_state);
                waiter_leave(request);
                if(status)
                    *status = current;
                errno = ETIMEDOUT;
                return -1;
            }

            remaining_timeout = (uint32_t)(deadline - now);
        }

        wait_result = genwait_wait(request, "PVR texture transfer",
                                   remaining_timeout);
        irq_restore(irq_state);

        if(wait_result < 0) {
            /* Reconcile a completion that raced the timeout wakeup. */
            if(pvr_txr_request_get_status(request, &current) == 0
               && request_state_terminal(current.state))
                break;

            waiter_leave(request);
            if(status)
                *status = current;
            if(errno == EAGAIN)
                errno = ETIMEDOUT;
            return -1;
        }
    }

    waiter_leave(request);
    if(status)
        *status = current;

    if(current.result) {
        errno = current.result;
        return -1;
    }

    return 0;
}

int pvr_txr_request_destroy(pvr_txr_request_t *request) {
    irq_mask_t irq_state;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();
    if(request->magic != PVR_TXR_REQUEST_MAGIC) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }

    if(!request_state_terminal(request->status.state) || request->waiters) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    request->magic = 0;
    irq_restore(irq_state);
    free(request);
    return 0;
}
