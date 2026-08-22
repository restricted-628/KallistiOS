# KOS background execution policy and audit

## Scope

This document classifies thread-context execution created by the KOS kernel and
bundled add-ons. Application-created KOS, C11, and POSIX threads remain under
application ownership and are outside this audit.

Three execution models are relevant here:

- **Dedicated thread:** persistent state, independent scheduling, blocking
  waits, or failure isolation are part of the subsystem contract.
- **Thread worker:** a coalesced wake flag activates one dedicated thread. This
  fits interrupt bottom halves whose source is masked or whose state remains
  available until serviced.
- **Workqueue:** finite callbacks execute serially on one deadline-ordered queue
  thread.

Callbacks which may sleep or perform arbitrary application work cannot be moved
silently onto a shared workqueue. Such a change would alter observable thread,
priority, stack, TLS, shutdown, and head-of-line-blocking behavior.

## Resource proportionality

Optional capabilities should not reserve significant RAM, create threads, or
perform periodic work until explicitly enabled or first used. A generic helper
does not itself impose a runtime cost: `workqueue_create()` is the operation
which allocates its queue and carrier thread.

Subsystems which require a persistent polling cadence, independent failure
isolation, or arbitrary callbacks may still own a dedicated thread. Their
initialization and shutdown paths must make that ownership explicit and must
report thread-creation failure instead of publishing a partially started
service.

## Current ownership decisions

| Owner | Model | Reason |
| --- | --- | --- |
| Network protocol deadlines | Workqueue | ARP, TCP, DHCP, and fragment work is finite and deadline ordered. |
| BBA receive path | Thread worker | Interrupt-driven packet draining is latency-sensitive and may run substantial network input. |
| W5500 receive path | Dedicated thread when enabled | The device has no receive interrupt input and requires polling. |
| PPP main loop | Dedicated thread | It owns device polling, protocol state, retransmission timing, and blocking connection setup. |
| Profiling sampler | Dedicated thread | Sampling is coupled to scheduler polling. |
| MIE callback dispatch | Shared thread worker | Registered callbacks require thread context and may unregister callbacks while dispatch is active. |
| Controller callbacks | Existing per-registration workers | Their isolation is observable and their callback contract is not bounded. |
| Threaded hardware events | Existing per-registration workers | A handler may sleep while its interrupt source remains masked. |
| One-shot timers | Existing per-timer workers | Independent callback isolation is existing behavior. |

## Correctness changes

The checked workqueue operations make queue ownership observable and safe:

- duplicate pending insertion is rejected;
- cancellation drains a running callback and suppresses racing requeue;
- self-cancellation and self-destruction report deadlock instead of waiting;
- far-future deadlines are waited in bounded slices; and
- concurrent stop requests serialize the worker join.

The original void enqueue and cancel interfaces remain source-compatible
wrappers. New code should use the checked forms when rejection changes program
behavior.

The audit also corrected independent background-worker defects:

1. BBA startup now fails if its receive worker cannot be created.
2. W5500 startup now fails without publishing a null receive thread.
3. Network shutdown stops receive producers before protocol consumers are
   destroyed.
4. MIE dispatch removes and snapshots one callback under its mutex, then invokes
   the snapshot after unlocking. Self- or peer-unregistration cannot invalidate
   the dispatcher's private iteration state.

## Interrupt ingress

Workqueue enqueue, cancellation, and state queries take a KOS mutex and are not
interrupt-safe. Interrupt handlers should acknowledge hardware, copy a small
bounded event into an IRQ-safe subsystem queue when one exists, and wake
thread-context processing. Any such ingress queue must define backpressure when
full.

## Validation

`examples/dreamcast/basic/threading/workqueue-safety` covers active-callback
cancellation, periodic requeue suppression, duplicate rejection,
self-cancellation, ordinary periodic scheduling, deadline ordering,
long-deadline handling, and admission rejection after shutdown.

The BBA, W5500, and MIE changes are compile-validated without making unavailable
hardware behavior claims. Device startup failure and callback-unregistration
interleavings remain runtime validation cases.
