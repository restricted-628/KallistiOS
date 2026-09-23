/* KallistiOS ##version##

   include/kos/fiber_sync.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/fiber_sync.h
    \brief   Synchronization which parks fibers rather than KOS threads.
    \ingroup fiber_synchronization
*/

#ifndef __KOS_FIBER_SYNC_H
#define __KOS_FIBER_SYNC_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>

/** \defgroup fiber_synchronization Cooperative Fiber Synchronization
    \brief Events and mutexes local to one attached fiber runtime
    \ingroup fibers

    These objects suspend only the calling fiber. A contended wait transfers
    to the owner thread's main fiber, which must continue dispatching ready
    fibers. Waiting from the main fiber itself is therefore rejected.

    Objects bind to the attached fiber runtime which creates them. They must be
    destroyed before that owner thread exits. They do not replace KOS mutexes
    or semaphores for synchronization between independently scheduled threads.

    @{ */

typedef struct kfiber_event kfiber_event_t;
typedef struct kfiber_mutex kfiber_mutex_t;

/** \brief Create a manual-reset cooperative event.

    The calling thread must already be attached with fiber_attach().

    \param signaled Initial event state.
    \return A new event, or `NULL` on error.
*/
kfiber_event_t *fiber_event_create(bool signaled);

/** \brief Destroy an event with no parked waiters.

    Must be called by the event's owner thread.
*/
int fiber_event_destroy(kfiber_event_t *event);

/** \brief Park the calling child fiber until the event is set.

    If the event is already set, this returns immediately. Otherwise the
    calling fiber becomes `KFIBER_STATE_WAITING` and transfers to its main
    fiber. The owner must later dispatch it after fiber_event_set() makes it
    ready. The main fiber cannot wait.
*/
int fiber_event_wait(kfiber_event_t *event);

/** \brief Set an event and make every parked waiter ready.

    This operation may be called from interrupt context. Setting an already-set
    event succeeds without changing state.
*/
int fiber_event_set(kfiber_event_t *event);

/** \brief Return an event to its nonsignaled state.

    Already-woken fibers remain ready. This operation may be called from
    interrupt context.
*/
int fiber_event_clear(kfiber_event_t *event);

/** \brief Return whether an event is currently set. */
bool fiber_event_is_set(const kfiber_event_t *event);

/** \brief Create a nonrecursive cooperative mutex.

    The calling thread must already be attached with fiber_attach().
*/
kfiber_mutex_t *fiber_mutex_create(void);

/** \brief Destroy an unlocked mutex with no parked waiters.

    Must be called by the mutex's owner thread.
*/
int fiber_mutex_destroy(kfiber_mutex_t *mutex);

/** \brief Acquire a cooperative mutex, parking a contending child fiber.

    Recursive acquisition returns `EDEADLK`. A contending main fiber also
    returns `EDEADLK`, because it has no dispatcher continuation to resume.
*/
int fiber_mutex_lock(kfiber_mutex_t *mutex);

/** \brief Attempt to acquire a cooperative mutex without parking.

    \retval 0  Acquired.
    \retval -1 Busy or invalid, with `errno` set.
*/
int fiber_mutex_trylock(kfiber_mutex_t *mutex);

/** \brief Release a cooperative mutex.

    Ownership passes directly to the oldest parked waiter, which becomes ready
    for its owner thread to dispatch.
*/
int fiber_mutex_unlock(kfiber_mutex_t *mutex);

/** @} */

__END_DECLS
#endif /* __KOS_FIBER_SYNC_H */
