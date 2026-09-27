# Direct raw DMA contract regression

Builds the production direct driver with MMIO, cache-maintenance, IRQ-client,
queue-submission, and GAPS-lease spies. No real drive or DMA engine is used.

Covers all even raw counts from 2 through 16, both completion-event orders,
exact packet/transfer/cache/protection ranges, cooked-format preservation,
short-DMA rejection, odd/invalid request rejection, and memory-boundary checks.
Deferred executor calls check copied format and byte accounting, cancellation
before the command, progress reporting, and GAPS lease claim/release/failure.
A lease sized for two cooked sectors must reject two raw sectors.

Fault injection also holds DMA active after disable for six fatal-stop cases:
RAM, VRAM, synchronous/queued GAPS reads, stream teardown, and stream-transfer
cancellation. A test-only escape replaces the final halt loop and checks that
interrupts/watchdog are disabled and no claim, handler, or buffer has retired.
Three controls let DMA stop after timeout/cancellation and require ordinary
error return, cache invalidation, and G1 release without a halt. These simulate
register state; they do not induce physical DMA faults.

Whole-range synchronous reads cover 17/18/32/33/34-sector boundaries, cached
and uncached RAM and PVR destinations, exact FAD/count/address progression,
per-command cache maintenance, middle/final short transfers, and failure to
reacquire G1. A driver-local simulated clock verifies decreasing lock budgets
and a single deadline across commands, including partial-byte accounting
when time expires between commands. Full-span RAM/VRAM, pointer-wrap, size,
and FAD overflow rejection must precede any I/O. GAPS remains bounded.

The test does not emulate payload writes or physical bus timing. Passing it
does not prove drive support, payload correctness, cache coherency on hardware,
interrupt races, timeout recovery, or performance. Real-drive comparisons and
hardware recovery tests remain required. Ranges and staged sessions are still
cooked-only; generic legacy sector APIs remain BIOS compatibility aliases.
