/* KallistiOS ##version##

   kos/timer_event.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/timer_event.h
    \brief   Shared thread-context software timers.
    \ingroup kthreads

    Timer events provide one-shot and periodic monotonic deadlines on one
    lazily-created KOS workqueue. They are intended for bounded callbacks which
    do not require the per-callback thread isolation of `oneshot_timer_t`.

    \author Joseph Black

    \see kos/timer.h
    \see kos/oneshot_timer.h
    \see kos/workqueue.h
*/

#ifndef __KOS_TIMER_EVENT_H
#define __KOS_TIMER_EVENT_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stdint.h>

/** \defgroup timer_events Shared software timers
    \brief Lazy one-shot and periodic thread-context timers
    \ingroup kthreads

    @{
*/

struct timer_event;

/** \brief Opaque shared software timer object. */
typedef struct timer_event timer_event_t;

/** \brief Timer event callback.

    The callback runs in ordinary thread context on a dispatcher shared by all
    timer events. Callbacks are serialized and must remain bounded; a callback
    which blocks delays other timer events, but not the scheduler or interrupt
    handling.

    For a periodic timer, return true to schedule the next period or false to
    stop. The return value is ignored for a one-shot timer. A callback may arm
    or cancel its own timer. It must not destroy its own timer.

    \param timer          Timer which expired.
    \param data           User data supplied at creation.
*/
typedef bool (*timer_event_callback_t)(timer_event_t *timer, void *data);

/** \brief Coherent state snapshot for a timer event. */
typedef struct timer_event_info {
    bool armed;              /**< Waiting for its next deadline. */
    bool running;            /**< Callback dispatch is in progress. */
    bool cancelling;         /**< Cancellation is draining dispatch. */
    uint64_t deadline_ms;    /**< Next absolute monotonic deadline. */
    uint64_t interval_ms;    /**< Period, or zero for one-shot mode. */
    uint64_t expirations;    /**< Number of callbacks actually started. */
    uint64_t overruns;       /**< Periods coalesced after late dispatch. */
} timer_event_info_t;

/** \brief Create a stopped timer event.

    Creating an object allocates only its control structure. The shared
    dispatcher thread is created on the first successful arm and is released
    after the last timer which used it is destroyed.

    \param callback       Bounded thread-context callback.
    \param data           User data passed to the callback.

    \return               New timer on success, or NULL with errno set.
*/
timer_event_t *timer_event_create(timer_event_callback_t callback, void *data);

/** \brief Cancel and destroy a timer event.

    Destruction drains a queued or running callback before releasing the
    object. Calling this from the timer's own callback is rejected.

    \retval 0             Timer destroyed.
    \retval -1            Error, with `EDEADLK`, `EINVAL`, or `EPERM`.
*/
int timer_event_destroy(timer_event_t *timer);

/** \brief Arm or rearm a timer relative to the monotonic clock.

    An existing queued deadline is replaced and a running callback is drained
    before the new deadline is published. A zero delay schedules immediate
    dispatch. A zero interval selects one-shot mode; a nonzero interval selects
    periodic mode.

    A timer callback may call this function on its own timer. The requested
    deadline is published after that callback returns.

    \param timer          Timer to arm.
    \param delay_ms       Delay before the first expiration.
    \param interval_ms    Period after the first expiration, or zero.

    \retval 0             Timer armed.
    \retval -1            Error, with errno set to `ECANCELED`, `EINVAL`,
                          `ENOMEM`, `EOVERFLOW`, or `EPERM`.
*/
int timer_event_arm(timer_event_t *timer, uint64_t delay_ms,
                    uint64_t interval_ms);

/** \brief Cancel and drain a timer event.

    A timer callback may cancel itself; in that case no periodic or explicitly
    requested rearm is published after the callback returns.

    \retval 0             A queued or running event was cancelled.
    \retval -1            Error, with `EINVAL`, `ENOENT`, or `EPERM`.
*/
int timer_event_cancel(timer_event_t *timer);

/** \brief Copy a coherent timer state snapshot.

    \retval 0             Snapshot copied.
    \retval -1            Error, with `EINVAL` or `EPERM`.
*/
int timer_event_get_info(timer_event_t *timer, timer_event_info_t *info);

/** @} */

__END_DECLS

#endif /* __KOS_TIMER_EVENT_H */
