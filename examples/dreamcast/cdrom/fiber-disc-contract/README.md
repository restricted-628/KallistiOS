# No-media fiber/disc adapter regression

Uses production fibers, cooperative events, the request queue, callback worker,
and `libfiber_disc`. A link-time spy replaces only direct DMA submission with
a custom request executor: no drive command or physical DMA is issued. The
same suite runs through RAM and GAPS submission. GAPS uses a synthetic lease
token and nonzero offset to check argument forwarding, not physical pinning.

Checks owner restrictions, child-only waits, sibling progress, early completion,
notification while a callback deliberately remains running, capacity/backpressure,
submission failure, running and queued cancellation, shutdown admission rejection,
safe release order, simulated timeout propagation, and request-system shutdown.
An invalid GAPS token checks error propagation without fallback to RAM.

The deliberately delayed callback is a test instrument, not an application
pattern. A five-second watchdog halts failed scheduling/drain scenarios. The
check count can vary with scheduling; require the PASS marker, six sibling
steps, fourteen completed callbacks, and `targets=ram,gaps`. Run both interpreter
and dynarec modes.
This is software-lifetime coverage, not cache, device IRQ or media validation.

The RAM/GAPS suite passed in Flycast interpreter (299 checks) and dynarec
(301 checks), each with six sibling steps and fourteen callbacks. The addon
and regression compile with SH-4 GCC 16.2, GNU C23, and `-Wall -Wextra -Werror`.
The live-media `fiber-read` example is compile/link validated, not run against
a disc here.
