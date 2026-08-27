/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4_service.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/pvr_chunk_asset_lz4_service.h>

#include <arch/irq.h>
#include <kos/fiber_service.h>
#include <kos/timer.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct pvr_chunk_asset_lz4_job {
    pvr_chunk_asset_lz4_state_t *decoder;
    pvr_chunk_asset_lz4_service_t *service;
    pvr_chunk_asset_lz4_job_callback_t callback;
    void *callback_data;
    volatile pvr_chunk_asset_lz4_job_status_t status;
    volatile bool cancel_requested;
    volatile bool callback_pending;
    volatile bool callback_running;
};

struct pvr_chunk_asset_lz4_service {
    fiber_service_executor_t *executor;
    fiber_service_t *fiber_service;
    pvr_chunk_asset_lz4_job_t **queue;
    size_t queue_capacity;
    volatile size_t queue_head;
    volatile size_t queue_count;
    size_t output_budget;
    pvr_chunk_asset_lz4_job_t *active;
    volatile bool entry_started;
    volatile bool entry_stopped;
    volatile bool stopping;
};

static bool job_terminal(pvr_chunk_asset_lz4_job_state_t state) {
    return state == PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE ||
           state == PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED ||
           state == PVR_CHUNK_ASSET_LZ4_JOB_FAILED;
}

static void copy_progress_locked(pvr_chunk_asset_lz4_job_t *job,
    const pvr_chunk_asset_lz4_progress_t *progress) {
    job->status.source_bytes = progress->source_bytes;
    job->status.source_total = progress->source_total;
    job->status.output_bytes = progress->output_bytes;
    job->status.output_total = progress->output_total;
}

static void finish_job(pvr_chunk_asset_lz4_job_t *job,
                       pvr_chunk_asset_lz4_job_state_t state, int error) {
    pvr_chunk_asset_lz4_job_callback_t callback = job->callback;
    void *callback_data = job->callback_data;
    pvr_chunk_asset_lz4_state_t *decoder = job->decoder;
    irq_mask_t irq;

    job->decoder = NULL;
    pvr_chunk_asset_lz4_state_destroy(decoder);

    irq = irq_disable();
    job->service = NULL;
    job->status.state = state;
    job->status.error = error;
    job->callback_pending = callback != NULL;
    irq_restore(irq);

    if(callback) {
        irq = irq_disable();
        job->callback_pending = false;
        job->callback_running = true;
        irq_restore(irq);
        callback(job, callback_data);
        irq = irq_disable();
        job->callback_running = false;
        irq_restore(irq);
    }
}

static pvr_chunk_asset_lz4_job_t *pop_job(
    pvr_chunk_asset_lz4_service_t *service) {
    pvr_chunk_asset_lz4_job_t *job = NULL;
    irq_mask_t irq = irq_disable();

    if(service->queue_count) {
        job = service->queue[service->queue_head];
        service->queue[service->queue_head] = NULL;
        if(++service->queue_head == service->queue_capacity)
            service->queue_head = 0;
        --service->queue_count;
    }
    irq_restore(irq);
    return job;
}

static void cancel_all_jobs(pvr_chunk_asset_lz4_service_t *service) {
    pvr_chunk_asset_lz4_job_t *job;

    if(service->active) {
        job = service->active;
        service->active = NULL;
        finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED, ECANCELED);
    }
    while((job = pop_job(service)))
        finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED, ECANCELED);
}

static void lz4_service_entry(fiber_service_t *fiber_service, void *data) {
    pvr_chunk_asset_lz4_service_t *service = data;

    service->entry_started = true;
    for(;;) {
        pvr_chunk_asset_lz4_job_t *job;
        pvr_chunk_asset_lz4_progress_t progress;
        irq_mask_t irq;
        int result;
        int saved_errno;

        if(fiber_service_stop_requested(fiber_service))
            break;
        if(!service->active)
            service->active = pop_job(service);
        job = service->active;
        if(!job) {
            if(fiber_service_wait(fiber_service, 0) < 0)
                break;
            continue;
        }

        irq = irq_disable();
        if(job->cancel_requested) {
            irq_restore(irq);
            service->active = NULL;
            finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED, ECANCELED);
            continue;
        }
        job->status.state = PVR_CHUNK_ASSET_LZ4_JOB_RUNNING;
        irq_restore(irq);

        result = pvr_chunk_asset_lz4_state_step(job->decoder,
                                                service->output_budget);
        saved_errno = errno;
        if(pvr_chunk_asset_lz4_state_get_progress(job->decoder,
                                                  &progress) < 0) {
            result = -1;
            saved_errno = errno;
        }
        else {
            irq = irq_disable();
            copy_progress_locked(job, &progress);
            irq_restore(irq);
        }

        if(result == PVR_CHUNK_ASSET_LZ4_COMPLETE) {
            service->active = NULL;
            finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE, 0);
        }
        else if(result < 0) {
            service->active = NULL;
            finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_FAILED, saved_errno);
        }
        else if(fiber_service_yield(fiber_service) < 0) {
            service->active = NULL;
            finish_job(job, PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED, ECANCELED);
            break;
        }
    }

    service->stopping = true;
    cancel_all_jobs(service);
    service->entry_stopped = true;
}

pvr_chunk_asset_lz4_service_t *pvr_chunk_asset_lz4_service_create(
    fiber_service_executor_t *executor, void *stack, size_t stack_size,
    size_t queue_capacity, size_t output_budget) {
    pvr_chunk_asset_lz4_service_t *service;

    if(!executor || !stack || !stack_size || !queue_capacity ||
       !output_budget || queue_capacity > SIZE_MAX / sizeof(*service->queue)) {
        errno = EINVAL;
        return NULL;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    service = calloc(1, sizeof(*service));
    if(!service) {
        errno = ENOMEM;
        return NULL;
    }
    service->queue = calloc(queue_capacity, sizeof(*service->queue));
    if(!service->queue) {
        free(service);
        errno = ENOMEM;
        return NULL;
    }
    service->queue_capacity = queue_capacity;
    service->output_budget = output_budget;
    service->executor = executor;
    service->fiber_service = fiber_service_add(executor, stack, stack_size,
                                               lz4_service_entry, service);
    if(!service->fiber_service) {
        free(service->queue);
        free(service);
        return NULL;
    }
    return service;
}

int pvr_chunk_asset_lz4_service_start(
    pvr_chunk_asset_lz4_service_t *service, uint32_t timeout_ms) {
    uint64_t deadline;

    if(!service || !timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(!fiber_service_executor_get_thread(service->executor)) {
        errno = ENODEV;
        return -1;
    }
    if(service->entry_stopped || service->stopping) {
        errno = ECANCELED;
        return -1;
    }
    if(service->entry_started)
        return 0;

    if(fiber_service_wake(service->fiber_service) < 0)
        return -1;
    deadline = timer_ms_gettime64() + timeout_ms;
    while(!service->entry_started) {
        if(service->entry_stopped || service->stopping) {
            errno = ECANCELED;
            return -1;
        }
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        thd_pass();
    }
    return 0;
}

int pvr_chunk_asset_lz4_service_destroy(
    pvr_chunk_asset_lz4_service_t *service) {
    if(!service) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    if(service->entry_started && !service->entry_stopped) {
        errno = EBUSY;
        return -1;
    }

    free(service->queue);
    free(service);
    return 0;
}

pvr_chunk_asset_lz4_job_t *pvr_chunk_asset_lz4_job_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    pvr_chunk_asset_lz4_job_callback_t callback, void *callback_data) {
    pvr_chunk_asset_lz4_job_t *job;
    pvr_chunk_asset_lz4_progress_t progress;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }
    job = calloc(1, sizeof(*job));
    if(!job) {
        errno = ENOMEM;
        return NULL;
    }
    job->decoder = pvr_chunk_asset_lz4_state_create(
        section, destination, destination_bytes, dictionary);
    if(!job->decoder) {
        free(job);
        return NULL;
    }
    if(pvr_chunk_asset_lz4_state_get_progress(job->decoder, &progress) < 0) {
        pvr_chunk_asset_lz4_state_destroy(job->decoder);
        free(job);
        return NULL;
    }
    job->status.state = PVR_CHUNK_ASSET_LZ4_JOB_CREATED;
    copy_progress_locked(job, &progress);
    job->callback = callback;
    job->callback_data = callback_data;
    return job;
}

int pvr_chunk_asset_lz4_job_destroy(pvr_chunk_asset_lz4_job_t *job) {
    pvr_chunk_asset_lz4_state_t *decoder;
    irq_mask_t irq;

    if(!job) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq = irq_disable();
    if((job->status.state != PVR_CHUNK_ASSET_LZ4_JOB_CREATED &&
        !job_terminal(job->status.state)) || job->callback_pending ||
       job->callback_running) {
        irq_restore(irq);
        errno = EBUSY;
        return -1;
    }
    decoder = job->decoder;
    job->decoder = NULL;
    irq_restore(irq);

    pvr_chunk_asset_lz4_state_destroy(decoder);
    free(job);
    return 0;
}

int pvr_chunk_asset_lz4_service_submit(
    pvr_chunk_asset_lz4_service_t *service,
    pvr_chunk_asset_lz4_job_t *job) {
    size_t tail;
    irq_mask_t irq;

    if(!service || !job || !service->fiber_service) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    if(service->stopping || service->entry_stopped) {
        irq_restore(irq);
        errno = ECANCELED;
        return -1;
    }
    if(!service->entry_started) {
        irq_restore(irq);
        errno = EAGAIN;
        return -1;
    }
    if(job->status.state != PVR_CHUNK_ASSET_LZ4_JOB_CREATED || job->service) {
        irq_restore(irq);
        errno = EBUSY;
        return -1;
    }
    if(service->queue_count == service->queue_capacity) {
        irq_restore(irq);
        errno = EAGAIN;
        return -1;
    }

    tail = service->queue_head + service->queue_count;
    if(tail >= service->queue_capacity)
        tail -= service->queue_capacity;
    service->queue[tail] = job;
    ++service->queue_count;
    job->service = service;
    job->status.state = PVR_CHUNK_ASSET_LZ4_JOB_QUEUED;
    irq_restore(irq);

    /* Submission is already owned by the service if a concurrent executor
       stop makes this wake report ECANCELED; shutdown still finalizes it. */
    (void)fiber_service_wake(service->fiber_service);
    return 0;
}

int pvr_chunk_asset_lz4_job_cancel(pvr_chunk_asset_lz4_job_t *job) {
    pvr_chunk_asset_lz4_state_t *decoder = NULL;
    pvr_chunk_asset_lz4_service_t *service = NULL;
    irq_mask_t irq;

    if(!job) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    irq = irq_disable();
    if(job_terminal(job->status.state)) {
        irq_restore(irq);
        errno = EALREADY;
        return -1;
    }
    if(job->status.state == PVR_CHUNK_ASSET_LZ4_JOB_CREATED) {
        decoder = job->decoder;
        job->decoder = NULL;
        job->status.state = PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED;
        job->status.error = ECANCELED;
    }
    else {
        job->cancel_requested = true;
        service = job->service;
    }
    irq_restore(irq);

    pvr_chunk_asset_lz4_state_destroy(decoder);
    if(service)
        (void)fiber_service_wake(service->fiber_service);
    return 0;
}

int pvr_chunk_asset_lz4_job_get_status(
    const pvr_chunk_asset_lz4_job_t *job,
    pvr_chunk_asset_lz4_job_status_t *status) {
    irq_mask_t irq;

    if(status)
        memset(status, 0, sizeof(*status));
    if(!job || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    *status = job->status;
    irq_restore(irq);
    return 0;
}
