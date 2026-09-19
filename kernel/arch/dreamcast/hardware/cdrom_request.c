/* KallistiOS ##version##

   cdrom_request.c
   Copyright (C) 2026 Joseph Black

*/

#include <dc/cdrom.h>
#include <dc/memory.h>
#include <dc/syscalls.h>

#include <kos/genwait.h>
#include <kos/cache.h>
#include <kos/cond.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <kos/worker_thread.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "cdrom_request.h"
#include "g1_bus.h"
#include "gdrom_direct_internal.h"

#define DIRECT_DMA_COMMAND_TIMEOUT_MS 4000u
#define DIRECT_STREAM_TRANSFER_TIMEOUT_MS 30000u

static int direct_error_result(
    int error, const gdrom_direct_result_t *transport);
static void finish_request(cdrom_request_t *request,
                           cdrom_request_state_t state, int result);

static void finish_g1_lock_failure(cdrom_request_t *request) {
    int error = errno;

    if(error == ETIMEDOUT)
        finish_request(request, CDROM_REQUEST_TIMED_OUT, ERR_TIMEOUT);
    else
        finish_request(request, CDROM_REQUEST_ERROR,
                       error == EIO ? ERR_HARDWARE : ERR_SYS);
}

/* Shared with the blocking CD-ROM and G1 ATA paths. */
extern int cdrom_dma_request_begin(gdc_cmd_hnd_t handle, semaphore_t *done,
                                   size_t transfer_size, bool stream_transfer);
extern bool cdrom_dma_request_active(gdc_cmd_hnd_t handle);
extern bool cdrom_dma_request_snapshot(gdc_cmd_hnd_t handle,
                                       cd_cmd_chk_t *response,
                                       cd_cmd_chk_status_t *status);
extern void cdrom_dma_request_end(gdc_cmd_hnd_t handle);
extern int cdrom_stream_request_claim(void);
extern bool cdrom_stream_sector_size_matches(size_t sector_size);

struct cdrom_stream_session;

struct cdrom_request {
    STAILQ_ENTRY(cdrom_request) entry;
    STAILQ_ENTRY(cdrom_request) callback_entry;
    cdrom_request_status_t status;
    void *params;
    size_t params_size;
    uint32_t timeout;
    cdrom_request_finalizer_t finalizer;
    void *finalizer_data;
    cdrom_request_continue_t continuation;
    void *continuation_data;
    cdrom_request_executor_t executor;
    cdrom_request_callback_t callback;
    void *callback_data;
    bool cancel_requested;
    bool callback_pending;
    bool callback_running;
    bool no_op;
    bool dma_read;
    bool direct_dma;
    gdrom_direct_sector_type_t direct_sector_type;
    cdrom_request_dma_segment_t dma_segment;
    size_t io_completed_base;
    size_t data_completed_base;
    uint64_t deadline;
    semaphore_t dma_done;
    gdc_cmd_hnd_t bios_handle;
    struct cdrom_stream_session *stream_session;
    struct cdrom_stream_session *stream_owner;
};

struct cdrom_stream_session {
    cdrom_request_t *owner_request;
    cdrom_request_t *transfer_request;
    cdrom_stream_session_status_t status;
    size_t sector_size;
    uint32_t idle_timeout;
    gdrom_direct_sector_type_t direct_sector_type;
    gdrom_direct_result_t direct_result;
    bool closing;
    cdrom_stream_session_finalizer_t finalizer;
    void *finalizer_data;
    semaphore_t work;
};

static STAILQ_HEAD(cdrom_request_queue, cdrom_request) request_queue =
    STAILQ_HEAD_INITIALIZER(request_queue);
static mutex_t request_queue_mutex = MUTEX_INITIALIZER;
static kthread_worker_t *request_worker;
static cdrom_request_t *active_request;
static bool request_shutdown = true;

static STAILQ_HEAD(cdrom_callback_queue, cdrom_request) callback_queue =
    STAILQ_HEAD_INITIALIZER(callback_queue);
static mutex_t callback_queue_mutex = MUTEX_INITIALIZER;
static condvar_t callback_queue_idle = COND_INITIALIZER;
static kthread_worker_t *callback_worker;
static cdrom_request_t *active_callback;

static bool request_is_terminal(cdrom_request_state_t state) {
    return state == CDROM_REQUEST_COMPLETE
        || state == CDROM_REQUEST_CANCELLED
        || state == CDROM_REQUEST_ERROR
        || state == CDROM_REQUEST_TIMED_OUT;
}

static int map_command_result(cd_cmd_chk_t response,
                              const cd_cmd_chk_status_t *detail) {
    return cdrom_status_to_result(response, detail);
}

static bool command_supported(cd_cmd_code_t command) {
    if(command <= 0 || command >= CD_CMD_MAX)
        return false;

    switch(command) {
        case CD_CMD_PIOREAD:
        case CD_CMD_DMAREAD:
        case CD_CMD_DMA_ABORT:
        case CD_CMD_DMAREAD_STREAM:
        case CD_CMD_PIOREAD_STREAM:
        case CD_CMD_DMAREAD_STREAM_EX:
        case CD_CMD_PIOREAD_STREAM_EX:
            return false;
        default:
            return true;
    }
}

static bool executor_command_supported(cd_cmd_code_t command) {
    switch(command) {
        case CD_CMD_DMAREAD:
        case CD_CMD_SEEK:
        case CD_CMD_PLAY_TRACKS:
        case CD_CMD_PLAY_SECTORS:
        case CD_CMD_PAUSE:
        case CD_CMD_RELEASE:
        case CD_CMD_SCAN_CD:
        case CD_CMD_STOP:
        case CD_CMD_GETSCD:
        case CD_CMD_REQ_MODE:
        case CD_CMD_SET_MODE:
        case CD_CMD_INIT:
            return true;
        default:
            return false;
    }
}

static bool cancellation_requested(const cdrom_request_t *request) {
    irq_mask_t irq = irq_disable();
    bool cancelled = request->cancel_requested || request_shutdown;
    irq_restore(irq);
    return cancelled;
}

bool cdrom_request_cancel_requested_internal(
        const cdrom_request_t *request) {
    return request ? cancellation_requested(request) : true;
}

semaphore_t *cdrom_request_event_internal(cdrom_request_t *request) {
    return request ? &request->dma_done : NULL;
}

void cdrom_request_update_direct_progress(
        cdrom_request_t *request, size_t io_completed_bytes) {
    irq_mask_t irq;
    size_t segment_data = 0;

    if(!request)
        return;

    irq = irq_disable();
    if(io_completed_bytes > request->dma_segment.io_bytes)
        io_completed_bytes = request->dma_segment.io_bytes;
    request->status.io_completed_bytes = request->io_completed_base
        + io_completed_bytes;

    if(request->dma_segment.data_direct
            && io_completed_bytes > request->dma_segment.data_offset) {
        segment_data = io_completed_bytes - request->dma_segment.data_offset;
        if(segment_data > request->dma_segment.data_bytes)
            segment_data = request->dma_segment.data_bytes;
    }
    request->status.completed_bytes = request->data_completed_base
        + segment_data;
    request->status.remaining_bytes = request->status.requested_bytes
        - request->status.completed_bytes;
    irq_restore(irq);
}

static void update_request(cdrom_request_t *request, cd_cmd_chk_t response,
                           const cd_cmd_chk_status_t *detail) {
    irq_mask_t irq = irq_disable();
    size_t segment_io;
    size_t segment_data = 0;

    request->status.response = response;
    request->status.detail = *detail;
    cdrom_decode_sense(detail, &request->status.sense);
    if(request->status.io_bytes) {
        segment_io = detail->size > request->dma_segment.io_bytes
            ? request->dma_segment.io_bytes : detail->size;
        request->status.io_completed_bytes =
            request->io_completed_base + segment_io;

        if(request->dma_segment.data_direct
                && segment_io > request->dma_segment.data_offset) {
            segment_data = segment_io - request->dma_segment.data_offset;
            if(segment_data > request->dma_segment.data_bytes)
                segment_data = request->dma_segment.data_bytes;
        }

        request->status.completed_bytes =
            request->data_completed_base + segment_data;
        request->status.remaining_bytes = request->status.requested_bytes
            - request->status.completed_bytes;
    }

    irq_restore(irq);
}

static void update_request_detail(cdrom_request_t *request,
                                  cd_cmd_chk_t response,
                                  const cd_cmd_chk_status_t *detail) {
    cdrom_sense_t sense;
    irq_mask_t irq;

    cdrom_decode_sense(detail, &sense);

    irq = irq_disable();
    request->status.response = response;
    request->status.detail = *detail;
    request->status.sense = sense;
    irq_restore(irq);
}

static void update_status_progress(cdrom_request_status_t *status,
                                   const cd_cmd_chk_status_t *detail,
                                   const cdrom_request_dma_segment_t *segment,
                                   size_t io_base, size_t data_base) {
    size_t segment_io;
    size_t segment_data = 0;

    status->detail = *detail;
    cdrom_decode_sense(detail, &status->sense);
    if(status->io_bytes) {
        segment_io = detail->size > segment->io_bytes
            ? segment->io_bytes : detail->size;
        status->io_completed_bytes = io_base + segment_io;

        if(segment->data_direct && segment_io > segment->data_offset) {
            segment_data = segment_io - segment->data_offset;
            if(segment_data > segment->data_bytes)
                segment_data = segment->data_bytes;
        }

        status->completed_bytes = data_base + segment_data;
        status->remaining_bytes = status->requested_bytes
            - status->completed_bytes;
    }
}

static void set_request_running(cdrom_request_t *request) {
    irq_mask_t irq = irq_disable();

    request->status.state = CDROM_REQUEST_RUNNING;
    request->status.error = EINPROGRESS;

    irq_restore(irq);
}

static bool queue_callback(cdrom_request_t *request) {
    mutex_lock_scoped(&callback_queue_mutex);

    if(!callback_worker)
        return false;

    STAILQ_INSERT_TAIL(&callback_queue, request, callback_entry);
    thd_worker_wakeup(callback_worker);
    return true;
}

static void finish_request(cdrom_request_t *request,
                           cdrom_request_state_t state, int result) {
    cdrom_request_status_t status;
    irq_mask_t irq = irq_disable();

    status = request->status;
    status.state = state;
    status.result = result;
    status.error = cdrom_result_to_errno(result);
    if(result == ERR_OK && status.io_bytes) {
        status.io_completed_bytes = status.io_bytes;
        status.completed_bytes = status.data_bytes;
        status.remaining_bytes = status.requested_bytes - status.data_bytes;
    }
    irq_restore(irq);

    if(state == CDROM_REQUEST_ERROR)
        cdrom_media_monitor_report_result(result, status.backend);

    /* Filesystem drivers may need to make data and descriptor bookkeeping
       coherent before a terminal state becomes visible. This private hook is
       bounded and context-agnostic. It normally runs on the GD request worker,
       but queued cancellation during shutdown runs it on the shutdown caller. */
    if(request->finalizer)
        request->finalizer(request, &status, request->finalizer_data);

    irq = irq_disable();
    request->status = status;
    request->callback_pending = request->callback != NULL;
    request->callback_running = false;
    irq_restore(irq);

    /* Hardware completion is now observable independently of application
       callback latency. */
    genwait_wake_all(request);

    if(request->callback && !queue_callback(request)) {
        /* Defensive teardown fallback: no producer can normally outlive the
           callback worker. Preserve callback and private cleanup semantics if
           that invariant is ever broken, even though this exceptional path
           cannot provide callback-thread isolation. */
        irq = irq_disable();
        request->callback_pending = false;
        request->callback_running = true;
        irq_restore(irq);

        request->callback(request, &status, request->callback_data);

        irq = irq_disable();
        request->callback_running = false;
        irq_restore(irq);
        genwait_wake_all(request);
    }
}

static void callback_worker_routine(void *data) {
    cdrom_request_t *request;
    cdrom_request_status_t status;
    cdrom_request_callback_t callback;
    void *callback_data;
    irq_mask_t irq;

    (void)data;

    for(;;) {
        mutex_lock(&callback_queue_mutex);
        request = STAILQ_FIRST(&callback_queue);

        if(!request) {
            active_callback = NULL;
            cond_broadcast(&callback_queue_idle);
            mutex_unlock(&callback_queue_mutex);
            return;
        }

        STAILQ_REMOVE_HEAD(&callback_queue, callback_entry);
        active_callback = request;

        irq = irq_disable();
        request->callback_pending = false;
        request->callback_running = true;
        status = request->status;
        callback = request->callback;
        callback_data = request->callback_data;
        irq_restore(irq);
        mutex_unlock(&callback_queue_mutex);

        callback(request, &status, callback_data);

        irq = irq_disable();
        request->callback_running = false;
        irq_restore(irq);
        genwait_wake_all(request);

        mutex_lock(&callback_queue_mutex);
        active_callback = NULL;
        if(STAILQ_EMPTY(&callback_queue))
            cond_broadcast(&callback_queue_idle);
        mutex_unlock(&callback_queue_mutex);
    }
}

static void abort_command(gdc_cmd_hnd_t handle) {
    irq_mask_t irq = irq_disable();

    syscall_gdrom_abort_command(handle);

    irq_restore(irq);
}

static void reset_command_server(void) {
    irq_mask_t irq = irq_disable();

    syscall_gdrom_reset();
    syscall_gdrom_init();

    irq_restore(irq);
}

static gdc_cmd_hnd_t submit_to_bios(cdrom_request_t *request) {
    uint64_t deadline = timer_ms_gettime64() + 10;
    gdc_cmd_hnd_t handle;

    do {
        if(cancellation_requested(request))
            return 0;

        handle = syscall_gdrom_send_command(request->status.command,
                                            request->params);

        if(handle > 0 && request->dma_read
                && cdrom_dma_request_begin(
                    handle, &request->dma_done,
                    request->dma_segment.io_bytes, false) < 0) {
            abort_command(handle);
            return 0;
        }

        /* DMA tracking is armed before the first server call. Keep the BIOS
           server non-reentrant with respect to GD DMA and vblank handlers. */
        irq_mask_t irq = irq_disable();
        syscall_gdrom_exec_server();
        irq_restore(irq);

        if(handle > 0)
            return handle;

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    return 0;
}

static cd_cmd_chk_t service_and_check(gdc_cmd_hnd_t handle,
                                      cd_cmd_chk_status_t *detail) {
    irq_mask_t irq = irq_disable();
    cd_cmd_chk_t response;

    syscall_gdrom_exec_server();
    response = syscall_gdrom_check_command(handle, detail);

    irq_restore(irq);
    return response;
}

static bool dma_segment_valid(const cdrom_request_t *request,
                              const cdrom_request_dma_segment_t *segment) {
    if(!segment || !segment->buffer || !segment->io_bytes
            || ((uintptr_t)segment->buffer & 31)
            || !segment->params.num_sec
            || segment->data_offset > segment->io_bytes
            || segment->data_bytes > segment->io_bytes - segment->data_offset)
        return false;

    if(request->io_completed_base > request->status.io_bytes
            || segment->io_bytes
                > request->status.io_bytes - request->io_completed_base)
        return false;

    if(request->data_completed_base > request->status.data_bytes
            || segment->data_bytes
                > request->status.data_bytes - request->data_completed_base)
        return false;

    return true;
}

static void install_dma_segment(cdrom_request_t *request,
                                const cdrom_request_dma_segment_t *segment) {
    int saved_errno = errno;

    request->dma_segment = *segment;
    memcpy(request->params, &segment->params, sizeof(segment->params));
    request->bios_handle = 0;

    /* A completed segment should consume its signal. Drain any redundant
       vblank/IRQ notification before arming the next physical command. */
    while(sem_trywait(&request->dma_done) == 0) {
    }

    errno = saved_errno;
}

static void complete_dma_segment(cdrom_request_t *request) {
    irq_mask_t irq = irq_disable();

    request->io_completed_base += request->dma_segment.io_bytes;
    request->data_completed_base += request->dma_segment.data_bytes;
    request->status.io_completed_bytes = request->io_completed_base;
    request->status.completed_bytes = request->data_completed_base;
    request->status.remaining_bytes = request->status.requested_bytes
        - request->status.completed_bytes;

    irq_restore(irq);
}

static bool requeue_dma_segment(
    cdrom_request_t *request, const cdrom_request_dma_segment_t *segment) {
    irq_mask_t irq;

    if(!dma_segment_valid(request, segment))
        return false;

    mutex_lock(&request_queue_mutex);
    irq = irq_disable();

    if(request_shutdown || request->cancel_requested || !request_worker) {
        irq_restore(irq);
        mutex_unlock(&request_queue_mutex);
        return false;
    }

    install_dma_segment(request, segment);
    request->status.state = CDROM_REQUEST_QUEUED;
    request->status.error = EINPROGRESS;
    request->status.response = CD_CMD_NOT_FOUND;
    request->status.detail = (cd_cmd_chk_status_t) { 0 };
    request->status.sense = (cdrom_sense_t) { 0 };

    irq_restore(irq);

    STAILQ_INSERT_TAIL(&request_queue, request, entry);
    thd_worker_wakeup(request_worker);
    mutex_unlock(&request_queue_mutex);
    return true;
}

static void finish_dma_chain_segment(cdrom_request_t *request) {
    cdrom_request_dma_segment_t completed = request->dma_segment;
    cdrom_request_dma_segment_t next;
    int continue_result = 0;

    if(cancellation_requested(request)) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    if(request->continuation) {
        memset(&next, 0, sizeof(next));
        continue_result = request->continuation(
            request, &completed, &next, request->continuation_data);
    }

    if(continue_result < 0) {
        finish_request(request, CDROM_REQUEST_ERROR, ERR_SYS);
        return;
    }

    complete_dma_segment(request);

    if(cancellation_requested(request)) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    if(continue_result > 0) {
        if(requeue_dma_segment(request, &next))
            return;

        finish_request(request,
                       cancellation_requested(request)
                           ? CDROM_REQUEST_CANCELLED
                           : CDROM_REQUEST_ERROR,
                       cancellation_requested(request)
                           ? ERR_ABORTED : ERR_SYS);
        return;
    }

    if(request->io_completed_base != request->status.io_bytes
            || request->data_completed_base != request->status.data_bytes) {
        finish_request(request, CDROM_REQUEST_ERROR, ERR_SYS);
        return;
    }

    finish_request(request, CDROM_REQUEST_COMPLETE, ERR_OK);
}

static void stream_session_set_state(
    cdrom_stream_session_t *session, cdrom_stream_session_state_t state,
    int result, cd_cmd_chk_t response,
    const cd_cmd_chk_status_t *detail) {
    irq_mask_t irq = irq_disable();

    session->status.state = state;
    session->status.result = result;
    session->status.error = cdrom_result_to_errno(result);
    session->status.response = response;
    if(detail) {
        session->status.detail = *detail;
        cdrom_decode_sense(detail, &session->status.sense);
    }
    else {
        session->status.detail = (cd_cmd_chk_status_t) { 0 };
        session->status.sense = (cdrom_sense_t) { 0 };
    }

    irq_restore(irq);
    genwait_wake_all(session);
}

static void stream_session_finalize(
    cdrom_request_t *request, const cdrom_request_status_t *status,
    void *data) {
    cdrom_stream_session_t *session = data;
    cdrom_stream_session_state_t state;
    cdrom_stream_session_status_t stream_status;
    irq_mask_t irq;

    (void)request;

    switch(status->state) {
        case CDROM_REQUEST_COMPLETE:
            state = CDROM_STREAM_SESSION_COMPLETE;
            break;
        case CDROM_REQUEST_CANCELLED:
            state = CDROM_STREAM_SESSION_CANCELLED;
            break;
        case CDROM_REQUEST_TIMED_OUT:
            state = CDROM_STREAM_SESSION_TIMED_OUT;
            break;
        default:
            state = CDROM_STREAM_SESSION_ERROR;
            break;
    }

    irq = irq_disable();
    stream_status = session->status;
    irq_restore(irq);

    stream_status.state = state;
    stream_status.result = status->result;
    stream_status.error = status->error;
    if(stream_status.backend == CDROM_REQUEST_BACKEND_BIOS) {
        stream_status.response = status->response;
        stream_status.detail = status->detail;
        stream_status.sense = status->sense;
    }

    if(session->finalizer)
        session->finalizer(session, &stream_status, session->finalizer_data);

    irq = irq_disable();
    session->status = stream_status;
    irq_restore(irq);
    genwait_wake_all(session);
}

static bool stop_stream_command(gdc_cmd_hnd_t handle, uint32_t timeout,
                                cd_cmd_chk_status_t *detail) {
    uint64_t deadline = timer_ms_gettime64() + timeout;
    cd_cmd_chk_t response;

    abort_command(handle);

    do {
        response = service_and_check(handle, detail);
        if(response == CD_CMD_NOT_FOUND || response == CD_CMD_COMPLETED)
            return true;
        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    reset_command_server();
    return false;
}

static void cancel_pending_stream_transfer(
    cdrom_stream_session_t *session) {
    cdrom_request_t *transfer;
    irq_mask_t irq = irq_disable();

    transfer = session->transfer_request;
    if(transfer && transfer->status.state == CDROM_REQUEST_QUEUED) {
        session->transfer_request = NULL;
        session->status.transfer_active = false;
        transfer->stream_owner = NULL;
    }
    else {
        transfer = NULL;
    }
    irq_restore(irq);

    if(transfer)
        finish_request(transfer, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
}

static int process_bios_stream_transfer(cdrom_stream_session_t *session,
                                        gdc_cmd_hnd_t handle) {
    cdrom_request_t *transfer;
    cd_cmd_chk_status_t detail = { 0 };
    cd_cmd_chk_t response = CD_CMD_STREAMING;
    cd_transfer_params_t params;
    uint64_t deadline;
    bool cancelled;
    bool timed_out = false;
    int result = ERR_OK;
    irq_mask_t irq;

    irq = irq_disable();
    transfer = session->transfer_request;
    irq_restore(irq);
    if(!transfer)
        return ERR_OK;

    if(cancellation_requested(transfer)) {
        irq = irq_disable();
        session->transfer_request = NULL;
        session->status.transfer_active = false;
        transfer->stream_owner = NULL;
        irq_restore(irq);
        finish_request(transfer, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        genwait_wake_all(session);
        return ERR_OK;
    }

    set_request_running(transfer);
    if(transfer->dma_segment.cacheable)
        dcache_inval_range((uintptr_t)transfer->dma_segment.buffer,
                           transfer->dma_segment.io_bytes);

    if(cdrom_dma_request_begin(handle, &transfer->dma_done,
                               transfer->dma_segment.io_bytes, true) < 0) {
        result = ERR_SYS;
        goto finished;
    }

    params.addr = transfer->dma_segment.params.buffer;
    params.size = transfer->dma_segment.io_bytes;
    irq = irq_disable();
    int transfer_result = syscall_gdrom_dma_transfer(handle, &params);
    irq_restore(irq);
    if(transfer_result < 0) {
        cdrom_dma_request_end(handle);
        result = ERR_SYS;
        goto finished;
    }

    irq = irq_disable();
    transfer->bios_handle = handle;
    irq_restore(irq);

    deadline = transfer->timeout
        ? timer_ms_gettime64() + transfer->timeout : 0;

    for(;;) {
        if(cdrom_dma_request_snapshot(handle, &response, &detail))
            update_request(transfer, response, &detail);

        cancelled = cancellation_requested(transfer)
            || cancellation_requested(session->owner_request);
        if(cancelled || (deadline && timer_ms_gettime64() >= deadline)) {
            timed_out = !cancelled;
            stop_stream_command(handle, 1000, &detail);
            break;
        }

        if(!cdrom_dma_request_active(handle))
            break;

        if(deadline) {
            uint64_t now = timer_ms_gettime64();
            uint32_t remaining = now < deadline
                ? (uint32_t)(deadline - now) : 1;

            sem_wait_timed(&transfer->dma_done, remaining);
        }
        else {
            sem_wait(&transfer->dma_done);
        }
    }

    if(cdrom_dma_request_snapshot(handle, &response, &detail))
        update_request(transfer, response, &detail);
    cdrom_dma_request_end(handle);

    if(transfer->dma_segment.cacheable)
        dcache_inval_range((uintptr_t)transfer->dma_segment.buffer,
                           transfer->dma_segment.io_bytes);

    if(cancelled || timed_out) {
        result = timed_out ? ERR_TIMEOUT : ERR_ABORTED;
    }
    else if(detail.size < transfer->dma_segment.io_bytes) {
        result = map_command_result(response, &detail);
        if(result == ERR_OK)
            result = ERR_SYS;
    }
    else {
        if(transfer->dma_segment.data_bytes
                < transfer->dma_segment.io_bytes) {
            memset((uint8_t *)transfer->dma_segment.buffer
                       + transfer->dma_segment.data_bytes,
                   0, transfer->dma_segment.io_bytes
                       - transfer->dma_segment.data_bytes);
        }
        complete_dma_segment(transfer);
    }

finished:
    irq = irq_disable();
    session->transfer_request = NULL;
    session->status.transfer_active = false;
    transfer->stream_owner = NULL;
    if(result == ERR_OK) {
        session->status.transferred_bytes += transfer->status.io_bytes;
        session->status.completed_bytes += transfer->status.data_bytes;
        session->status.remaining_bytes -= transfer->status.io_bytes;
    }
    irq_restore(irq);

    if(result == ERR_OK)
        finish_request(transfer, CDROM_REQUEST_COMPLETE, ERR_OK);
    else if(result == ERR_ABORTED)
        finish_request(transfer, CDROM_REQUEST_CANCELLED, result);
    else if(result == ERR_TIMEOUT)
        finish_request(transfer, CDROM_REQUEST_TIMED_OUT, result);
    else
        finish_request(transfer, CDROM_REQUEST_ERROR, result);

    /* A transfer is serviced outside the main queue while its owner holds
       the streaming command. Propagate the transfer's terminal BIOS detail
       so the session reports the failure which actually ended it, rather
       than the earlier CD_CMD_STREAMING response. */
    if(result != ERR_OK && result != ERR_ABORTED && result != ERR_TIMEOUT)
        update_request_detail(session->owner_request, response, &detail);

    genwait_wake_all(session);
    return result;
}

static void publish_direct_stream_result(
        cdrom_stream_session_t *session, cdrom_request_t *transfer,
        const gdrom_direct_result_t *transport) {
    irq_mask_t irq;

    if(!transport || !transport->sense_valid)
        return;

    irq = irq_disable();
    session->status.sense = transport->sense;
    if(transfer)
        transfer->status.sense = transport->sense;
    irq_restore(irq);
}

static int process_direct_stream_transfer(
        cdrom_stream_session_t *session, gdrom_direct_stream_t *stream) {
    cdrom_request_t *transfer;
    gdrom_direct_result_t transport = { 0 };
    uint32_t timeout;
    int result = ERR_OK;
    int error = 0;
    irq_mask_t irq;

    irq = irq_disable();
    transfer = session->transfer_request;
    irq_restore(irq);
    if(!transfer)
        return ERR_OK;

    if(cancellation_requested(transfer)) {
        irq = irq_disable();
        session->transfer_request = NULL;
        session->status.transfer_active = false;
        transfer->stream_owner = NULL;
        irq_restore(irq);
        finish_request(transfer, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        genwait_wake_all(session);
        return ERR_OK;
    }

    set_request_running(transfer);
    /* BIOS transfers historically accept zero as unbounded. A direct stream
       cannot safely leave the shared G1 path wedged forever if DMARQ is lost,
       so zero selects a generous bounded transport default instead. */
    timeout = transfer->timeout ? transfer->timeout
                                : DIRECT_STREAM_TRANSFER_TIMEOUT_MS;
    if(gdrom_direct_stream_transfer(
            stream, session->owner_request, transfer,
            transfer->dma_segment.buffer, transfer->dma_segment.io_bytes,
            timeout, &transport) < 0) {
        error = errno;
        result = direct_error_result(error, &transport);
    }
    else {
        if(transfer->dma_segment.data_bytes
                < transfer->dma_segment.io_bytes) {
            memset((uint8_t *)transfer->dma_segment.buffer
                       + transfer->dma_segment.data_bytes,
                   0, transfer->dma_segment.io_bytes
                       - transfer->dma_segment.data_bytes);
        }
        complete_dma_segment(transfer);
    }

    publish_direct_stream_result(session, transfer, &transport);

    irq = irq_disable();
    session->transfer_request = NULL;
    session->status.transfer_active = false;
    transfer->stream_owner = NULL;
    if(result == ERR_OK) {
        session->status.transferred_bytes += transfer->status.io_bytes;
        session->status.completed_bytes += transfer->status.data_bytes;
        session->status.remaining_bytes -= transfer->status.io_bytes;
    }
    irq_restore(irq);

    if(result == ERR_OK)
        finish_request(transfer, CDROM_REQUEST_COMPLETE, ERR_OK);
    else if(result == ERR_ABORTED)
        finish_request(transfer, CDROM_REQUEST_CANCELLED, result);
    else if(result == ERR_TIMEOUT)
        finish_request(transfer, CDROM_REQUEST_TIMED_OUT, result);
    else
        finish_request(transfer, CDROM_REQUEST_ERROR, result);

    genwait_wake_all(session);
    return result;
}

static void process_direct_stream_session(cdrom_request_t *request) {
    cdrom_stream_session_t *session = request->stream_session;
    gdrom_direct_stream_t *stream = NULL;
    int result = ERR_OK;
    int error;

    if(cancellation_requested(request)) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    stream_session_set_state(session, CDROM_STREAM_SESSION_STARTING,
                             ERR_OK, CD_CMD_NOT_FOUND, NULL);
    stream = gdrom_direct_stream_begin(
        request, &session->work, session->status.start_sector,
        session->status.sector_count, session->direct_sector_type,
        request->timeout, &session->direct_result);
    if(!stream) {
        error = errno;
        result = direct_error_result(error, &session->direct_result);
        goto finished;
    }

    stream_session_set_state(session, CDROM_STREAM_SESSION_READY,
                             ERR_OK, CD_CMD_NOT_FOUND, NULL);

    for(;;) {
        if(cancellation_requested(request)) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            cancel_pending_stream_transfer(session);
            result = ERR_ABORTED;
            break;
        }

        if(session->status.remaining_bytes == 0) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            break;
        }

        if(sem_wait_timed(&session->work, session->idle_timeout) < 0) {
            irq_mask_t irq = irq_disable();
            bool cancelled = request->cancel_requested || request_shutdown;

            session->closing = true;
            irq_restore(irq);
            cancel_pending_stream_transfer(session);
            result = cancelled ? ERR_ABORTED : ERR_TIMEOUT;
            break;
        }

        result = process_direct_stream_transfer(session, stream);
        if(result != ERR_OK) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            break;
        }
    }

finished:
    if(stream && gdrom_direct_stream_end(
            stream, &session->direct_result) < 0 && result == ERR_OK) {
        result = direct_error_result(errno, &session->direct_result);
    }
    publish_direct_stream_result(session, NULL, &session->direct_result);

    if(result == ERR_OK)
        finish_request(request, CDROM_REQUEST_COMPLETE, ERR_OK);
    else if(result == ERR_ABORTED)
        finish_request(request, CDROM_REQUEST_CANCELLED, result);
    else if(result == ERR_TIMEOUT)
        finish_request(request, CDROM_REQUEST_TIMED_OUT, result);
    else
        finish_request(request, CDROM_REQUEST_ERROR, result);
}

static void process_stream_session(cdrom_request_t *request) {
    cdrom_stream_session_t *session = request->stream_session;
    cd_cmd_chk_status_t detail = { 0 };
    cd_cmd_chk_t response = CD_CMD_NOT_FOUND;
    gdc_cmd_hnd_t handle = 0;
    uint64_t deadline = request->timeout
        ? timer_ms_gettime64() + request->timeout : 0;
    bool timed_out = false;
    int result = ERR_OK;

    if(session->status.backend == CDROM_REQUEST_BACKEND_DIRECT) {
        process_direct_stream_session(request);
        return;
    }

    if(cancellation_requested(request)) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    stream_session_set_state(session, CDROM_STREAM_SESSION_STARTING,
                             ERR_OK, CD_CMD_NOT_FOUND, NULL);

    if(deadline) {
        uint64_t now = timer_ms_gettime64();
        uint32_t remaining = now < deadline
            ? (uint32_t)(deadline - now) : 1;

        if(g1_bus_lock_timed(remaining) < 0) {
            finish_g1_lock_failure(request);
            return;
        }
    }
    else {
        if(g1_bus_lock() < 0) {
            finish_g1_lock_failure(request);
            return;
        }
    }

    if(cdrom_stream_request_claim() != ERR_OK) {
        g1_bus_unlock();
        finish_request(request, CDROM_REQUEST_ERROR, ERR_SYS);
        return;
    }

    if(!cdrom_stream_sector_size_matches(session->sector_size)) {
        g1_bus_unlock();
        finish_request(request, CDROM_REQUEST_ERROR, ERR_SYS);
        return;
    }

    handle = submit_to_bios(request);
    if(handle <= 0) {
        result = cancellation_requested(request) ? ERR_ABORTED : ERR_SYS;
        goto finished;
    }

    irq_mask_t irq = irq_disable();
    request->bios_handle = handle;
    irq_restore(irq);

    for(;;) {
        uint64_t now = timer_ms_gettime64();

        if(cancellation_requested(request)
                || (deadline && now >= deadline)) {
            timed_out = !cancellation_requested(request);
            stop_stream_command(handle, 1000, &detail);
            result = timed_out ? ERR_TIMEOUT : ERR_ABORTED;
            goto finished;
        }

        response = service_and_check(handle, &detail);
        update_request(request, response, &detail);

        if(response == CD_CMD_STREAMING)
            break;
        if(response == CD_CMD_COMPLETED || response == CD_CMD_NOT_FOUND
                || response == CD_CMD_FAILED) {
            result = map_command_result(response, &detail);
            if(result == ERR_OK)
                result = ERR_SYS;
            goto finished;
        }

        thd_pass();
    }

    stream_session_set_state(session, CDROM_STREAM_SESSION_READY,
                             ERR_OK, response, &detail);

    for(;;) {
        if(cancellation_requested(request)) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            cancel_pending_stream_transfer(session);
            stop_stream_command(handle, 1000, &detail);
            result = ERR_ABORTED;
            break;
        }

        if(session->status.remaining_bytes == 0) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            if(!stop_stream_command(handle, 1000, &detail))
                result = ERR_TIMEOUT;
            break;
        }

        if(sem_wait_timed(&session->work, session->idle_timeout) < 0) {
            irq_mask_t irq = irq_disable();
            bool cancelled = request->cancel_requested || request_shutdown;

            /* Close admission before inspecting/cancelling a transfer which
               may have raced the idle deadline. */
            session->closing = true;
            irq_restore(irq);

            cancel_pending_stream_transfer(session);
            stop_stream_command(handle, 1000, &detail);
            result = cancelled ? ERR_ABORTED : ERR_TIMEOUT;
            break;
        }

        result = process_bios_stream_transfer(session, handle);
        if(result != ERR_OK) {
            irq_mask_t irq = irq_disable();
            session->closing = true;
            irq_restore(irq);
            if(result != ERR_ABORTED && result != ERR_TIMEOUT)
                stop_stream_command(handle, 1000, &detail);
            break;
        }
    }

finished:
    g1_bus_unlock();

    if(result == ERR_OK)
        finish_request(request, CDROM_REQUEST_COMPLETE, ERR_OK);
    else if(result == ERR_ABORTED)
        finish_request(request, CDROM_REQUEST_CANCELLED, result);
    else if(result == ERR_TIMEOUT || timed_out)
        finish_request(request, CDROM_REQUEST_TIMED_OUT, ERR_TIMEOUT);
    else
        finish_request(request, CDROM_REQUEST_ERROR, result);
}

static int direct_error_result(
        int error, const gdrom_direct_result_t *transport) {
    switch(error) {
        case ECANCELED:
            return ERR_ABORTED;
        case ETIMEDOUT:
            return ERR_TIMEOUT;
        case EBUSY:
        case EAGAIN:
            return ERR_BUSY;
        case EIO:
            if(transport && transport->sense_valid)
                return cdrom_sense_to_result(&transport->sense);
            return ERR_SYS;
        default:
            return ERR_SYS;
    }
}

static void process_direct_dma_segment(cdrom_request_t *request) {
    gdrom_direct_result_t transport;
    uint32_t timeout = DIRECT_DMA_COMMAND_TIMEOUT_MS;
    uint64_t now = timer_ms_gettime64();
    int result;
    int error;

    if(request->deadline) {
        if(now >= request->deadline) {
            finish_request(request, CDROM_REQUEST_TIMED_OUT, ERR_TIMEOUT);
            return;
        }
        timeout = (uint32_t)(request->deadline - now);
    }
    else if(request->timeout) {
        request->deadline = now + request->timeout;
        timeout = request->timeout;
    }

    if(gdrom_direct_read_sectors_dma_request(
            request, request->dma_segment.buffer,
            request->dma_segment.params.start_sec,
            request->dma_segment.params.num_sec,
            request->direct_sector_type, timeout, &transport) == 0) {
        finish_dma_chain_segment(request);
        return;
    }

    error = errno;
    if(cancellation_requested(request) || error == ECANCELED) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    result = direct_error_result(error, &transport);
    if(result == ERR_TIMEOUT)
        finish_request(request, CDROM_REQUEST_TIMED_OUT, result);
    else
        finish_request(request, CDROM_REQUEST_ERROR, result);
}

static void process_request(cdrom_request_t *request) {
    cd_cmd_chk_status_t detail = { 0 };
    cd_cmd_chk_t response = CD_CMD_NOT_FOUND;
    gdc_cmd_hnd_t handle;
    uint64_t deadline;
    uint64_t abort_deadline = 0;
    bool abort_sent = false;
    bool timed_out = false;
    int result;

    if(request->stream_session) {
        process_stream_session(request);
        return;
    }

    if(cancellation_requested(request)) {
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    if(request->no_op) {
        finish_request(request, CDROM_REQUEST_COMPLETE, ERR_OK);
        return;
    }

    if(request->executor) {
        result = request->executor(request, request->params);
        if(result == ERR_OK)
            finish_request(request, CDROM_REQUEST_COMPLETE, result);
        else if(result == ERR_ABORTED)
            finish_request(request, CDROM_REQUEST_CANCELLED, result);
        else if(result == ERR_TIMEOUT)
            finish_request(request, CDROM_REQUEST_TIMED_OUT, result);
        else
            finish_request(request, CDROM_REQUEST_ERROR, result);
        return;
    }

    if(request->direct_dma) {
        process_direct_dma_segment(request);
        return;
    }

    deadline = request->deadline;
    if(deadline) {
        uint64_t now = timer_ms_gettime64();
        uint32_t remaining;

        if(now >= deadline) {
            finish_request(request, CDROM_REQUEST_TIMED_OUT, ERR_TIMEOUT);
            return;
        }

        remaining = (uint32_t)(deadline - now);
        if(g1_bus_lock_timed(remaining) < 0) {
            finish_g1_lock_failure(request);
            return;
        }
    }
    else {
        if(g1_bus_lock() < 0) {
            finish_g1_lock_failure(request);
            return;
        }
        if(request->timeout)
            request->deadline = timer_ms_gettime64() + request->timeout;
    }

    deadline = request->deadline;

    if(cancellation_requested(request)) {
        g1_bus_unlock();
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
        return;
    }

    /* Invalidate immediately before DMA while this request owns G1. This
       prevents dirty cache lines from being written back over incoming data. */
    if(request->dma_read && request->dma_segment.cacheable)
        dcache_inval_range((uintptr_t)request->dma_segment.buffer,
                           request->dma_segment.io_bytes);

    handle = submit_to_bios(request);
    if(handle <= 0) {
        bool cancelled = cancellation_requested(request);

        g1_bus_unlock();
        finish_request(request,
                       cancelled ? CDROM_REQUEST_CANCELLED
                                 : CDROM_REQUEST_ERROR,
                       cancelled ? ERR_ABORTED : ERR_SYS);
        return;
    }

    irq_mask_t handle_irq = irq_disable();
    request->bios_handle = handle;
    irq_restore(handle_irq);

    /* One BIOS command has four terminal routes: an IRQ-owned DMA snapshot,
       ordinary command-server completion, bounded abort completion, or a
       forced command-server reset after the abort grace period. The worker
       sleeps only while DMA hardware/IRQ service can make progress; all other
       states yield so the short BIOS service sequence can advance. */
    for(;;) {
        uint64_t now = timer_ms_gettime64();
        bool cancelled = cancellation_requested(request);

        /* The DMA IRQ/vblank path checks the BIOS command before waking us.
           A completed response is consumable: checking the same handle again
           can return CD_CMD_NOT_FOUND. Publish and use the IRQ-owned terminal
           snapshot instead of immediately re-entering the command server. */
        if(request->dma_read && !cdrom_dma_request_active(handle)
                && cdrom_dma_request_snapshot(handle, &response, &detail)) {
            update_request(request, response, &detail);
            if(response == CD_CMD_COMPLETED
                    || response == CD_CMD_NOT_FOUND
                    || response == CD_CMD_FAILED)
                break;
        }

        if(!abort_sent && (cancelled || (deadline && now >= deadline))) {
            abort_command(handle);
            abort_sent = true;
            timed_out = !cancelled;
            abort_deadline = now + 1000;
        }

        response = service_and_check(handle, &detail);
        update_request(request, response, &detail);

        if((response == CD_CMD_COMPLETED || response == CD_CMD_NOT_FOUND
                || response == CD_CMD_FAILED)
                && (!request->dma_read
                    || !cdrom_dma_request_active(handle)))
            break;

        /* Streaming commands are not accepted by this layer. Treat an
           unexpected streaming response as a command failure and stop it. */
        if(response == CD_CMD_STREAMING && !abort_sent) {
            abort_command(handle);
            abort_sent = true;
            abort_deadline = now + 1000;
        }

        if(abort_sent && now >= abort_deadline) {
            reset_command_server();
            response = CD_CMD_FAILED;
            update_request(request, response, &detail);
            break;
        }

        if(request->dma_read && response == CD_CMD_PROCESSING
                && cdrom_dma_request_active(handle)) {
            uint64_t wait_deadline = abort_sent ? abort_deadline : deadline;
            uint32_t wait_timeout = 0;

            if(wait_deadline) {
                now = timer_ms_gettime64();
                if(now >= wait_deadline)
                    continue;

                wait_timeout = (uint32_t)(wait_deadline - now);
            }

            /* The DMA IRQ normally signals this semaphore. Cancellation also
               signals it so the worker can issue an abort promptly. With no
               request timeout, this wait is deliberately indefinite: the
               vblank monitor services the BIOS and wakes us if the DMA IRQ is
               missed or the command reaches an error state. */
            sem_wait_timed(&request->dma_done, wait_timeout);
        }
        else {
            thd_pass();
        }
    }

    if(request->dma_read) {
        cdrom_dma_request_end(handle);

        /* Discard any CPU cache lines filled speculatively during DMA before
           the completed buffer becomes visible to the caller or callback. */
        if(request->dma_segment.cacheable)
            dcache_inval_range((uintptr_t)request->dma_segment.buffer,
                               request->dma_segment.io_bytes);
    }

    g1_bus_unlock();

    if(abort_sent) {
        finish_request(request,
                       timed_out ? CDROM_REQUEST_TIMED_OUT
                                 : CDROM_REQUEST_CANCELLED,
                       timed_out ? ERR_TIMEOUT : ERR_ABORTED);
        return;
    }

    /* Stock blocking DMA treats a consumed completion as successful. Require
       the full physical segment here so a genuinely missing handle cannot be
       mistaken for completed I/O. */
    if(request->dma_read && response == CD_CMD_NOT_FOUND
            && detail.size >= request->dma_segment.io_bytes)
        result = ERR_OK;
    else
        result = map_command_result(response, &detail);

    if(result == ERR_OK && request->dma_read) {
        finish_dma_chain_segment(request);
        return;
    }

    finish_request(request,
                   result == ERR_OK ? CDROM_REQUEST_COMPLETE
                                    : CDROM_REQUEST_ERROR,
                   result);
}

static void request_worker_routine(void *data) {
    cdrom_request_t *request;

    (void)data;

    for(;;) {
        mutex_lock(&request_queue_mutex);
        request = STAILQ_FIRST(&request_queue);

        if(!request) {
            active_request = NULL;
            mutex_unlock(&request_queue_mutex);
            return;
        }

        STAILQ_REMOVE_HEAD(&request_queue, entry);
        active_request = request;
        set_request_running(request);
        mutex_unlock(&request_queue_mutex);

        process_request(request);

        mutex_lock(&request_queue_mutex);
        active_request = NULL;
        mutex_unlock(&request_queue_mutex);
    }
}

static int request_workers_start_locked(void) {
    static const kthread_attr_t request_attrs = {
        .label = "[gdrom-request]",
        .prio = PRIO_DEFAULT - 2,
    };
    static const kthread_attr_t callback_attrs = {
        .label = "[gdrom-callback]",
    };

    if(request_worker)
        return 0;

    callback_worker = thd_worker_create_ex(
        &callback_attrs, callback_worker_routine, NULL);
    if(!callback_worker) {
        errno = ENOMEM;
        return -1;
    }

    request_worker = thd_worker_create_ex(
        &request_attrs, request_worker_routine, NULL);
    if(!request_worker) {
        thd_worker_destroy(callback_worker);
        callback_worker = NULL;
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

int cdrom_request_system_init(void) {
    mutex_lock_scoped(&request_queue_mutex);

    /* Arm submissions without reserving either worker stack. The first valid
       asynchronous request creates the workers while holding the same queue
       lock used for admission, so concurrent first users cannot race. */
    request_shutdown = false;
    return 0;
}

void cdrom_request_system_shutdown(void) {
    struct cdrom_request_queue cancelled =
        STAILQ_HEAD_INITIALIZER(cancelled);
    kthread_worker_t *worker;
    kthread_worker_t *callbacks;
    cdrom_request_t *request;

    mutex_lock(&request_queue_mutex);
    request_shutdown = true;

    while((request = STAILQ_FIRST(&request_queue)) != NULL) {
        STAILQ_REMOVE_HEAD(&request_queue, entry);
        STAILQ_INSERT_TAIL(&cancelled, request, entry);
    }

    if(active_request) {
        irq_mask_t irq = irq_disable();

        active_request->cancel_requested = true;

        if(active_request->dma_read || active_request->executor)
            sem_signal(&active_request->dma_done);
        if(active_request->stream_session) {
            cdrom_request_t *transfer =
                active_request->stream_session->transfer_request;

            if(transfer) {
                transfer->cancel_requested = true;
                sem_signal(&transfer->dma_done);
            }
            sem_signal(&active_request->stream_session->work);
        }
        irq_restore(irq);
    }

    worker = request_worker;
    request_worker = NULL;
    mutex_unlock(&request_queue_mutex);

    while((request = STAILQ_FIRST(&cancelled)) != NULL) {
        STAILQ_REMOVE_HEAD(&cancelled, entry);
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);
    }

    /* thd_worker_destroy() sets the worker's quit flag and joins its thread.
       If process_request() is active, that join cannot return until it has
       released G1, finalized the request, and cleared active_request. */
    if(worker)
        thd_worker_destroy(worker);

    /* All producers are stopped. Let already queued callbacks drain before
       terminating their dispatcher. */
    mutex_lock(&callback_queue_mutex);
    while(active_callback || !STAILQ_EMPTY(&callback_queue))
        cond_wait(&callback_queue_idle, &callback_queue_mutex);
    callbacks = callback_worker;
    callback_worker = NULL;
    mutex_unlock(&callback_queue_mutex);

    if(callbacks)
        thd_worker_destroy(callbacks);
}

static cdrom_request_t *submit_request(cd_cmd_code_t command,
                                       const void *params, size_t params_size,
                                       uint32_t timeout,
                                       cdrom_request_executor_t executor,
                                       cdrom_request_continue_t continuation,
                                       void *continuation_data,
                                       cdrom_request_finalizer_t finalizer,
                                       void *finalizer_data,
                                       cdrom_request_callback_t callback,
                                       void *callback_data, bool no_op,
                                       bool dma_read, bool direct_dma,
                                       gdrom_direct_sector_type_t sector_type,
                                       const cdrom_request_dma_segment_t *segment,
                                       size_t requested_bytes,
                                       size_t data_bytes, size_t io_bytes,
                                       cdrom_stream_session_t *stream_session) {
    cdrom_request_t *request;
    bool staged_stream = stream_session != NULL;
    bool custom = executor != NULL;
    bool event_required = dma_read || custom;

    if((direct_dma
            && (!dma_read || custom || staged_stream || no_op
                || (sector_type != GDROM_DIRECT_SECTOR_MODE1
                    && sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)))
            || (custom
            && (staged_stream || no_op || dma_read
                || !executor_command_supported(command)))
            || (!custom && staged_stream
            && (no_op || dma_read || command != CD_CMD_DMAREAD_STREAM))
            || (!custom && !staged_stream
                && ((no_op && (dma_read
                               || command <= 0 || command >= CD_CMD_MAX))
                    || (!no_op && dma_read && command != CD_CMD_DMAREAD)
                    || (!no_op && !dma_read
                        && !command_supported(command))))) {
        errno = (command > 0 && command < CD_CMD_MAX) ? ENOTSUP : EINVAL;
        return NULL;
    }

    if((params_size && !params) || data_bytes > requested_bytes
            || data_bytes > io_bytes || (dma_read && !segment)) {
        errno = EINVAL;
        return NULL;
    }

    request = calloc(1, sizeof(*request));
    if(!request) {
        errno = ENOMEM;
        return NULL;
    }

    if(event_required && sem_init(&request->dma_done, 0) < 0) {
        free(request);
        return NULL;
    }

    if(params_size) {
        request->params = malloc(params_size);
        if(!request->params) {
            if(event_required)
                sem_destroy(&request->dma_done);
            free(request);
            errno = ENOMEM;
            return NULL;
        }

        memcpy(request->params, params, params_size);
    }

    request->params_size = params_size;
    request->timeout = timeout;
    request->executor = executor;
    request->continuation = continuation;
    request->continuation_data = continuation_data;
    request->finalizer = finalizer;
    request->finalizer_data = finalizer_data;
    request->callback = callback;
    request->callback_data = callback_data;
    request->no_op = no_op;
    request->dma_read = dma_read;
    request->direct_dma = direct_dma;
    request->direct_sector_type = sector_type;
    request->stream_session = stream_session;
    request->status = (cdrom_request_status_t) {
        .command = command,
        .backend = staged_stream ? stream_session->status.backend
            : (custom || direct_dma ? CDROM_REQUEST_BACKEND_DIRECT
                                    : CDROM_REQUEST_BACKEND_BIOS),
        .state = CDROM_REQUEST_QUEUED,
        .result = ERR_OK,
        .error = EINPROGRESS,
        .response = CD_CMD_NOT_FOUND,
        .detail = { 0 },
        .requested_bytes = requested_bytes,
        .data_bytes = data_bytes,
        .completed_bytes = 0,
        .remaining_bytes = requested_bytes,
        .io_bytes = io_bytes,
        .io_completed_bytes = 0,
    };

    if(dma_read) {
        request->dma_segment = *segment;
        if(!dma_segment_valid(request, segment)) {
            free(request->params);
            sem_destroy(&request->dma_done);
            free(request);
            errno = EINVAL;
            return NULL;
        }
    }

    mutex_lock(&request_queue_mutex);

    if(request_shutdown || request_workers_start_locked() < 0) {
        int saved_errno = request_shutdown ? ENODEV : errno;

        mutex_unlock(&request_queue_mutex);
        free(request->params);
        if(event_required)
            sem_destroy(&request->dma_done);
        free(request);
        errno = saved_errno;
        return NULL;
    }

    if(stream_session)
        stream_session->owner_request = request;

    STAILQ_INSERT_TAIL(&request_queue, request, entry);
    thd_worker_wakeup(request_worker);
    mutex_unlock(&request_queue_mutex);

    return request;
}

cdrom_request_t *cdrom_request_submit(cd_cmd_code_t command,
                                      const void *params, size_t params_size,
                                      uint32_t timeout,
                                      cdrom_request_callback_t callback,
                                      void *callback_data) {
    return cdrom_request_submit_internal(command, params, params_size, timeout,
                                         NULL, NULL, callback, callback_data);
}

cdrom_request_t *cdrom_request_submit_internal(
    cd_cmd_code_t command, const void *params, size_t params_size,
    uint32_t timeout, cdrom_request_finalizer_t finalizer,
    void *finalizer_data, cdrom_request_callback_t callback,
    void *callback_data) {
    return submit_request(command, params, params_size, timeout, NULL, NULL,
                          NULL, finalizer, finalizer_data, callback, callback_data,
                          false, false, false, GDROM_DIRECT_SECTOR_MODE1,
                          NULL, 0, 0, 0, NULL);
}

cdrom_request_t *cdrom_request_submit_dma_chain(
    const cdrom_request_dma_segment_t *first, size_t requested_bytes,
    size_t data_bytes, size_t io_bytes, uint32_t timeout,
    cdrom_request_continue_t continuation, void *continuation_data,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data) {
    if(!first) {
        errno = EINVAL;
        return NULL;
    }

    return submit_request(CD_CMD_DMAREAD, &first->params,
                          sizeof(first->params), timeout, NULL,
                          continuation, continuation_data, finalizer,
                          finalizer_data, callback, callback_data, false, true,
                          false, GDROM_DIRECT_SECTOR_MODE1, first,
                          requested_bytes, data_bytes, io_bytes, NULL);
}

cdrom_request_t *cdrom_request_submit_direct_dma_chain(
    const cdrom_request_dma_segment_t *first,
    gdrom_direct_sector_type_t sector_type,
    size_t requested_bytes, size_t data_bytes, size_t io_bytes,
    uint32_t timeout, cdrom_request_continue_t continuation,
    void *continuation_data, cdrom_request_finalizer_t finalizer,
    void *finalizer_data, cdrom_request_callback_t callback,
    void *callback_data) {
    if(!first) {
        errno = EINVAL;
        return NULL;
    }

    return submit_request(
        CD_CMD_DMAREAD, &first->params, sizeof(first->params), timeout,
        NULL, continuation, continuation_data, finalizer, finalizer_data,
        callback, callback_data, false, true, true, sector_type, first,
        requested_bytes, data_bytes, io_bytes, NULL);
}

cdrom_request_t *cdrom_request_submit_noop(
    cd_cmd_code_t command, size_t requested_bytes,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data) {
    return submit_request(command, NULL, 0, 0, NULL, NULL, NULL, finalizer,
                          finalizer_data, callback, callback_data, true, false,
                          false, GDROM_DIRECT_SECTOR_MODE1, NULL,
                          requested_bytes, 0, 0, NULL);
}

cdrom_request_t *cdrom_request_submit_executor(
        cd_cmd_code_t command, const void *params, size_t params_size,
        size_t requested_bytes, size_t data_bytes, size_t io_bytes,
        uint32_t timeout, cdrom_request_executor_t executor,
        cdrom_request_finalizer_t finalizer, void *finalizer_data,
        cdrom_request_callback_t callback, void *callback_data) {
    if(!executor) {
        errno = EINVAL;
        return NULL;
    }

    return submit_request(command, params, params_size, timeout, executor,
                          NULL, NULL, finalizer, finalizer_data,
                          callback, callback_data, false, false, false,
                          GDROM_DIRECT_SECTOR_MODE1, NULL,
                          requested_bytes, data_bytes, io_bytes, NULL);
}

cdrom_stream_session_t *cdrom_stream_session_start_internal(
    uint32_t sector, size_t sector_count, size_t sector_size,
    size_t data_bytes, uint32_t start_timeout, uint32_t idle_timeout,
    cdrom_request_backend_t backend,
    gdrom_direct_sector_type_t direct_sector_type,
    cdrom_stream_session_finalizer_t finalizer, void *finalizer_data) {
    struct {
        int sec;
        int num;
    } params;
    cdrom_stream_session_t *session;
    cdrom_request_t *request;
    size_t total_bytes;

    if(sector > INT_MAX || !sector_count || !sector_size || !idle_timeout
            || (sector_size & 31)
            || sector_count > INT_MAX
            || sector_count > SIZE_MAX / sector_size
            || (uint64_t)sector + sector_count - 1 > UINT32_MAX
            || (backend != CDROM_REQUEST_BACKEND_BIOS
                && backend != CDROM_REQUEST_BACKEND_DIRECT)
            || (backend == CDROM_REQUEST_BACKEND_DIRECT
                && (!start_timeout || sector_size != GDROM_DIRECT_SECTOR_SIZE
                    || sector_count > UINT16_MAX
                    || (direct_sector_type != GDROM_DIRECT_SECTOR_MODE1
                        && direct_sector_type
                            != GDROM_DIRECT_SECTOR_MODE2_FORM1)))) {
        errno = (sector_size && (sector_size & 31)) ? ENOTSUP : EINVAL;
        return NULL;
    }

    total_bytes = sector_count * sector_size;
    if(data_bytes > total_bytes) {
        errno = EINVAL;
        return NULL;
    }
    session = calloc(1, sizeof(*session));
    if(!session) {
        errno = ENOMEM;
        return NULL;
    }

    if(sem_init(&session->work, 0) < 0) {
        free(session);
        return NULL;
    }

    session->sector_size = sector_size;
    session->idle_timeout = idle_timeout;
    session->direct_sector_type = direct_sector_type;
    session->finalizer = finalizer;
    session->finalizer_data = finalizer_data;
    session->status = (cdrom_stream_session_status_t) {
        .backend = backend,
        .state = CDROM_STREAM_SESSION_QUEUED,
        .result = ERR_OK,
        .error = EINPROGRESS,
        .response = CD_CMD_NOT_FOUND,
        .detail = { 0 },
        .start_sector = sector,
        .sector_count = sector_count,
        .total_bytes = total_bytes,
        .data_bytes = data_bytes,
        .transferred_bytes = 0,
        .completed_bytes = 0,
        .remaining_bytes = total_bytes,
        .transfer_active = false,
        .idle_timeout = idle_timeout,
    };

    params.sec = (int)sector;
    params.num = (int)sector_count;
    request = submit_request(
        CD_CMD_DMAREAD_STREAM, &params, sizeof(params), start_timeout,
        NULL, NULL, NULL, stream_session_finalize, session, NULL, NULL,
        false, false, false, GDROM_DIRECT_SECTOR_MODE1, NULL,
        total_bytes, data_bytes, total_bytes, session);
    if(!request) {
        sem_destroy(&session->work);
        free(session);
        return NULL;
    }

    return session;
}

int cdrom_stream_session_get_status(
    const cdrom_stream_session_t *session,
    cdrom_stream_session_status_t *status) {
    irq_mask_t irq;

    if(!session || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    *status = session->status;
    irq_restore(irq);
    return 0;
}

static bool stream_session_is_terminal(cdrom_stream_session_state_t state) {
    return state == CDROM_STREAM_SESSION_COMPLETE
        || state == CDROM_STREAM_SESSION_CANCELLED
        || state == CDROM_STREAM_SESSION_ERROR
        || state == CDROM_STREAM_SESSION_TIMED_OUT;
}

int cdrom_stream_session_wait_ready(
    cdrom_stream_session_t *session, uint32_t timeout,
    cdrom_stream_session_status_t *status) {
    uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    irq_mask_t irq;

    if(!session) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    while(session->status.state != CDROM_STREAM_SESSION_READY
            && !stream_session_is_terminal(session->status.state)) {
        uint32_t remaining = 0;

        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                irq_restore(irq);
                errno = ETIMEDOUT;
                return -1;
            }
            remaining = (uint32_t)(deadline - now);
        }

        if(genwait_wait(session, "GD-ROM stream", remaining) < 0) {
            irq_restore(irq);
            errno = ETIMEDOUT;
            return -1;
        }
    }

    if(stream_session_is_terminal(session->status.state)) {
        uint32_t remaining = 0;

        irq_restore(irq);
        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                errno = ETIMEDOUT;
                return -1;
            }
            remaining = (uint32_t)(deadline - now);
        }
        if(cdrom_request_wait(
                session->owner_request, remaining, NULL) < 0)
            return -1;
        return status
            ? cdrom_stream_session_get_status(session, status) : 0;
    }

    if(status)
        *status = session->status;
    irq_restore(irq);
    return 0;
}

int cdrom_stream_session_wait(
    cdrom_stream_session_t *session, uint32_t timeout,
    cdrom_stream_session_status_t *status) {
    if(!session) {
        errno = EINVAL;
        return -1;
    }

    if(cdrom_request_wait(session->owner_request, timeout, NULL) < 0)
        return -1;

    return status ? cdrom_stream_session_get_status(session, status) : 0;
}

cdrom_request_t *cdrom_stream_session_transfer_async(
    cdrom_stream_session_t *session, void *buffer, size_t bytes,
    uint32_t timeout, cdrom_request_callback_t callback,
    void *callback_data) {
    cdrom_request_t *request;
    uintptr_t address = (uintptr_t)buffer;
    size_t data_bytes;
    irq_mask_t irq;

    if(!session || !buffer || !bytes || (address & 31) || (bytes & 31)) {
        errno = EINVAL;
        return NULL;
    }

    /* A transfer runs inside the stream session which already owns G1, so it
       is deliberately detached from the main request queue and has no BIOS
       parameter block. It still uses the normal request lifecycle. */
    request = calloc(1, sizeof(*request));
    if(!request) {
        errno = ENOMEM;
        return NULL;
    }
    if(sem_init(&request->dma_done, 0) < 0) {
        free(request);
        return NULL;
    }

    request->timeout = timeout;
    request->callback = callback;
    request->callback_data = callback_data;
    request->dma_read = true;
    request->stream_owner = session;
    request->dma_segment = (cdrom_request_dma_segment_t) {
        .params = {
            .start_sec = 0,
            .num_sec = 1,
            .buffer = (void *)(address & MEM_AREA_CACHE_MASK),
            .is_test = 0,
        },
        .buffer = buffer,
        .io_bytes = bytes,
        .data_offset = 0,
        .data_bytes = bytes,
        .cacheable = (address & MEM_AREA_P2_BASE) != MEM_AREA_P2_BASE,
        .data_direct = true,
    };
    request->status = (cdrom_request_status_t) {
        .command = CD_CMD_DMAREAD_STREAM,
        .backend = session->status.backend,
        .state = CDROM_REQUEST_QUEUED,
        .result = ERR_OK,
        .error = EINPROGRESS,
        .response = session->status.backend == CDROM_REQUEST_BACKEND_BIOS
            ? CD_CMD_STREAMING : CD_CMD_NOT_FOUND,
        .detail = { 0 },
        .requested_bytes = bytes,
        .data_bytes = bytes,
        .completed_bytes = 0,
        .remaining_bytes = bytes,
        .io_bytes = bytes,
        .io_completed_bytes = 0,
    };

    irq = irq_disable();
    if(session->status.state != CDROM_STREAM_SESSION_READY
            || session->closing || session->transfer_request
            || session->owner_request->cancel_requested || request_shutdown
            || bytes > session->status.remaining_bytes) {
        bool busy = session->transfer_request != NULL;
        bool usable = session->status.state == CDROM_STREAM_SESSION_READY
            && !session->closing
            && !session->owner_request->cancel_requested
            && !request_shutdown;

        irq_restore(irq);
        sem_destroy(&request->dma_done);
        free(request);
        errno = busy ? EBUSY : (usable ? EINVAL : ENODEV);
        return NULL;
    }

    session->transfer_request = request;
    session->status.transfer_active = true;
    data_bytes = session->status.data_bytes - session->status.completed_bytes;
    if(data_bytes > bytes)
        data_bytes = bytes;
    request->dma_segment.data_bytes = data_bytes;
    request->status.data_bytes = data_bytes;
    irq_restore(irq);

    sem_signal(&session->work);
    return request;
}

int cdrom_stream_session_cancel(cdrom_stream_session_t *session) {
    cdrom_request_t *transfer;
    irq_mask_t irq;

    if(!session || !session->owner_request) {
        errno = EINVAL;
        return -1;
    }

    cdrom_request_cancel(session->owner_request);

    irq = irq_disable();
    transfer = session->transfer_request;
    if(transfer) {
        transfer->cancel_requested = true;
        sem_signal(&transfer->dma_done);
    }
    sem_signal(&session->work);
    irq_restore(irq);
    return 0;
}

int cdrom_stream_session_destroy(cdrom_stream_session_t *session) {
    irq_mask_t irq;
    bool busy;

    if(!session || !session->owner_request) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    busy = !stream_session_is_terminal(session->status.state)
        || session->transfer_request != NULL;
    irq_restore(irq);
    if(busy) {
        errno = EBUSY;
        return -1;
    }

    if(cdrom_request_wait(session->owner_request, 0, NULL) < 0)
        return -1;

    if(cdrom_request_destroy(session->owner_request) < 0)
        return -1;

    sem_destroy(&session->work);
    free(session);
    return 0;
}

int cdrom_request_get_status(const cdrom_request_t *request,
                             cdrom_request_status_t *status) {
    irq_mask_t irq;
    gdc_cmd_hnd_t handle;
    cdrom_request_dma_segment_t segment;
    size_t io_base;
    size_t data_base;
    bool live_dma;

    if(!request || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    *status = request->status;
    handle = request->bios_handle;
    segment = request->dma_segment;
    io_base = request->io_completed_base;
    data_base = request->data_completed_base;
    live_dma = request->dma_read
        && request->status.state == CDROM_REQUEST_RUNNING;
    irq_restore(irq);

    if(live_dma) {
        cd_cmd_chk_t response;
        cd_cmd_chk_status_t detail;

        if(cdrom_dma_request_snapshot(handle, &response, &detail)) {
            status->response = response;
            update_status_progress(status, &detail, &segment,
                                   io_base, data_base);
        }
    }

    return 0;
}

static int wait_for_request(cdrom_request_t *request, uint32_t timeout,
                            bool wait_for_callback,
                            cdrom_request_status_t *status) {
    uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    irq_mask_t irq;
    int wait_result;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    if(wait_for_callback && callback_worker
            && thd_get_current() == thd_worker_get_thread(callback_worker)) {
        errno = EDEADLK;
        return -1;
    }

    irq = irq_disable();

    while(!request_is_terminal(request->status.state)
            || (wait_for_callback
                && (request->callback_pending || request->callback_running))) {
        uint32_t remaining = 0;

        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                irq_restore(irq);
                errno = ETIMEDOUT;
                return -1;
            }

            remaining = (uint32_t)(deadline - now);
        }

        wait_result = genwait_wait(request, "GD-ROM request", remaining);
        if(wait_result < 0) {
            irq_restore(irq);
            errno = ETIMEDOUT;
            return -1;
        }
    }

    if(status)
        *status = request->status;

    irq_restore(irq);
    return 0;
}

int cdrom_request_wait(cdrom_request_t *request, uint32_t timeout,
                       cdrom_request_status_t *status) {
    return wait_for_request(request, timeout, false, status);
}

int cdrom_request_wait_callback(cdrom_request_t *request, uint32_t timeout) {
    return wait_for_request(request, timeout, true, NULL);
}

int cdrom_request_cancel(cdrom_request_t *request) {
    cdrom_request_t *queued_request;
    cdrom_stream_session_t *stream_owner;
    bool queued = false;
    irq_mask_t irq;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    stream_owner = request->stream_owner;
    if(stream_owner) {
        if(!request_is_terminal(request->status.state))
            request->cancel_requested = true;
        sem_signal(&request->dma_done);
        sem_signal(&stream_owner->work);
        irq_restore(irq);
        return 0;
    }
    irq_restore(irq);

    mutex_lock(&request_queue_mutex);
    irq = irq_disable();

    if(request->status.state == CDROM_REQUEST_QUEUED) {
        STAILQ_FOREACH(queued_request, &request_queue, entry) {
            if(queued_request == request) {
                queued = true;
                break;
            }
        }

        request->cancel_requested = true;
    }
    else if(request->status.state == CDROM_REQUEST_RUNNING) {
        request->cancel_requested = true;
        if(request->dma_read || request->executor)
            sem_signal(&request->dma_done);
    }

    irq_restore(irq);

    if(queued)
        STAILQ_REMOVE(&request_queue, request, cdrom_request, entry);

    mutex_unlock(&request_queue_mutex);

    if(queued)
        finish_request(request, CDROM_REQUEST_CANCELLED, ERR_ABORTED);

    return 0;
}

int cdrom_request_destroy(cdrom_request_t *request) {
    irq_mask_t irq;
    bool busy;

    if(!request) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    busy = !request_is_terminal(request->status.state)
        || request->callback_pending || request->callback_running;
    irq_restore(irq);

    if(busy) {
        errno = EBUSY;
        return -1;
    }

    free(request->params);
    if(request->dma_read || request->executor)
        sem_destroy(&request->dma_done);
    free(request);
    return 0;
}
