# No-media fiber/disc adapter regression

Uses production fibers, cooperative events, the request queue, callback worker,
and `libfiber_disc`. A link-time spy replaces only direct DMA submission with
a custom request executor: no drive command or physical DMA is issued.

Checks owner restrictions, child-only waits, sibling progress, early completion,
notification while a callback deliberately remains running, capacity/backpressure,
submission failure, running and queued cancellation, shutdown admission rejection,
safe release order, simulated timeout propagation, and request-system shutdown.

The deliberately delayed callback is a test instrument, not an application
pattern. A five-second watchdog halts failed scheduling/drain scenarios. The
check count can vary with scheduling; require the PASS marker, three sibling
steps, and seven completed callbacks. Run both interpreter and dynarec modes.
This is software-lifetime coverage, not cache, device IRQ or media validation.

Validation for this addition: 135 checks, three sibling steps, and seven
completed callbacks passed in both Flycast interpreter and dynarec modes.
The full SH-4 GCC 16.2 build and focused addon/example `-Werror` builds pass.
The direct-DMA queue (334 checks), convenience routing (405 checks), core
fiber, and fiber-sync regressions also pass in both modes. The live-media
`fiber-read` example is compile/link validated, not run against a disc here.
