# Shared G1 bus ownership

The optical drive and the optional ATA slave share one controller, one selected
task-file device, one Holly DMA engine, and the same completion/error events.
Treating their locks and interrupt handlers as separate resources allowed one
driver's initialization or teardown order to replace the other's handler.

This topic introduces one private `g1_bus` arbiter and moves the existing
BIOS-backed disc and ATA paths onto it. Their public APIs and default transport
remain unchanged.

## Controller claim

`g1_bus_lock()`, `g1_bus_trylock()`, and `g1_bus_lock_timed()` serialize the
controller, selected device, task-file registers, and DMA registers as one
claim. An interrupt-context call never blocks: the IRQ-safe lock only succeeds
when ownership is immediately available, while timed acquisition is restricted
to thread context.

Releasing the bus on a retail system restores the optical drive as the selected
master. A device change waits until Holly DMA is inactive and neither `BSY` nor
`DRQ` is asserted. Timed callers receive `ETIMEDOUT`; an interrupt-context
device change refuses to wait and returns the historical sentinel value.

Every migrated legacy call checks acquisition failure before touching the
controller. In particular, a failed claim can no longer be mistaken for a
successful timeout or ignored by a BIOS-backed operation.

## DMA event routing

The arbiter exclusively claims normal completion plus the three G1 DMA fault
events when its first client registers. Each client supplies a small
interrupt-context predicate. Clients are consulted in registration-slot order,
and the first client returning true consumes the event. The final client
release returns all four exclusive claims.

The BIOS-backed disc and ATA drivers now register predicates rather than
replacing one another's ASIC handlers. Both distinguish a normal completion
from overrun, illegal-address, and access-during-DMA faults. Fault events stop
Holly DMA, terminate the active operation as an I/O failure, and do not continue
an ATA chain or service the command server as though completion succeeded.
Blocking DMA keeps G1 ownership after the IRQ wake until the caller has
acknowledged terminal task-file status; nonblocking DMA releases ownership from
its completion handler.

A separate temporary claim is available internally for command-INTRQ users. It
supports mask and unmask operations while preserving exclusive ownership, so a
level-backed interrupt can be acknowledged safely in thread context.

## Fail-closed state

If a transport cannot quiesce the shared DMA engine, it may call
`g1_bus_mark_faulted()`. The latch wakes the current ownership chain and makes
all later lock attempts fail with `EIO` until reboot. Unlock after the latch
does not publish a second ownership token. This prevents later clients from
interpreting an available semaphore as proof that the hardware is safe.

## Resource cost

The arbiter adds one static semaphore, four DMA-client slots, four event claims,
one command client, and device/fault bookkeeping. It allocates no heap memory,
creates no thread, reserves no transfer buffer, and performs no periodic work.

## Validation

`utils/g1-bus-test` compiles the production arbiter with simulated task-file,
DMA, ASIC-claim, semaphore, timer, and IRQ boundaries. It covers:

- blocking, try, timed, and interrupt-context ownership behavior;
- exact device selection, DMA-idle waits, and status timeouts;
- partial four-event claim failure and unwind;
- ordered multi-client dispatch and last-client claim release;
- command-INTRQ claim, mask, unmask, dispatch, and release;
- shared DMA status and disable operations; and
- latched-fault wakeup and rejection of all later ownership attempts.

The harness runs with strict warnings and address/undefined-behavior
sanitizers. Physical validation remains required for master/slave settle time,
DMA error signaling, ATA expansion hardware, and concurrent optical/ATA load.
