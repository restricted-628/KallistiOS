/* KallistiOS ##version##

   spu_request.c
   Copyright (C) 2026 Joseph Black
*/

#include <arch/arch.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <kos/cond.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <kos/worker_thread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "spu_internal.h"

#define SPU_TRANSFER_MAGIC             0x53505552u
#define SPU_TRANSFER_PIO_CHUNK         4096u
#define SPU_TRANSFER_WORKER_STACK      16384u
#define SPU_TRANSFER_DMA_POLL_MS       10u
#define SPU_RAM_RETAIL_SIZE            0x00200000u
#define SPU_RAM_DEVELOPMENT_SIZE       0x00800000u

struct spu_transfer_request {
    STAILQ_ENTRY(spu_transfer_request) entry;
    STAILQ_ENTRY(spu_transfer_request) callback_entry;
    uint32_t magic;
    spu_transfer_status_t status;
    uintptr_t sound_address;
    void *memory;
    uint64_t deadline;
    spu_transfer_callback_t callback;
    void *callback_data;
    semaphore_t dma_done;
    uint64_t dma_sequence;
    unsigned int waiters;
    int dma_result;
    bool cancel_requested;
    bool dma_active;
    bool callback_queued;
    bool callback_running;
    bool callback_delivered;
};

static STAILQ_HEAD(spu_request_queue, spu_transfer_request) request_queue =
    STAILQ_HEAD_INITIALIZER(request_queue);
static STAILQ_HEAD(spu_callback_queue, spu_transfer_request) callback_queue =
    STAILQ_HEAD_INITIALIZER(callback_queue);
static mutex_t request_mutex = MUTEX_INITIALIZER;
static mutex_t callback_mutex = MUTEX_INITIALIZER;
static condvar_t callback_idle = COND_INITIALIZER;
static kthread_worker_t *request_worker;
static kthread_worker_t *callback_worker;
static spu_transfer_request_t *active_request;
static spu_transfer_request_t *active_callback;
static volatile bool shutting_down = true;
static bool lifecycle_busy;

static bool transfer_terminal(spu_transfer_state_t state) {
    return state == SPU_TRANSFER_COMPLETE
        || state == SPU_TRANSFER_CANCELLED
        || state == SPU_TRANSFER_TIMED_OUT
        || state == SPU_TRANSFER_ERROR;
}

static size_t sound_ram_capacity(void) {
    return hardware_sys_mode(NULL) == HW_TYPE_RETAIL
        ? SPU_RAM_RETAIL_SIZE : SPU_RAM_DEVELOPMENT_SIZE;
}

static bool deadline_expired(const spu_transfer_request_t *request) {
    return request->deadline && timer_ms_gettime64() >= request->deadline;
}

static uint64_t deadline_from_timeout(uint32_t timeout) {
    uint64_t now;

    if(!timeout)
        return 0;
    now = timer_ms_gettime64();
    return UINT64_MAX - now < timeout ? UINT64_MAX : now + timeout;
}

static bool request_cancelled(const spu_transfer_request_t *request) {
    irq_mask_t irq_state = irq_disable();
    bool cancelled = request->cancel_requested || shutting_down;

    irq_restore(irq_state);
    return cancelled;
}

static void set_running(spu_transfer_request_t *request,
                        spu_transfer_method_t method) {
    irq_disable_scoped();
    request->status.state = SPU_TRANSFER_RUNNING;
    request->status.active_method = method;
    request->status.result = EINPROGRESS;
}

static bool queue_callback_locked(spu_transfer_request_t *request) {
    irq_mask_t irq_state;

    if(!request->callback || !callback_worker)
        return false;

    irq_state = irq_disable();
    request->callback_queued = true;
    irq_restore(irq_state);
    STAILQ_INSERT_TAIL(&callback_queue, request, callback_entry);
    thd_worker_wakeup(callback_worker);
    return true;
}

static void finish_request(spu_transfer_request_t *request,
                           spu_transfer_state_t state, int result) {
    spu_transfer_status_t status;
    spu_transfer_callback_t callback = request->callback;
    void *callback_data = request->callback_data;
    bool queued = false;
    irq_mask_t irq_state;

    /* Stop publishing the request as the active cancellation target before a
       terminal wake allows its caller to destroy it. */
    mutex_lock(&request_mutex);
    if(active_request == request)
        active_request = NULL;
    mutex_unlock(&request_mutex);

    if(callback)
        mutex_lock(&callback_mutex);

    irq_state = irq_disable();
    request->status.state = state;
    request->status.result = result;
    if(state == SPU_TRANSFER_COMPLETE)
        request->status.completed_bytes = request->status.requested_bytes;
    status = request->status;
    irq_restore(irq_state);

    if(callback)
        queued = queue_callback_locked(request);

    genwait_wake_all(request);

    if(callback)
        mutex_unlock(&callback_mutex);

    /* Lifecycle ordering keeps the dispatcher alive until every producer has
       stopped. Preserve delivery if that invariant is changed later. */
    if(callback && !queued) {
        irq_state = irq_disable();
        request->callback_running = true;
        irq_restore(irq_state);
        status.callback_pending = true;
        callback(request, &status, callback_data);
        irq_state = irq_disable();
        request->callback_running = false;
        request->callback_delivered = true;
        irq_restore(irq_state);
        genwait_wake_all(request);
    }
}

static void callback_worker_routine(void *data) {
    spu_transfer_request_t *request;
    spu_transfer_status_t status;
    irq_mask_t irq_state;

    (void)data;

    for(;;) {
        mutex_lock(&callback_mutex);
        request = STAILQ_FIRST(&callback_queue);

        if(!request) {
            active_callback = NULL;
            cond_broadcast(&callback_idle);
            mutex_unlock(&callback_mutex);
            return;
        }

        STAILQ_REMOVE_HEAD(&callback_queue, callback_entry);
        active_callback = request;
        irq_state = irq_disable();
        request->callback_queued = false;
        request->callback_running = true;
        status = request->status;
        status.callback_pending = true;
        irq_restore(irq_state);
        mutex_unlock(&callback_mutex);

        request->callback(request, &status, request->callback_data);

        irq_state = irq_disable();
        request->callback_running = false;
        request->callback_delivered = true;
        irq_restore(irq_state);
        genwait_wake_all(request);

        mutex_lock(&callback_mutex);
        active_callback = NULL;
        if(STAILQ_EMPTY(&callback_queue))
            cond_broadcast(&callback_idle);
        mutex_unlock(&callback_mutex);
    }
}

/* The generic G2 callback runs in interrupt context. It only publishes the
   terminal DMA generation and wakes the private transfer worker; application
   callbacks are deliberately dispatched elsewhere. */
static void dma_complete(void *data) {
    spu_transfer_request_t *request = data;

    if(request->magic != SPU_TRANSFER_MAGIC || !request->dma_active)
        return;

    request->dma_active = false;
    request->dma_result = 0;
    request->status.completed_bytes = request->status.requested_bytes;
    sem_signal(&request->dma_done);
}

static void sample_dma_progress_locked(const spu_transfer_request_t *request,
                                       spu_transfer_status_t *status) {
    g2_dma_status_t dma;
    size_t completed;
    int saved_errno = errno;

    if(!request->dma_active
       || g2_dma_get_status(G2_DMA_CHAN_SPU, &dma) < 0
       || dma.sequence != request->dma_sequence
       || (dma.state != G2_DMA_STATE_RUNNING
           && dma.state != G2_DMA_STATE_SUSPENDED)) {
        errno = saved_errno;
        return;
    }

    completed = dma.remaining_bytes > status->requested_bytes
        ? 0 : status->requested_bytes - dma.remaining_bytes;
    if(completed > status->completed_bytes)
        status->completed_bytes = completed;
    errno = saved_errno;
}

/* Interrupts must be disabled so completion cannot transfer ownership of DMA
   channel zero between the active test and cancellation. */
static void stop_request_dma_locked(spu_transfer_request_t *request,
                                    int result) {
    g2_dma_status_t dma;

    if(!request->dma_active)
        return;

    if(g2_dma_get_status(G2_DMA_CHAN_SPU, &dma) == 0
       && dma.sequence == request->dma_sequence
       && (dma.state == G2_DMA_STATE_RUNNING
           || dma.state == G2_DMA_STATE_SUSPENDED)) {
        size_t completed = dma.remaining_bytes > request->status.requested_bytes
            ? 0 : request->status.requested_bytes - dma.remaining_bytes;

        if(completed > request->status.completed_bytes)
            request->status.completed_bytes = completed;
        (void)g2_dma_cancel(G2_DMA_CHAN_SPU);
    }

    /* G2 shutdown cancels without invoking the channel callback. Always wake
       our worker even when the generic channel is already terminal. */
    request->dma_active = false;
    request->dma_result = result;
    sem_signal(&request->dma_done);
}

static int start_dma(spu_transfer_request_t *request) {
    void *g2_address = (void *)(SPU_RAM_BASE | request->sound_address);
    uint32_t direction = request->status.direction == SPU_TRANSFER_TO_SOUND_RAM
        ? G2_DMA_TO_G2 : G2_DMA_TO_SH4;
    g2_dma_status_t dma;
    irq_mask_t irq_state;
    int result;

    irq_state = irq_disable();
    request->dma_active = true;
    request->dma_result = EINPROGRESS;
    irq_restore(irq_state);

    result = g2_dma_transfer(request->memory, g2_address,
                             request->status.requested_bytes, 0,
                             dma_complete, request, direction, 0,
                             G2_DMA_CHAN_SPU, 0);
    if(result < 0) {
        irq_state = irq_disable();
        request->dma_active = false;
        request->dma_result = errno;
        irq_restore(irq_state);
        return -1;
    }

    /* Capture the admitted channel generation while completion is excluded.
       This makes later progress sampling and cancellation ownership-safe. */
    irq_state = irq_disable();
    if(request->dma_active
       && g2_dma_get_status(G2_DMA_CHAN_SPU, &dma) == 0)
        request->dma_sequence = dma.sequence;
    irq_restore(irq_state);
    return 0;
}

static int wait_for_dma(spu_transfer_request_t *request) {
    for(;;) {
        uint32_t wait_time = SPU_TRANSFER_DMA_POLL_MS;
        irq_mask_t irq_state;
        int dma_result;

        if(request_cancelled(request)) {
            irq_state = irq_disable();
            stop_request_dma_locked(request, ECANCELED);
            irq_restore(irq_state);
        }
        else if(deadline_expired(request)) {
            irq_state = irq_disable();
            stop_request_dma_locked(request, ETIMEDOUT);
            irq_restore(irq_state);
        }

        if(request->deadline) {
            uint64_t now = timer_ms_gettime64();
            uint64_t remaining = request->deadline > now
                ? request->deadline - now : 1;

            if(remaining < wait_time)
                wait_time = (uint32_t)remaining;
        }

        if(sem_wait_timed(&request->dma_done, wait_time) < 0
           && errno != ETIMEDOUT)
            return -1;

        irq_state = irq_disable();
        dma_result = request->dma_result;
        if(request->dma_active) {
            spu_transfer_status_t status = request->status;

            sample_dma_progress_locked(request, &status);
            request->status.completed_bytes = status.completed_bytes;
        }
        irq_restore(irq_state);

        if(dma_result != EINPROGRESS) {
            if(dma_result) {
                errno = dma_result;
                return -1;
            }
            return 0;
        }
    }
}

static int process_pio(spu_transfer_request_t *request) {
    size_t completed = 0;

    set_running(request, SPU_TRANSFER_PIO);

    while(completed < request->status.requested_bytes) {
        size_t count = request->status.requested_bytes - completed;
        irq_mask_t irq_state;

        if(request_cancelled(request)) {
            errno = ECANCELED;
            return -1;
        }
        if(deadline_expired(request)) {
            errno = ETIMEDOUT;
            return -1;
        }

        if(count > SPU_TRANSFER_PIO_CHUNK)
            count = SPU_TRANSFER_PIO_CHUNK;

        if(request->status.direction == SPU_TRANSFER_TO_SOUND_RAM) {
            spu_memload(request->sound_address + completed,
                        (const uint8_t *)request->memory + completed, count);
        }
        else {
            spu_memread((uint8_t *)request->memory + completed,
                        request->sound_address + completed, count);
        }

        completed += count;
        irq_state = irq_disable();
        request->status.completed_bytes = completed;
        irq_restore(irq_state);
        thd_pass();
    }

    return 0;
}

static int process_dma(spu_transfer_request_t *request, bool retry_busy) {
    set_running(request, SPU_TRANSFER_DMA);

    while(start_dma(request) < 0) {
        int error = errno;

        if(error != EINPROGRESS || !retry_busy) {
            errno = error;
            return -1;
        }
        if(request_cancelled(request)) {
            errno = ECANCELED;
            return -1;
        }
        if(deadline_expired(request)) {
            errno = ETIMEDOUT;
            return -1;
        }
        thd_sleep(1);
    }

    return wait_for_dma(request);
}

static void process_request(spu_transfer_request_t *request) {
    bool dma_compatible = __is_aligned(request->memory, 32)
        && !(request->sound_address & 31u)
        && !(request->status.requested_bytes & 31u);
    int result;

    if(request_cancelled(request)) {
        finish_request(request, SPU_TRANSFER_CANCELLED, ECANCELED);
        return;
    }
    if(deadline_expired(request)) {
        finish_request(request, SPU_TRANSFER_TIMED_OUT, ETIMEDOUT);
        return;
    }

    if(request->status.requested_method == SPU_TRANSFER_DMA
       || (request->status.requested_method == SPU_TRANSFER_AUTO
           && dma_compatible)) {
        result = process_dma(request,
                             request->status.requested_method
                                 == SPU_TRANSFER_DMA);

        /* AUTO is a transfer policy, not a DMA guarantee. If channel zero is
           occupied or DMA is unavailable, exact PIO preserves forward progress
           without reserving another worker or staging buffer. */
        if(result < 0 && request->status.requested_method == SPU_TRANSFER_AUTO
           && request->status.completed_bytes == 0
           && (errno == EINPROGRESS || errno == ENODEV || errno == EFAULT))
            result = process_pio(request);
    }
    else {
        result = process_pio(request);
    }

    if(result == 0)
        finish_request(request, SPU_TRANSFER_COMPLETE, 0);
    else if(errno == ECANCELED)
        finish_request(request, SPU_TRANSFER_CANCELLED, ECANCELED);
    else if(errno == ETIMEDOUT)
        finish_request(request, SPU_TRANSFER_TIMED_OUT, ETIMEDOUT);
    else
        finish_request(request, SPU_TRANSFER_ERROR, errno ? errno : EIO);
}

static void request_worker_routine(void *data) {
    spu_transfer_request_t *request;

    (void)data;

    for(;;) {
        mutex_lock(&request_mutex);
        request = STAILQ_FIRST(&request_queue);
        if(!request) {
            active_request = NULL;
            mutex_unlock(&request_mutex);
            return;
        }
        STAILQ_REMOVE_HEAD(&request_queue, entry);
        active_request = request;
        mutex_unlock(&request_mutex);

        process_request(request);
    }
}

static int workers_start_locked(void) {
    static const kthread_attr_t request_attrs = {
        .stack_size = SPU_TRANSFER_WORKER_STACK,
        .prio = PRIO_DEFAULT - 2,
        .label = "[spu-transfer]"
    };
    static const kthread_attr_t callback_attrs = {
        .label = "[spu-callback]"
    };

    if(request_worker)
        return 0;

    callback_worker = thd_worker_create_ex(&callback_attrs,
                                            callback_worker_routine, NULL);
    if(!callback_worker)
        goto failure;

    request_worker = thd_worker_create_ex(&request_attrs,
                                           request_worker_routine, NULL);
    if(!request_worker) {
        thd_worker_destroy(callback_worker);
        callback_worker = NULL;
        goto failure;
    }
    return 0;

failure:
    errno = ENOMEM;
    return -1;
}

int spu_transfer_submit(spu_transfer_direction_t direction,
                        uintptr_t sound_address, void *memory, size_t length,
                        spu_transfer_method_t method,
                        uint32_t execution_timeout,
                        spu_transfer_callback_t callback,
                        void *callback_data,
                        spu_transfer_request_t **output) {
    spu_transfer_request_t *request;
    size_t capacity = sound_ram_capacity();

    if(output)
        *output = NULL;

    if(!output || !memory || !length
       || direction > SPU_TRANSFER_FROM_SOUND_RAM
       || method > SPU_TRANSFER_DMA
       || sound_address >= capacity || length > capacity - sound_address) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(method == SPU_TRANSFER_DMA
       && !__is_aligned(memory, 32)) {
        errno = EFAULT;
        return -1;
    }
    if(method == SPU_TRANSFER_DMA
       && ((sound_address & 31u) || (length & 31u))) {
        errno = EINVAL;
        return -1;
    }

    request = calloc(1, sizeof(*request));
    if(!request) {
        errno = ENOMEM;
        return -1;
    }
    if(sem_init(&request->dma_done, 0) < 0) {
        int saved_errno = errno;

        free(request);
        errno = saved_errno;
        return -1;
    }

    request->magic = SPU_TRANSFER_MAGIC;
    request->sound_address = sound_address;
    request->memory = memory;
    request->callback = callback;
    request->callback_data = callback_data;
    request->dma_result = EINPROGRESS;
    request->status = (spu_transfer_status_t) {
        .state = SPU_TRANSFER_QUEUED,
        .direction = direction,
        .requested_method = method,
        .active_method = method,
        .requested_bytes = length,
        .result = EINPROGRESS
    };

    mutex_lock(&request_mutex);
    if(shutting_down) {
        mutex_unlock(&request_mutex);
        sem_destroy(&request->dma_done);
        free(request);
        errno = ENODEV;
        return -1;
    }
    if(workers_start_locked() < 0) {
        int saved_errno = errno;

        mutex_unlock(&request_mutex);
        sem_destroy(&request->dma_done);
        free(request);
        errno = saved_errno;
        return -1;
    }

    request->deadline = deadline_from_timeout(execution_timeout);
    STAILQ_INSERT_TAIL(&request_queue, request, entry);
    *output = request;
    thd_worker_wakeup(request_worker);
    mutex_unlock(&request_mutex);
    return 0;
}

int spu_memload_async(uintptr_t to, const void *from, size_t length,
                      spu_transfer_method_t method,
                      uint32_t execution_timeout,
                      spu_transfer_callback_t callback, void *callback_data,
                      spu_transfer_request_t **request) {
    return spu_transfer_submit(SPU_TRANSFER_TO_SOUND_RAM, to, (void *)from,
                               length, method, execution_timeout, callback,
                               callback_data, request);
}

int spu_memread_async(void *to, uintptr_t from, size_t length,
                      spu_transfer_method_t method,
                      uint32_t execution_timeout,
                      spu_transfer_callback_t callback, void *callback_data,
                      spu_transfer_request_t **request) {
    return spu_transfer_submit(SPU_TRANSFER_FROM_SOUND_RAM, from, to, length,
                               method, execution_timeout, callback,
                               callback_data, request);
}

static int copy_status_locked(const spu_transfer_request_t *request,
                              spu_transfer_status_t *status) {
    if(request->magic != SPU_TRANSFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    *status = request->status;
    sample_dma_progress_locked(request, status);
    status->callback_pending = request->callback_queued
        || request->callback_running;
    return 0;
}

int spu_transfer_get_status(const spu_transfer_request_t *request,
                            spu_transfer_status_t *status) {
    irq_mask_t irq_state;
    int result;

    if(status)
        *status = (spu_transfer_status_t) { 0 };
    if(!request || !status) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();
    result = copy_status_locked(request, status);
    irq_restore(irq_state);
    return result;
}

static void waiter_leave(spu_transfer_request_t *request) {
    irq_disable_scoped();
    --request->waiters;
}

int spu_transfer_wait(spu_transfer_request_t *request, uint32_t timeout,
                      spu_transfer_status_t *status) {
    spu_transfer_status_t current = { 0 };
    uint64_t deadline = deadline_from_timeout(timeout);
    irq_mask_t irq_state;

    if(status)
        *status = (spu_transfer_status_t) { 0 };
    if(!request) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq_state = irq_disable();
    if(request->magic != SPU_TRANSFER_MAGIC) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }
    ++request->waiters;
    irq_restore(irq_state);

    for(;;) {
        uint32_t wait_time = 0;
        int wait_result;

        irq_state = irq_disable();
        if(copy_status_locked(request, &current) < 0) {
            irq_restore(irq_state);
            waiter_leave(request);
            return -1;
        }
        if(transfer_terminal(current.state)) {
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
            wait_time = (uint32_t)(deadline - now);
        }

        wait_result = genwait_wait(request, "SPU transfer", wait_time);
        irq_restore(irq_state);
        if(wait_result < 0) {
            if(spu_transfer_get_status(request, &current) == 0
               && transfer_terminal(current.state))
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

int spu_transfer_wait_callback(spu_transfer_request_t *request,
                               uint32_t timeout) {
    uint64_t deadline = deadline_from_timeout(timeout);
    irq_mask_t irq_state;
    bool callback_context;

    if(!request) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    mutex_lock(&callback_mutex);
    callback_context = callback_worker
        && thd_get_current() == thd_worker_get_thread(callback_worker);
    mutex_unlock(&callback_mutex);
    if(callback_context) {
        errno = EDEADLK;
        return -1;
    }

    irq_state = irq_disable();
    if(request->magic != SPU_TRANSFER_MAGIC) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }
    ++request->waiters;

    while(request->callback && !request->callback_delivered) {
        uint32_t wait_time = 0;
        int wait_result;

        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                irq_restore(irq_state);
                waiter_leave(request);
                errno = ETIMEDOUT;
                return -1;
            }
            wait_time = (uint32_t)(deadline - now);
        }

        wait_result = genwait_wait(request, "SPU transfer callback", wait_time);
        if(wait_result < 0) {
            irq_restore(irq_state);
            waiter_leave(request);
            if(errno == EAGAIN)
                errno = ETIMEDOUT;
            return -1;
        }
    }

    irq_restore(irq_state);
    waiter_leave(request);
    return 0;
}

int spu_transfer_cancel(spu_transfer_request_t *request) {
    spu_transfer_request_t *item;
    bool queued = false;
    irq_mask_t irq_state;

    if(!request) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    mutex_lock(&request_mutex);
    irq_state = irq_disable();
    if(request->magic != SPU_TRANSFER_MAGIC) {
        irq_restore(irq_state);
        mutex_unlock(&request_mutex);
        errno = EINVAL;
        return -1;
    }
    if(transfer_terminal(request->status.state)) {
        irq_restore(irq_state);
        mutex_unlock(&request_mutex);
        errno = EALREADY;
        return -1;
    }
    request->cancel_requested = true;
    if(request->status.state == SPU_TRANSFER_QUEUED) {
        STAILQ_FOREACH(item, &request_queue, entry) {
            if(item == request) {
                queued = true;
                break;
            }
        }
    }
    else if(request->dma_active) {
        stop_request_dma_locked(request, ECANCELED);
    }
    irq_restore(irq_state);

    if(queued)
        STAILQ_REMOVE(&request_queue, request, spu_transfer_request, entry);
    mutex_unlock(&request_mutex);

    if(queued)
        finish_request(request, SPU_TRANSFER_CANCELLED, ECANCELED);
    return 0;
}

int spu_transfer_destroy(spu_transfer_request_t *request) {
    bool busy;
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
    if(request->magic != SPU_TRANSFER_MAGIC) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }
    busy = !transfer_terminal(request->status.state) || request->waiters
        || request->callback_queued || request->callback_running
        || (request->callback && !request->callback_delivered);
    if(!busy)
        request->magic = 0;
    irq_restore(irq_state);

    if(busy) {
        errno = EBUSY;
        return -1;
    }

    sem_destroy(&request->dma_done);
    free(request);
    return 0;
}

void spu_transfer_system_init(void) {
    irq_mask_t irq_state;

    mutex_lock_scoped(&request_mutex);
    irq_state = irq_disable();
    shutting_down = false;
    irq_restore(irq_state);
}

int spu_transfer_system_shutdown(void) {
    struct spu_request_queue cancelled = STAILQ_HEAD_INITIALIZER(cancelled);
    kthread_worker_t *worker;
    kthread_worker_t *callbacks;
    spu_transfer_request_t *request;
    irq_mask_t irq_state;
    bool request_context;
    bool callback_context;

    mutex_lock(&request_mutex);
    if(lifecycle_busy) {
        mutex_unlock(&request_mutex);
        errno = EBUSY;
        return -1;
    }
    request_context = request_worker
        && thd_get_current() == thd_worker_get_thread(request_worker);
    lifecycle_busy = true;
    mutex_unlock(&request_mutex);
    mutex_lock(&callback_mutex);
    callback_context = callback_worker
        && thd_get_current() == thd_worker_get_thread(callback_worker);
    mutex_unlock(&callback_mutex);
    if(request_context || callback_context) {
        mutex_lock(&request_mutex);
        lifecycle_busy = false;
        mutex_unlock(&request_mutex);
        errno = EDEADLK;
        return -1;
    }

    mutex_lock(&request_mutex);
    irq_state = irq_disable();
    shutting_down = true;
    irq_restore(irq_state);
    while((request = STAILQ_FIRST(&request_queue)) != NULL) {
        STAILQ_REMOVE_HEAD(&request_queue, entry);
        STAILQ_INSERT_TAIL(&cancelled, request, entry);
    }
    if(active_request) {
        irq_state = irq_disable();
        active_request->cancel_requested = true;
        stop_request_dma_locked(active_request, ECANCELED);
        irq_restore(irq_state);
    }
    worker = request_worker;
    mutex_unlock(&request_mutex);

    while((request = STAILQ_FIRST(&cancelled)) != NULL) {
        STAILQ_REMOVE_HEAD(&cancelled, entry);
        finish_request(request, SPU_TRANSFER_CANCELLED, ECANCELED);
    }

    if(worker)
        thd_worker_destroy(worker);

    mutex_lock(&request_mutex);
    request_worker = NULL;
    mutex_unlock(&request_mutex);

    mutex_lock(&callback_mutex);
    while(active_callback || !STAILQ_EMPTY(&callback_queue))
        cond_wait(&callback_idle, &callback_mutex);
    callbacks = callback_worker;
    callback_worker = NULL;
    mutex_unlock(&callback_mutex);

    if(callbacks)
        thd_worker_destroy(callbacks);

    mutex_lock(&request_mutex);
    lifecycle_busy = false;
    mutex_unlock(&request_mutex);

    return 0;
}
