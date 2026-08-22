/* KallistiOS ##version##

   vmufs_request.c
   Copyright (C) 2026 Joseph Black

*/

#include <dc/maple.h>
#include <dc/vmufs.h>

#include <kos/cond.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <kos/worker_thread.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "vmufs_internal.h"

struct vmufs_request {
    STAILQ_ENTRY(vmufs_request) entry;
    STAILQ_ENTRY(vmufs_request) callback_entry;
    vmufs_request_status_t status;
    maple_device_t *dev;
    char filename[13];
    char destination[13];
    const void *input;
    int input_size;
    int flags;
    void *output;
    size_t first_block;
    size_t block_count;
    vmufs_file_attributes_t attributes;
    vmufs_format_options_t options;
    vmufs_format_mode_t mode;
    vmufs_request_callback_t callback;
    void *callback_data;
    bool cancel_requested;
    bool callback_queued;
    bool callback_running;
    bool callback_update_pending;
    bool callback_terminal_pending;
    bool callback_terminal_delivered;
};

static STAILQ_HEAD(request_queue_head, vmufs_request) request_queue =
    STAILQ_HEAD_INITIALIZER(request_queue);
static STAILQ_HEAD(callback_queue_head, vmufs_request) callback_queue =
    STAILQ_HEAD_INITIALIZER(callback_queue);
static mutex_t request_mutex = MUTEX_INITIALIZER;
static mutex_t callback_mutex = MUTEX_INITIALIZER;
static condvar_t callback_idle = COND_INITIALIZER;
static kthread_worker_t *request_worker;
static kthread_worker_t *callback_worker;
static vmufs_request_t *active_request;
static vmufs_request_t *active_callback;
static volatile bool shutting_down = true;

static int workers_start_locked(void);

static bool terminal(vmufs_request_state_t state) {
    return state == VMUFS_REQUEST_COMPLETE ||
           state == VMUFS_REQUEST_CANCELLED ||
           state == VMUFS_REQUEST_ERROR;
}

static size_t filename_length(const char *filename, size_t maximum) {
    size_t length = 0;

    while(length < maximum && filename[length])
        ++length;
    return length;
}

static bool observer_cancelled(void *data) {
    vmufs_request_t *request = data;
    irq_mask_t irq = irq_disable();
    bool cancelled = request->cancel_requested || shutting_down;

    irq_restore(irq);
    return cancelled;
}

static vmufs_request_phase_t public_phase(vmufs_transaction_phase_t phase) {
    static const vmufs_request_phase_t phases[] = {
        VMUFS_REQUEST_PHASE_PREPARING,
        VMUFS_REQUEST_PHASE_DATA,
        VMUFS_REQUEST_PHASE_FAT,
        VMUFS_REQUEST_PHASE_DIRECTORY,
        VMUFS_REQUEST_PHASE_CLEANUP,
        VMUFS_REQUEST_PHASE_ERASING,
        VMUFS_REQUEST_PHASE_FINISHED
    };

    return phases[phase];
}

static bool queue_callback_locked(vmufs_request_t *request, bool terminal) {
    bool enqueue = false;
    irq_mask_t irq;

    if(!request->callback || !callback_worker)
        return false;
    irq = irq_disable();
    request->callback_update_pending = true;
    request->callback_terminal_pending |= terminal;
    if(!request->callback_queued && !request->callback_running) {
        request->callback_queued = true;
        enqueue = true;
    }
    irq_restore(irq);

    if(enqueue) {
        STAILQ_INSERT_TAIL(&callback_queue, request, callback_entry);
        thd_worker_wakeup(callback_worker);
    }
    return true;
}

static void queue_progress(vmufs_request_t *request) {
    if(!request->callback)
        return;
    mutex_lock_scoped(&callback_mutex);
    (void)queue_callback_locked(request, false);
}

static void observer_update(void *data, vmufs_transaction_phase_t phase,
                            size_t completed, size_t total,
                            size_t data_completed, size_t data_blocks,
                            bool committed) {
    vmufs_request_t *request = data;
    irq_mask_t irq = irq_disable();

    request->status.phase = public_phase(phase);
    request->status.completed_blocks = completed;
    request->status.total_blocks = total;
    request->status.data_blocks_completed = data_completed;
    request->status.data_blocks = data_blocks;
    request->status.committed = committed;
    irq_restore(irq);
    queue_progress(request);
}

static void finish_request(vmufs_request_t *request,
                           vmufs_request_state_t state,
                           int result, int error) {
    vmufs_request_status_t status;
    bool queued = false;
    irq_mask_t irq;

    if(request->callback)
        mutex_lock(&callback_mutex);
    irq = irq_disable();
    request->status.state = state;
    request->status.result = result;
    request->status.error = error;
    if(state == VMUFS_REQUEST_COMPLETE)
        request->status.phase = VMUFS_REQUEST_PHASE_FINISHED;
    status = request->status;
    irq_restore(irq);
    genwait_wake_all(request);
    if(request->callback) {
        queued = queue_callback_locked(request, true);
        mutex_unlock(&callback_mutex);
    }

    /* Normal shutdown ordering keeps the dispatcher alive until every request
       producer has stopped. Preserve callback completion if that invariant is
       ever changed by running the terminal notification synchronously. */
    if(request->callback && !queued) {
        irq = irq_disable();
        request->callback_running = true;
        irq_restore(irq);
        request->callback(request, &status, request->callback_data);
        irq = irq_disable();
        request->callback_running = false;
        request->callback_terminal_delivered = true;
        irq_restore(irq);
        genwait_wake_all(request);
    }
}

static void process_request(vmufs_request_t *request) {
    const vmufs_transaction_observer_t observer = {
        .cancelled = observer_cancelled,
        .update = observer_update,
        .data = request
    };
    int result;
    int error;

    errno = 0;
    switch(request->status.operation) {
    case VMUFS_REQUEST_WRITE:
        result = vmufs_write_observed(request->dev, request->filename,
                                      request->input, request->input_size,
                                      request->flags, &observer);
        break;
    case VMUFS_REQUEST_DELETE:
        result = vmufs_delete_observed(request->dev, request->filename,
                                       &observer);
        break;
    case VMUFS_REQUEST_FORMAT:
        result = vmufs_format_observed(request->dev, &request->options,
                                       request->mode, &observer);
        break;
    case VMUFS_REQUEST_DEFRAGMENT:
        result = vmufs_defragment_observed(request->dev, &observer);
        break;
    case VMUFS_REQUEST_RENAME:
        result = vmufs_rename_observed(request->dev, request->filename,
                                       request->destination, &observer);
        break;
    case VMUFS_REQUEST_READ:
        result = vmufs_read_blocks_observed(
            request->dev, request->filename, request->first_block,
            request->output, request->block_count, &observer);
        break;
    case VMUFS_REQUEST_SET_ATTRIBUTES:
        result = vmufs_set_file_attributes_observed(
            request->dev, request->filename, &request->attributes,
            &observer);
        break;
    default:
        errno = EINVAL;
        result = -1;
        break;
    }
    error = errno;

    if(result == VMUFS_TRANSACTION_CANCELLED)
        finish_request(request, VMUFS_REQUEST_CANCELLED, -1, ECANCELED);
    else if(result == 0)
        finish_request(request, VMUFS_REQUEST_COMPLETE, 0, 0);
    else
        finish_request(request, VMUFS_REQUEST_ERROR, result,
                       error ? error : EIO);
}

static void request_worker_routine(void *data) {
    (void)data;

    for(;;) {
        vmufs_request_t *request;
        irq_mask_t irq;

        mutex_lock(&request_mutex);
        request = STAILQ_FIRST(&request_queue);
        if(!request) {
            active_request = NULL;
            mutex_unlock(&request_mutex);
            return;
        }
        STAILQ_REMOVE_HEAD(&request_queue, entry);
        active_request = request;
        irq = irq_disable();
        request->status.state = VMUFS_REQUEST_RUNNING;
        request->status.phase = VMUFS_REQUEST_PHASE_PREPARING;
        request->status.error = EINPROGRESS;
        irq_restore(irq);
        mutex_unlock(&request_mutex);

        queue_progress(request);
        process_request(request);

        mutex_lock(&request_mutex);
        active_request = NULL;
        mutex_unlock(&request_mutex);
    }
}

static void callback_worker_routine(void *data) {
    (void)data;

    for(;;) {
        vmufs_request_t *request;
        vmufs_request_status_t status;
        bool terminal_event;
        bool requeue;
        irq_mask_t irq;

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
        irq = irq_disable();
        request->callback_queued = false;
        request->callback_running = true;
        request->callback_update_pending = false;
        terminal_event = request->callback_terminal_pending;
        request->callback_terminal_pending = false;
        status = request->status;
        irq_restore(irq);
        mutex_unlock(&callback_mutex);

        request->callback(request, &status, request->callback_data);

        mutex_lock(&callback_mutex);
        irq = irq_disable();
        request->callback_running = false;
        request->callback_terminal_delivered |= terminal_event;
        requeue = request->callback_update_pending ||
                  request->callback_terminal_pending;
        request->callback_queued = requeue;
        irq_restore(irq);
        if(requeue)
            STAILQ_INSERT_TAIL(&callback_queue, request, callback_entry);
        active_callback = NULL;
        if(STAILQ_EMPTY(&callback_queue))
            cond_broadcast(&callback_idle);
        mutex_unlock(&callback_mutex);
        genwait_wake_all(request);
    }
}

static vmufs_request_t *submit_request(
    vmufs_request_operation_t operation, maple_device_t *dev,
    const char *filename, const char *destination,
    const void *input, size_t input_size, int flags,
    void *output, size_t first_block, size_t block_count,
    const vmufs_file_attributes_t *attributes,
    const vmufs_format_options_t *options, vmufs_format_mode_t mode,
    vmufs_request_callback_t callback, void *callback_data) {
    vmufs_request_t *request;
    size_t filename_size = 0;
    size_t destination_size = 0;

    if(!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD) ||
       operation > VMUFS_REQUEST_SET_ATTRIBUTES ||
       ((operation == VMUFS_REQUEST_WRITE ||
         operation == VMUFS_REQUEST_DELETE ||
         operation == VMUFS_REQUEST_RENAME ||
         operation == VMUFS_REQUEST_READ ||
         operation == VMUFS_REQUEST_SET_ATTRIBUTES) && !filename) ||
       (operation == VMUFS_REQUEST_RENAME && !destination) ||
       (operation == VMUFS_REQUEST_READ && block_count && !output) ||
       (operation == VMUFS_REQUEST_SET_ATTRIBUTES && !attributes) ||
       (operation == VMUFS_REQUEST_WRITE &&
        ((input_size && !input) || input_size > INT_MAX ||
         (flags & ~(VMUFS_OVERWRITE | VMUFS_VMUGAME | VMUFS_NOCOPY)))) ||
       (operation == VMUFS_REQUEST_FORMAT &&
        (!options || (mode != VMUFS_FORMAT_QUICK &&
                      mode != VMUFS_FORMAT_FULL)))) {
        errno = EINVAL;
        return NULL;
    }
    if(filename) {
        filename_size = filename_length(filename, 13u);
        if(filename_size == 0 || filename_size > 12u) {
            errno = EINVAL;
            return NULL;
        }
    }
    if(destination) {
        destination_size = filename_length(destination, 13u);
        if(destination_size == 0 || destination_size > 12u) {
            errno = EINVAL;
            return NULL;
        }
    }

    request = calloc(1, sizeof(*request));
    if(!request) {
        errno = ENOMEM;
        return NULL;
    }
    request->dev = dev;
    if(filename)
        memcpy(request->filename, filename, filename_size + 1u);
    if(destination) {
        memcpy(request->destination, destination,
               destination_size + 1u);
    }
    request->input = input;
    request->input_size = (int)input_size;
    request->flags = flags;
    request->output = output;
    request->first_block = first_block;
    request->block_count = block_count;
    if(attributes)
        request->attributes = *attributes;
    if(options)
        request->options = *options;
    request->mode = mode;
    request->callback = callback;
    request->callback_data = callback_data;
    request->callback_terminal_delivered = callback == NULL;
    request->status.operation = operation;
    request->status.state = VMUFS_REQUEST_QUEUED;
    request->status.phase = VMUFS_REQUEST_PHASE_QUEUED;

    mutex_lock(&request_mutex);
    if(shutting_down || workers_start_locked() < 0) {
        int saved_errno = shutting_down ? ENODEV : errno;

        mutex_unlock(&request_mutex);
        free(request);
        errno = saved_errno;
        return NULL;
    }
    STAILQ_INSERT_TAIL(&request_queue, request, entry);
    thd_worker_wakeup(request_worker);
    mutex_unlock(&request_mutex);
    return request;
}

vmufs_request_t *vmufs_read_blocks_async(
    maple_device_t *dev, const char *fn, size_t first_block,
    void *outbuf, size_t block_count,
    vmufs_request_callback_t callback, void *callback_data) {
    return submit_request(VMUFS_REQUEST_READ, dev, fn, NULL, NULL, 0, 0,
                          outbuf, first_block, block_count, NULL, NULL,
                          VMUFS_FORMAT_QUICK, callback, callback_data);
}

vmufs_request_t *vmufs_write_async(
    maple_device_t *dev, const char *fn, const void *inbuf, size_t insize,
    int flags, vmufs_request_callback_t callback, void *callback_data) {
    return submit_request(VMUFS_REQUEST_WRITE, dev, fn, NULL, inbuf, insize,
                          flags, NULL, 0, 0, NULL, NULL, VMUFS_FORMAT_QUICK,
                          callback, callback_data);
}

vmufs_request_t *vmufs_delete_async(
    maple_device_t *dev, const char *fn,
    vmufs_request_callback_t callback, void *callback_data) {
    return submit_request(VMUFS_REQUEST_DELETE, dev, fn, NULL, NULL, 0, 0,
                          NULL, 0, 0, NULL, NULL, VMUFS_FORMAT_QUICK, callback,
                          callback_data);
}

vmufs_request_t *vmufs_rename_async(
    maple_device_t *dev, const char *old_name, const char *new_name,
    vmufs_request_callback_t callback, void *callback_data) {
    return submit_request(VMUFS_REQUEST_RENAME, dev, old_name, new_name,
                          NULL, 0, 0, NULL, 0, 0, NULL, NULL,
                          VMUFS_FORMAT_QUICK, callback, callback_data);
}

vmufs_request_t *vmufs_set_file_attributes_async(
    maple_device_t *dev, const char *fn,
    const vmufs_file_attributes_t *attributes,
    vmufs_request_callback_t callback, void *callback_data) {
    return submit_request(VMUFS_REQUEST_SET_ATTRIBUTES, dev, fn, NULL,
                          NULL, 0, 0, NULL, 0, 0, attributes, NULL,
                          VMUFS_FORMAT_QUICK, callback, callback_data);
}

vmufs_request_t *vmufs_format_async(
    maple_device_t *dev, const vmufs_format_options_t *options,
    vmufs_format_mode_t mode, vmufs_request_callback_t callback,
    void *callback_data) {
    vmu_root_t root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];

    if(vmufs_format_build(options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) < 0)
        return NULL;
    return submit_request(VMUFS_REQUEST_FORMAT, dev, NULL, NULL, NULL, 0, 0,
                          NULL, 0, 0, NULL, options, mode, callback,
                          callback_data);
}

vmufs_request_t *vmufs_defragment_async(
    maple_device_t *dev, vmufs_request_callback_t callback,
    void *callback_data) {
    return submit_request(VMUFS_REQUEST_DEFRAGMENT, dev, NULL, NULL, NULL,
                          0, 0, NULL, 0, 0, NULL, NULL, VMUFS_FORMAT_QUICK,
                          callback, callback_data);
}

int vmufs_request_get_status(const vmufs_request_t *request,
                             vmufs_request_status_t *status) {
    irq_mask_t irq;

    if(!request || !status) {
        errno = EINVAL;
        return -1;
    }
    irq = irq_disable();
    *status = request->status;
    irq_restore(irq);
    return 0;
}

static int wait_request(vmufs_request_t *request, uint32_t timeout,
                        bool callback, vmufs_request_status_t *status) {
    uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    irq_mask_t irq;

    if(!request) {
        errno = EINVAL;
        return -1;
    }
    if(callback && callback_worker &&
       thd_get_current() == thd_worker_get_thread(callback_worker)) {
        errno = EDEADLK;
        return -1;
    }

    irq = irq_disable();
    for(;;) {
        bool done = terminal(request->status.state);
        bool callback_done = !callback || !request->callback ||
                             request->callback_terminal_delivered;
        uint32_t remaining = 0;

        if(done && callback_done)
            break;
        if(deadline) {
            uint64_t now = timer_ms_gettime64();

            if(now >= deadline) {
                irq_restore(irq);
                errno = ETIMEDOUT;
                return -1;
            }
            remaining = (uint32_t)(deadline - now);
        }
        if(genwait_wait(request, "VMU request", remaining) < 0) {
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

int vmufs_request_wait(vmufs_request_t *request, uint32_t timeout,
                       vmufs_request_status_t *status) {
    return wait_request(request, timeout, false, status);
}

int vmufs_request_wait_callback(vmufs_request_t *request, uint32_t timeout) {
    return wait_request(request, timeout, true, NULL);
}

int vmufs_request_cancel(vmufs_request_t *request) {
    vmufs_request_t *item;
    bool queued = false;
    irq_mask_t irq;

    if(!request) {
        errno = EINVAL;
        return -1;
    }
    mutex_lock(&request_mutex);
    irq = irq_disable();
    if(request->status.state == VMUFS_REQUEST_QUEUED) {
        STAILQ_FOREACH(item, &request_queue, entry) {
            if(item == request) {
                queued = true;
                break;
            }
        }
        request->cancel_requested = true;
    }
    else if(request->status.state == VMUFS_REQUEST_RUNNING) {
        request->cancel_requested = true;
    }
    irq_restore(irq);
    if(queued)
        STAILQ_REMOVE(&request_queue, request, vmufs_request, entry);
    mutex_unlock(&request_mutex);
    if(queued)
        finish_request(request, VMUFS_REQUEST_CANCELLED, -1, ECANCELED);
    return 0;
}

int vmufs_request_destroy(vmufs_request_t *request) {
    irq_mask_t irq;
    bool busy;

    if(!request) {
        errno = EINVAL;
        return -1;
    }
    irq = irq_disable();
    busy = !terminal(request->status.state) || request->callback_queued ||
           request->callback_running || request->callback_update_pending ||
           request->callback_terminal_pending ||
           (request->callback && !request->callback_terminal_delivered);
    irq_restore(irq);
    if(busy) {
        errno = EBUSY;
        return -1;
    }
    free(request);
    return 0;
}

static int workers_start_locked(void) {
    static const kthread_attr_t request_attrs = {
        .label = "[vmufs-request]"
    };
    static const kthread_attr_t callback_attrs = {
        .label = "[vmufs-callback]"
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

int vmufs_request_system_init(void) {
    mutex_lock_scoped(&request_mutex);
    shutting_down = false;
    return 0;
}

void vmufs_request_system_shutdown(void) {
    struct request_queue_head cancelled = STAILQ_HEAD_INITIALIZER(cancelled);
    kthread_worker_t *worker;
    kthread_worker_t *callbacks;
    vmufs_request_t *request;

    mutex_lock(&request_mutex);
    shutting_down = true;
    while((request = STAILQ_FIRST(&request_queue)) != NULL) {
        STAILQ_REMOVE_HEAD(&request_queue, entry);
        STAILQ_INSERT_TAIL(&cancelled, request, entry);
    }
    if(active_request) {
        irq_mask_t irq = irq_disable();
        active_request->cancel_requested = true;
        irq_restore(irq);
    }
    worker = request_worker;
    request_worker = NULL;
    mutex_unlock(&request_mutex);

    while((request = STAILQ_FIRST(&cancelled)) != NULL) {
        STAILQ_REMOVE_HEAD(&cancelled, entry);
        finish_request(request, VMUFS_REQUEST_CANCELLED, -1, ECANCELED);
    }
    if(worker)
        thd_worker_destroy(worker);

    mutex_lock(&callback_mutex);
    while(active_callback || !STAILQ_EMPTY(&callback_queue))
        cond_wait(&callback_idle, &callback_mutex);
    callbacks = callback_worker;
    callback_worker = NULL;
    mutex_unlock(&callback_mutex);
    if(callbacks)
        thd_worker_destroy(callbacks);
}
