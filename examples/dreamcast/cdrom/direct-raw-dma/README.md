# Direct raw DMA contract regression

Builds the production direct driver with MMIO, cache-maintenance, IRQ-client,
queue-submission, and GAPS-lease spies. No real drive or DMA engine is used.

Covers all even raw counts from 2 through 16, both completion-event orders,
exact packet/transfer/cache/protection ranges, cooked-format preservation,
short-DMA rejection, odd/invalid request rejection, and memory-boundary checks.
Deferred executor calls check copied format and byte accounting, cancellation
before the command, progress reporting, and GAPS lease claim/release/failure.
A lease sized for two cooked sectors must reject two raw sectors.

The test does not emulate payload writes or physical bus timing. Passing it
does not prove drive support, payload correctness, cache coherency on hardware,
interrupt races, timeout recovery, or performance. Real-drive comparisons and
hardware recovery tests remain required. Ranges and staged sessions are still
cooked-only; generic legacy sector APIs remain BIOS compatibility aliases.
