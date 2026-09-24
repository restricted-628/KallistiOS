/* KallistiOS ##version##

   dc/sound/stream_service.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/sound/stream_service.h
    \brief   Optional automatic sound-stream polling.
    \ingroup audio_streaming

    This adapter polls several checked sound streams from one explicitly
    created KOS thread. Manual \ref snd_stream_poll_ex use remains the default
    and creates no thread or periodic work.

    Stream callbacks can wait for DMA and use ordinary KOS synchronization, so
    this adapter deliberately uses a preemptive thread rather than a shared
    cooperative fiber executor. Blocking one fiber in such an executor would
    also block every unrelated service sharing its carrier thread.

    \author Joseph Black
*/

#ifndef __DC_SOUND_STREAM_SERVICE_H
#define __DC_SOUND_STREAM_SERVICE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stdint.h>

#include <kos/thread.h>

#include <dc/sound/stream.h>

/** \defgroup audio_stream_service Stream polling service
    \brief Optional caller-sized automatic stream polling
    \ingroup audio_streaming
    @{
*/

/** \brief Opaque stream polling service. */
typedef struct snd_stream_service snd_stream_service_t;

/** \brief Stream polling service lifecycle state. */
typedef enum snd_stream_service_state {
    SND_STREAM_SERVICE_RUNNING = 0, /**< Polling registered streams. */
    SND_STREAM_SERVICE_STOPPING,    /**< Rejecting work and leaving the loop. */
    SND_STREAM_SERVICE_STOPPED      /**< Thread has exited. */
} snd_stream_service_state_t;

/** \brief Coherent stream polling service status. */
typedef struct snd_stream_service_status {
    snd_stream_service_state_t state; /**< Current service state. */
    uint32_t poll_interval_ms;        /**< Delay between complete sweeps. */
    uint32_t registered_streams;      /**< Streams owned by this service. */
    snd_stream_hnd_t active_stream;   /**< Stream being polled or invalid. */
    snd_stream_hnd_t last_error_stream; /**< Last stream with a hard error. */
    int last_error;                   /**< Last hard poll errno or zero. */
    uint64_t sweeps;                  /**< Complete handle-set passes. */
    uint64_t polls;                   /**< Poll operations attempted. */
    uint64_t successful_polls;        /**< Polls completing without an error. */
    uint64_t underruns;               /**< Data-starved, silence-padded polls. */
    uint64_t hard_errors;             /**< All other poll failures. */
} snd_stream_service_status_t;

/** \brief Hard-error notification callback.

    Data-starved ENODATA underruns are counted but do not invoke this callback.
    A hard ENODATA failure, such as a missing producer callback, does invoke it.
    The callback runs on the service thread after the stream poll has released
    internal sound locks. It must return promptly. It may query status, but
    must not destroy the service or remove its currently active stream.

    \param service         Service reporting the error.
    \param stream          Stream whose poll failed.
    \param error           errno value returned by the poll.
    \param user_data       Opaque pointer supplied at creation.
*/
typedef void (*snd_stream_service_error_cb_t)(
    snd_stream_service_t *service, snd_stream_hnd_t stream, int error,
    void *user_data);

/** \brief Create and start an optional stream polling service.

    Creation is the only operation that allocates the service and starts its
    thread. The caller must choose a nonzero stack size, accounting for the
    stack needs of its own stream and error callbacks. A caller-provided stack
    remains owned by the caller until destruction succeeds.

    The service copies \p thread_attr before returning. It always creates a
    joinable thread; detached-thread attributes are rejected. If the attribute
    has no label, `[sound-streams]` is used.

    All service functions require thread context. Service lifecycle calls for
    one object must not be issued concurrently, and no call may race successful
    destruction.

    \param poll_interval_ms Nonzero delay between complete polling sweeps.
    \param thread_attr      Thread attributes with a nonzero stack_size.
    \param error_cb         Optional hard-error notification.
    \param user_data        Opaque callback pointer.
    \return                 New running service, or NULL with errno set.
*/
snd_stream_service_t *snd_stream_service_create(
    uint32_t poll_interval_ms, const kthread_attr_t *thread_attr,
    snd_stream_service_error_cb_t error_cb, void *user_data);

/** \brief Register a stream for automatic polling.

    One stream can belong to only one polling service. Once registered, manual
    \ref snd_stream_poll_ex calls return EBUSY. Registration may occur before
    start or while queueing; the service defers polling until playback begins.

    \param service         Running service.
    \param stream          Allocated stream handle.
    \retval 0              On success.
    \retval -1             On error with errno set.
*/
int snd_stream_service_add(snd_stream_service_t *service,
                           snd_stream_hnd_t stream);

/** \brief Remove a stream after any active poll and callback finish.

    A timed-out removal leaves the stream registered and may be retried. Calls
    from the service's own error callback are rejected with EDEADLK.

    \param service         Running service.
    \param stream          Registered stream handle.
    \param timeout_ms      Nonzero wait bound in milliseconds.
    \retval 0              Stream is no longer owned by the service.
    \retval -1             On error with errno set.
*/
int snd_stream_service_remove(snd_stream_service_t *service,
                              snd_stream_hnd_t stream,
                              uint32_t timeout_ms);

/** \brief Read one coherent service status snapshot.

    \param service         Service to inspect.
    \param status          Receives the snapshot.
    \retval 0              On success.
    \retval -1             On invalid input with errno set.
*/
int snd_stream_service_get_status(snd_stream_service_t *service,
                                  snd_stream_service_status_t *status);

/** \brief Stop and destroy a stream polling service.

    Destruction first prevents new registrations, wakes the service, and waits
    for its active poll and optional error callback to return. Registered
    streams are released before the thread exits. If the deadline expires, the
    service remains valid in STOPPING state and this call may be retried.

    \param service         Service to destroy.
    \param timeout_ms      Nonzero wait bound in milliseconds.
    \retval 0              On success; the pointer and any caller stack may
                           then be reclaimed.
    \retval -1             On error; the service remains valid.
*/
int snd_stream_service_destroy(snd_stream_service_t *service,
                               uint32_t timeout_ms);

/** @} */

__END_DECLS

#endif /* __DC_SOUND_STREAM_SERVICE_H */
