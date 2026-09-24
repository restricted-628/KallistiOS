# Deferred VBlank callbacks

The native VBlank API remains an IRQ dispatcher. `libfiber_vblank` is an optional
adapter, not a kernel worker. It runs ordered callbacks on one caller-scheduled
child fiber, using either the core fiber runtime or the mutually exclusive
SH4ZAM runtime. No math wrappers or extra FPU saves are added by the adapter.
Choose `KFIBER_ATTACH_MATH_CONTEXT` on the runtime/executor if callbacks retain
XMTRX across yields; the selected provider implements that preservation.

## Ordering and overload

Allocate a fixed registration capacity, add callbacks, then start. Lower numeric
priorities run first; ties run newest first. The list is immutable after start.
Stop and replace the dispatcher to reconfigure it. There is deliberately no
allocation, list mutation, or general callback execution in the IRQ path.

Only a pending frame counter is retained, saturated at `UINT32_MAX`. The IRQ
signals on its zero-to-nonzero transition. Dispatch atomically takes that count
and runs one ordered batch. All callbacks see the same count. Frames arriving
while callbacks run become one subsequent batch, not an unbounded work queue.
Callback order survives cooperative yields; a stalled earlier callback delays
all later callbacks. Priority is batch order, not preemption or a deadline.

Callbacks run after the interrupt when their owner schedules them. Display
register changes that must occur during blanking still belong in bounded IRQ
callbacks. Blocking a KOS thread from a fiber blocks all siblings, so use
cooperative waits and short work units. Do not yield with hardware ownership,
ordinary KOS mutexes, or temporary incompatible FPU modes held.

## Core fiber use

See `examples/dreamcast/basic/threading/fiber-vblank`. Attach the application
thread and create a child dispatcher plus a manual-reset fiber event. The IRQ
wake callback only sets that event. The child waits, clears the event **before**
dispatching, then dispatches one batch. The application's main fiber schedules
ready children. There is no hidden thread and no Service Executor dependency.

## Service Executor use

See `examples/dreamcast/basic/threading/fiber-vblank-service` on the integrated
and addon branches. Register one persistent service. The IRQ calls
`fiber_service_wake()`. The service dispatches pending batches and yields between
them, waiting only after dispatch reports no pending work. Recheck the batch
counter after yielding: the executor may consume wake hints while an earlier
callback is still suspended. This is why blindly waiting after each batch could
strand pending work. Separate service fibers do not provide callback ordering.

The core-fiber submission does not acquire a Service Executor dependency.
The complete core branch includes the application-fiber example; the complete
SH4ZAM branch additionally supports the service example.

## Shutdown

Stop the adapter before destroying an event, service, executor or VBlank itself.
Stop unregisters the IRQ and discards pending work, but does not wake a parked
consumer or join it. An already-selected callback can finish; callbacks not yet
selected are skipped. Wake and finish the application child, or destroy/join
the executor, then destroy the adapter. Destruction fails with `EBUSY` while
started or inside dispatch, including while a callback is suspended. Never
forcibly destroy such a fiber: its dispatch cannot unwind. The caller serializes
lifecycle operations and prevents future calls through destroyed pointers.

## Regression coverage

The host suite in `utils/fiber-vblank-test` executes production code, checking
priority ties, capacity, failed-start retry, coalescing, saturation, reentrancy,
IRQ/main-fiber rejection, callback stop, destruction barriers, and allocation
balance. The target examples pause an earlier callback across several real
VBlank interrupts and verify order `13241324`, two batches, coalesced frame
counts, and orderly teardown. Emulator execution is not hardware timing proof.
