# Fiber service executor probe

This regression validates the KOS fiber service executor. Three persistent
fibers share one owning KOS thread:

- one is woken from a VBlank interrupt;
- one wakes at an absolute monotonic deadline;
- one receives a cooperative shutdown request while waiting indefinitely.

Interrupt handlers only publish a wake and signal the owner thread. They never
switch a fiber or alter the owner thread's MMU context.

The probe also attempts a service wait while that service owns a store-queue
transaction. The rejected transfer must return `EBUSY` without leaving the
service in a waiting state.

Success prints `KOSFIBERSVC irq=1 deadline=N shutdown=1`.

The same executable also validates the opt-in executor math-context
policy (before printing the final success marker):

- The original executor uses default flags and rejects an attempted late
  upgrade from inside a running service.
- Invalid flags are rejected at executor creation.
- Two math services start with identity XMTRX, then retain different matrices
  across 32 yields and timed waits each. All 16 registers and actual FTRV
  results are checked, along with distinct scalar FR12 values.
- A separate KOS thread overwrites XMTRX before each service resumes, forcing
  64 kernel scheduling round trips in addition to cooperative transfers.
- Both services verify their matrices and scalar state again when shutdown
  resumes their indefinite waits with ECANCELED.

The extra success marker is
`KOSFIBERSVCMATH rounds=32/32 preempt=64 shutdown=1/1 result=PASS`.
The test deliberately blocks the executor on a semaphore to force the helper
thread round trips; that is a test technique, not recommended service design.
It does not run a Sofdec decoder, validate arbitrary FPSCR changes, or prove
physical-hardware timing or media playback performance.

Validation on 2026-09-21: SH-4 GCC 16.2 rebuilt KOS and this probe; both
Flycast interpreter and dynarec produced the two success markers above.
The unchanged mailbox and service-synchronization probes were relinked and
also passed in both modes. Physical-hardware validation remains open.
