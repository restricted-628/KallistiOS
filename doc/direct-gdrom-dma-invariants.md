# Direct GD-ROM DMA invariants

## Status and scope

The direct backend is experimental. It provides post-boot access to the
Dreamcast optical drive without using the BIOS GD command server for each
operation.

The supported public scope is CD and CD-R data. Media outside that scope is not
a supported target and must not be inferred from unused command fields.

This document records implementation invariants and failure rules. It is not a
hardware bring-up guide.

## Ownership

All GD-ROM, G1 ATA, BIOS CD-ROM, direct packet, PIO, and GD-DMA operations
share `g1_bus` ownership.

- A caller must own G1 before selecting a device or touching shared task-file
  state.
- Ownership spans the complete command lifetime, including DMA completion,
  cache maintenance, error collection, and recovery.
- No path may continue after `g1_bus_lock*()` fails.
- A latched G1 fault fails future ownership attempts with `EIO` until the
  subsystem is safely reinitialized.
- The optical drive is the master device; the external ATA path remains the
  slave-device client.

Releasing ownership while GD DMA is active permits another G1 client to touch
the bus during a transfer. That is a subsystem fault, not a recoverable queue
collision.

## Command and DMA sequencing

A direct DMA read follows this order:

1. Validate the FAD, sector count, destination, alignment, and addressability.
2. Acquire G1 ownership and select the optical-drive device.
3. Ensure no prior DMA operation remains active.
4. Prepare the destination cache state.
5. Submit the packet command and reach its data phase.
6. Program the GD-DMA protection, destination, length, direction, and enable
   state in the required order.
7. Start DMA only after every preceding register is coherent.
8. Wait for terminal DMA and command state without polling the firmware server
   concurrently from thread and interrupt contexts.
9. Disable and quiesce DMA before releasing G1.
10. Invalidate the completed destination range before publishing completion.

The implementation must never rewrite destination or length registers while a
transfer is active.

## Address and length rules

- Direct DMA destinations must be 32-byte aligned system RAM.
- Cache aliases are normalized before programming the physical destination.
- Arithmetic uses subtraction-based bounds checks so address and length sums
  cannot wrap.
- Zero-length transfers are rejected before hardware access.
- One public command transfers at most sixteen sectors, bounding continuous
  G1 ownership. Callers must divide larger operations.
- The transport result reports bytes actually transferred before completion
  or failure.
- Request-engine reads divide larger operations into bounded segments and
  requeue between them so unrelated G1 clients can make progress.
- Requested payload, useful data, and physical I/O remain separate counters
  in asynchronous status.

## Cache coherency

Cache maintenance brackets every cacheable DMA destination:

- invalidate immediately before the command while G1 ownership is held; and
- invalidate again after DMA has stopped and before terminal status is visible.

If DMA cannot be proven stopped, cleanup must not invalidate a range that the
engine may still be writing.

Uncached/P2 destinations bypass cache operations but retain all alignment,
range, ownership, and completion rules.

## Interrupt rules

The shared G1 layer owns the command and DMA interrupt vectors and dispatches
registered clients.

- Interrupt handlers acknowledge hardware and record bounded completion state.
- Interrupt clients wake only the thread which owns the direct command.
- Stale interrupt state is matched against the active DMA operation before it
  can terminate anything.
- Application callbacks run on the isolated callback worker, never in
  interrupt context or on the transport worker.
- Access-during-DMA, illegal-address, and overrun conditions are terminal bus
  faults unless safe quiescence is established.

## Cancellation, timeout, and diagnostic aborts

- Every public transport operation requires a nonzero timeout.
- Queued requests cancel without touching hardware.
- Active cancellation wakes the DMA owner and enters bounded recovery.
- The controlled DMA diagnostic can force an abort after starting Holly.
- A failed stop proceeds to bounded recovery.
- A timeout never becomes an unbounded wait during cleanup.
- Transfer accounting includes only bytes observed before the terminal state.

## Recovery

Recovery is deliberately conservative:

1. Stop issuing new hardware operations.
2. Attempt to disable and quiesce GD DMA.
3. Collect command and drive error state when safe.
4. Soft-reset and reprobe the post-boot drive when permitted.
5. Mark G1 faulted if safe quiescence cannot be established.

The direct backend does not attempt cold-boot optical-drive authorization.

## Staged streaming

A staged session owns the drive while read-ahead data remains in the drive
buffer. Transfers from that buffer use ordinary request objects.

- Every session requires a nonzero idle timeout.
- The timeout bounds application-controlled ownership of G1.
- A closing flag orders timeout/cancellation against concurrent transfer
  publication.
- Transfers bypass the normal command queue because the session already owns
  the drive.
- Session state and transfer progress remain separate.

## Validation requirements

Before a direct-backend change is accepted, it must pass:

- the host packet/response vector test;
- a full warning-clean KOS build;
- direct status and readiness diagnostics;
- bounded PIO reads compared byte-for-byte with the BIOS backend;
- aligned DMA reads with destination guards;
- cancellation, timeout, forced-error, and post-recovery reuse checks;
- BIOS/direct coexistence tests through shared G1 ownership;
- dynarec and interpreter emulator runs without an SH-4 exception.

ISO9660 integration is a separate dependent topic and carries its own
validation gates.

Physical-drive timing, tray behavior, CD-R variability, and cache aliases still
require hardware validation. None of those gates may be converted into a
support claim by emulator-only results.
