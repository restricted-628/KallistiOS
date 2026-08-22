/* KallistiOS ##version##

   include/kos/fiber_service.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/fiber_service.h
    \brief   Persistent cooperative services on one KOS thread.
    \ingroup fiber_services

    A service executor owns one KOS thread and one fiber for each configured
    service. This is useful for bounded background state machines that need a
    persistent stack but do not need independently scheduled kernel threads.

    Service fibers share the executor thread's MMU, TLS, libc, priority, and
    kernel wait identity. They may use fiber_service_wait(),
    fiber_service_yield(), or the cooperative objects in kos/fiber_sync.h.
    Blocking on a regular KOS primitive blocks the executor thread and every
    service it owns.
*/

#ifndef __KOS_FIBER_SERVICE_H
#define __KOS_FIBER_SERVICE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <kos/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** \defgroup fiber_services Fiber Service Executors
    \brief Persistent fiber services sharing one KOS thread
    \ingroup threading

    @{ */

typedef struct fiber_service_executor fiber_service_executor_t;
typedef struct fiber_service fiber_service_t;

/** \brief Fixed-size message copied through a service mailbox.

    The runtime does not interpret either word. Pointer values are borrowed;
    their referents must remain valid until the consumer receives the message.
*/
typedef struct fiber_service_message {
    uintptr_t tag;
    uintptr_t value;
} fiber_service_message_t;

/** \brief Coherent service mailbox statistics. */
typedef struct fiber_service_queue_info {
    size_t capacity;
    size_t queued;
    size_t high_watermark;
    uint64_t posted;
    uint64_t received;
    uint64_t rejected;
} fiber_service_queue_info_t;

/** \brief Service entry-point function. */
typedef void (*fiber_service_entry_t)(fiber_service_t *service, void *data);

/** \brief Observable service lifecycle state. */
typedef enum fiber_service_state {
    FIBER_SERVICE_CREATED  = 0,
    FIBER_SERVICE_WAITING  = 1,
    FIBER_SERVICE_READY    = 2,
    FIBER_SERVICE_RUNNING  = 3,
    FIBER_SERVICE_FINISHED = 4,
    FIBER_SERVICE_FAILED   = 5,
    FIBER_SERVICE_STOPPED  = 6
} fiber_service_state_t;

/** \brief Allocate an unstarted service executor. */
fiber_service_executor_t *fiber_service_executor_create(void);

/** \brief Add a persistent service before starting its executor.

    The stack is borrowed and follows the same mapping, alignment, lifetime,
    and non-overlap requirements as fiber_create(). Services cannot be added
    after fiber_service_executor_start().
*/
fiber_service_t *fiber_service_add(fiber_service_executor_t *executor,
                                   void *stack, size_t stack_size,
                                   fiber_service_entry_t entry, void *data);

/** \brief Configure a bounded mailbox before starting the executor.

    Storage is allocated once here and freed with the executor. Posting and
    receiving never allocate. This function may be called once per service.

    \param service   Unstarted service.
    \param capacity  Positive number of fixed-size messages.
*/
int fiber_service_queue_configure(fiber_service_t *service, size_t capacity);

/** \brief Start the executor's owning KOS thread.

    This call waits until all configured fibers have been created or startup
    has failed. Services begin waiting and run only after a wake request.

    \param executor  Unstarted executor.
    \param attr      Optional KOS thread attributes.
    \retval 0        Executor is running.
    \retval -1       Startup failed, with `errno` set.
*/
int fiber_service_executor_start(fiber_service_executor_t *executor,
                                 const kthread_attr_t *attr);

/** \brief Stop, join, and destroy an executor.

    Waiting services are resumed so their wait returns `ECANCELED`. A service
    currently running must yield or return before the owning thread can join;
    cooperative code cannot be forcibly preempted safely.

    Callers must stop and unregister all interrupt or callback producers that
    can still wake, stop, or post to a service before destroying the executor.

    This function cannot be called from the executor thread.
*/
int fiber_service_executor_destroy(fiber_service_executor_t *executor);

/** \brief Wake a service, coalescing repeated wake requests.

    This function may be called from interrupt context. It never switches a
    fiber directly; the owning thread performs the transfer later. If the
    service fiber is blocked on a fiber event or mutex, the request remains
    pending and cannot complete that unrelated synchronization wait.
*/
int fiber_service_wake(fiber_service_t *service);

/** \brief Request cooperative termination and wake the service.

    This function may be called from interrupt context. The service should
    observe fiber_service_stop_requested() or an `ECANCELED` wait/yield result
    and return from its entry point.
*/
int fiber_service_request_stop(fiber_service_t *service);

/** \brief Copy a message into a service's bounded mailbox and wake it.

    This operation may be called from interrupt context and performs no dynamic
    allocation. `EAGAIN` reports backpressure when the mailbox is full. A post
    made while the service fiber is blocked on a fiber event or mutex remains
    queued and cannot complete that unrelated synchronization wait.

    Producers must stop posting before the executor is destroyed.
*/
int fiber_service_post(fiber_service_t *service,
                       const fiber_service_message_t *message);

/** \brief Receive the oldest mailbox message, parking cooperatively if empty.

    May only be called by the destination service. A zero deadline waits
    indefinitely; a nonzero deadline uses timer_ms_gettime64() units.

    \retval 0  A message was copied to `message`.
    \retval -1 Error, with `ETIMEDOUT`, `ECANCELED`, or another errno.
*/
int fiber_service_receive(fiber_service_t *service,
                          fiber_service_message_t *message,
                          uint64_t deadline_ms);

/** \brief Copy a coherent mailbox-statistics snapshot. */
int fiber_service_get_queue_info(const fiber_service_t *service,
                                 fiber_service_queue_info_t *info);

/** \brief Wait cooperatively for a wake request or absolute deadline.

    May only be called by the service's own fiber. A deadline of zero waits
    indefinitely. The deadline uses timer_ms_gettime64() units.

    \retval 0        Woken or deadline reached.
    \retval -1       Stop requested or invalid context, with `errno` set.
*/
int fiber_service_wait(fiber_service_t *service, uint64_t deadline_ms);

/** \brief Yield cooperatively while remaining ready to run. */
int fiber_service_yield(fiber_service_t *service);

/** \brief Return whether termination has been requested. */
bool fiber_service_stop_requested(const fiber_service_t *service);

/** \brief Return a coherent service-state snapshot. */
fiber_service_state_t fiber_service_get_state(const fiber_service_t *service);

/** \brief Return the executor's KOS thread, or `NULL` before start. */
kthread_t *fiber_service_executor_get_thread(
    const fiber_service_executor_t *executor);

/** @} */

__END_DECLS
#endif /* __KOS_FIBER_SERVICE_H */
