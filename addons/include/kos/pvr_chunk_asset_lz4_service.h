/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4_service.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/pvr_chunk_asset_lz4_service.h
    \brief   Optional cooperative service for incremental compact-asset decode.

    This adapter is a separate object in liblz4. The synchronous and manually
    stepped decoders do not reference the fiber runtime, create a thread, or
    reserve a service stack.
*/

#ifndef __KOS_PVR_CHUNK_ASSET_LZ4_SERVICE_H
#define __KOS_PVR_CHUNK_ASSET_LZ4_SERVICE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <kos/fiber_service.h>
#include <kos/pvr_chunk_asset_lz4.h>

#include <stddef.h>

/** \addtogroup pvr_chunk_model
    @{
*/

typedef struct pvr_chunk_asset_lz4_service
    pvr_chunk_asset_lz4_service_t;
typedef struct pvr_chunk_asset_lz4_job pvr_chunk_asset_lz4_job_t;

/** \brief Observable cooperative decode-job state. */
typedef enum pvr_chunk_asset_lz4_job_state {
    PVR_CHUNK_ASSET_LZ4_JOB_CREATED = 0,
    PVR_CHUNK_ASSET_LZ4_JOB_QUEUED = 1,
    PVR_CHUNK_ASSET_LZ4_JOB_RUNNING = 2,
    PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE = 3,
    PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED = 4,
    PVR_CHUNK_ASSET_LZ4_JOB_FAILED = 5
} pvr_chunk_asset_lz4_job_state_t;

/** \brief Coherent status for one cooperative decode job. */
typedef struct pvr_chunk_asset_lz4_job_status {
    pvr_chunk_asset_lz4_job_state_t state;
    size_t source_bytes;
    size_t source_total;
    size_t output_bytes;
    size_t output_total;
    int error;
} pvr_chunk_asset_lz4_job_status_t;

/** \brief Completion callback running on the service fiber. */
typedef void (*pvr_chunk_asset_lz4_job_callback_t)(
    pvr_chunk_asset_lz4_job_t *job, void *data);

/** \brief Add one reusable LZ4 decode service to an unstarted executor.

    The service borrows its fiber stack. Queue storage is allocated once by
    this call; submission, cancellation, stepping, and completion allocate
    nothing. The positive output budget limits bytes published between
    cooperative yields. The upstream decoder may internally process at most
    one frame block while satisfying that budget.

    The returned adapter must outlive the executor. Release it only after
    fiber_service_executor_destroy() has stopped and joined the service.
*/
pvr_chunk_asset_lz4_service_t *pvr_chunk_asset_lz4_service_create(
    fiber_service_executor_t *executor, void *stack, size_t stack_size,
    size_t queue_capacity, size_t output_budget);

/** \brief Admit the service after its shared executor has started.

    This wake-and-handshake prevents a job from being queued before the service
    entry exists to cancel or complete it. The timeout must be positive.

    \retval 0 Service is ready to accept jobs.
    \retval -1 Error, including ENODEV before executor start or ETIMEDOUT.
*/
int pvr_chunk_asset_lz4_service_start(
    pvr_chunk_asset_lz4_service_t *service, uint32_t timeout_ms);

/** \brief Release a stopped adapter and its fixed queue storage.

    Destroying an executor after the start handshake cancels every active or
    queued job and runs its callback before returning. The application must
    then call this function; the generic executor owns no adapter storage.
    EBUSY reports an entry which has started but has not stopped yet.
*/
int pvr_chunk_asset_lz4_service_destroy(
    pvr_chunk_asset_lz4_service_t *service);

/** \brief Allocate an unsubmitted decode job.

    All buffers and dictionary storage are borrowed through terminal callback
    completion. Creating the job allocates its LZ4 Frame context. The callback
    is optional and must not block the shared service executor.
*/
pvr_chunk_asset_lz4_job_t *pvr_chunk_asset_lz4_job_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    pvr_chunk_asset_lz4_job_callback_t callback, void *callback_data);

/** \brief Destroy an unsubmitted or fully finalized job.

    A queued/running job, or a terminal job whose callback is pending or
    running, fails with EBUSY. This function cannot be called from that job's
    callback.
*/
int pvr_chunk_asset_lz4_job_destroy(pvr_chunk_asset_lz4_job_t *job);

/** \brief Submit a created job to the adapter's bounded FIFO.

    This performs no allocation and may be called from interrupt context after
    pvr_chunk_asset_lz4_service_start() succeeds. EAGAIN reports a full queue
    or a service whose start handshake has not completed. Each job may be
    submitted only once.
*/
int pvr_chunk_asset_lz4_service_submit(
    pvr_chunk_asset_lz4_service_t *service,
    pvr_chunk_asset_lz4_job_t *job);

/** \brief Request cancellation of a created, queued, or running job. */
int pvr_chunk_asset_lz4_job_cancel(pvr_chunk_asset_lz4_job_t *job);

/** \brief Copy coherent job state and decode progress. */
int pvr_chunk_asset_lz4_job_get_status(
    const pvr_chunk_asset_lz4_job_t *job,
    pvr_chunk_asset_lz4_job_status_t *status);

/** @} */

__END_DECLS
#endif /* __KOS_PVR_CHUNK_ASSET_LZ4_SERVICE_H */
