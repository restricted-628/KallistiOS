# G2 DMA state and ownership

KOS already provided four G2 DMA channels and width-specific PIO helpers. This
topic preserves the existing transfer entry point while making its validation,
cache behavior, channel state, and interaction with G2 PIO explicit.

## PIO critical sections

`g2_lock()` now records the suspend register for all four channels, suspends
each channel, and waits for the SH-4/G2 FIFO to drain. `g2_unlock()` restores
each exact value. A PIO access therefore cannot accidentally resume a transfer
which its owner had already suspended, and channel 3 is no longer omitted.
Nested locks compose: the inner lock restores the outer suspended state, and
the outer lock restores the original owner state.

## Submission contract

`g2_dma_transfer()` rejects zero-length, non-32-byte-multiple, oversized,
invalid-direction, and out-of-range transfers. It no longer rounds an invalid
length upward beyond the caller's buffer. A blocking transfer is rejected from
interrupt context.

The root-bus endpoint must cover contiguous system RAM or one PVR-RAM
aperture. With the MMU disabled, P0, P1, P2, and P3 aliases are accepted; with
the MMU enabled, only direct P1/P2 aliases are accepted. Translated P0/P3
mappings cannot be represented by one physical DMA span. Status snapshots
identify system RAM, the 64-bit PVR aperture, or the 32-bit PVR aperture.

The G2 endpoint follows different rules: it is a bus address, not a virtual
SH-4 mapping. Physical addresses such as the AICA RAM window, along with their
ordinary P1/P2/P3 aliases, normalize through the controller's 29-bit address
field even when the MMU is enabled.

## Cache and terminal state

Cacheable system-RAM sources are written back before a transfer to G2.
Cacheable system-RAM destinations are invalidated before a transfer from G2
and again only after the engine reaches a terminal state. P2 system RAM and
both PVR-RAM apertures bypass cache maintenance. The caller remains responsible
for excluding rendering, texture upload, or another DMA user of the same PVR
range.

The bridge SRAM window has an additional ownership rule. Every G2 transfer
whose external endpoint falls in that window must be fully contained in a live
SRAM lease. The transfer claims that lease until completion or cancellation.
A simultaneous G1 disc DMA to the same lease therefore fails with `EBUSY`
instead of relying on undocumented DMA ordering.

Every channel exposes a coherent status snapshot containing state, requested
and remaining bytes, sequence, completion/cancellation totals, result, and
callback-pending state. Completion publishes terminal state and wakes the
waiter before invoking the interrupt-context callback. A callback may submit a
new operation on the same channel without the old completion clearing the new
callback state.

`g2_dma_wait()` permits one explicit waiter on an active channel; a legacy
blocking submission already occupies that role. `g2_dma_suspend()`,
`g2_dma_resume()`, and `g2_dma_cancel()` operate on the same channel state.
Cancellation disables the channel, suppresses its callback, publishes
`ECANCELED`, and wakes the waiter.

Shutdown fences completion IRQ delivery while active channels are disabled and
their callbacks detached, then releases the exclusive ASIC event claims and
destroys the existing per-channel semaphores. This prevents a completion from
racing shutdown and being published once as success and again as cancellation.
Initialization does not publish readiness until all four event claims and
semaphores exist. Initialization and shutdown must run in thread context, and
overlapping lifecycle changes fail rather than exposing a partially created or
partially destroyed channel set. Channel operations recheck readiness after
fencing IRQ delivery, closing admission races with shutdown.

## Resource cost

The implementation adds fixed bookkeeping for four hardware channels. It does
not allocate heap memory, create threads, reserve transfer buffers, or perform
periodic work. Callers continue to own every DMA buffer.

## Validation

`utils/g2dma-test` compiles the production implementation with simulated DMA,
ASIC, semaphore, cache, MMU, and IRQ boundaries. It covers:

- four-channel and nested PIO lock restoration;
- validation and MMU-aware address handling, including low physical AICA
  addresses and both PVR-RAM apertures;
- cache maintenance in both directions;
- progress, suspend/resume, cancellation, and timed waits;
- blocking completion and same-channel callback chaining;
- partial initialization unwind, IRQ-context lifecycle rejection, and shutdown
  callback suppression.

`examples/dreamcast/basic/dma/g2-state` performs a target-side AICA RAM
round-trip with the existing sound-memory allocator and verifies the terminal
status snapshots. `examples/dreamcast/cdrom/direct-gaps-stage` performs a
serialized disc-to-SRAM-to-system-RAM pipeline when the bridge is available.
Physical hardware validation remains required for suspend latency,
cancellation during bus latency, PVR aperture behavior, cache aliases, and
every external G2 device variant.
