# KOS workqueue safety and service boundaries

KOS workqueues provide one-shot or deadline-scheduled callbacks on a dedicated
thread. They are intended for finite thread-context work and do not replace
ordinary KOS threads whose persistent state or blocking behavior is part of
their contract.

## Checked job operations

`workqueue_enqueue_ex()` rejects malformed jobs, a duplicate pending instance,
a stopped queue, and requeue suppressed by an active cancellation. A callback
may still enqueue its own job once to implement periodic work. Jobs remain
caller-owned and must stay alive while queued, running, or cancelling.

`workqueue_cancel_ex()` first removes a pending instance. If the callback is
already running, cancellation establishes a barrier, waits for the callback to
return, and removes any requeue that raced establishment of the barrier. An
attempt by the running callback to requeue after the barrier returns
`ECANCELED`. Self-cancellation reports `EDEADLK` instead of blocking the worker
on itself. Barriers are tracked per job, so that callback may still cancel a
different pending job while its own external cancellation is draining.

`workqueue_job_get_info()` copies queued, running, and cancelling state while
holding the queue lock. A periodic callback which has already requeued itself
can be both running and queued; these states describe distinct instances of the
same caller-owned job record.

The original void enqueue and cancel functions remain as source-compatible
wrappers around the checked operations. They deliberately discard the result.
New code should use the checked forms when rejection changes program behavior.

## Deadlines and shutdown

The worker keeps the pending list in absolute millisecond order. Deadlines
farther away than the signed condition-variable timeout range are waited in
bounded slices and re-evaluated, avoiding integer truncation or premature
execution.

Queue shutdown rejects new work, wakes the worker, joins it, and is serialized
when multiple threads request shutdown concurrently. A callback may request
that its queue stop, but cannot join or destroy its own worker thread.

## Interrupt ingress

Workqueue enqueue, cancel, and state-query operations take a KOS mutex and are
not interrupt-safe. Interrupt handlers should acknowledge hardware, copy a
small bounded event into an IRQ-safe subsystem queue when one exists, and wake
thread-context processing. That ingress queue must provide explicit
backpressure when it is full.

## Regression coverage

`examples/dreamcast/basic/threading/workqueue-safety` verifies active-callback
cancellation, racing periodic requeue suppression, duplicate rejection,
self-cancel deadlock detection, ordinary periodic self-requeue, deadline
ordering, long-deadline handling, and post-shutdown admission rejection.
