# RTC, timers and VBlank dispatch

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform + fibers**.

## Scope and entry points

RTC/calendar conversion, POSIX clocks, TMU channels, VBlank callback ordering and optional fiber adapters.

- `kernel/arch/dreamcast/hardware/rtc.c`
- `kernel/arch/dreamcast/hardware/rtc_calendar.c`
- `kernel/arch/dreamcast/kernel/timer.c`
- `kernel/libc/posix/clock_gettime.c`
- `kernel/libc/posix/settimeofday.c`
- `kernel/arch/dreamcast/hardware/vblank.c`
- `addons/libfiber_vblank`

Relevant existing documentation (dated claims must be rechecked):

- `doc/rtc-audit.md`
- `doc/tmu-channel-ownership.md`
- `doc/vblank-callback-safety.md`
- `doc/fiber-vblank.md`

## Preserve these boundaries

- Preserve TMU0 scheduler and TMU2 uptime roles while auditing optional TMU1 ownership and pending events.
- Keep wall-clock/calendar semantics distinct from monotonic deadlines; validate timespec ranges and process-clock identity.
- Explicit priority order is lower-first/newest-first; legacy registrations preserve FIFO. VBlank callbacks are IRQ context, not a thread.
- Removal during dispatch must retain traversal storage and defer reclamation. Optional fiber waits do not require the executor.

## Coordinate before changing

- Services and timer events
- Video/PVR display stages
- Fibers
- VMU/flash timestamps

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/rtc-test` — candidate `make -C utils/rtc-test test` in an assigned checkout.
- `utils/posix-clock-test` — candidate `make -C utils/posix-clock-test test` in an assigned checkout.
- `utils/tmu-channel-test` — candidate `make -C utils/tmu-channel-test test` in an assigned checkout.
- `utils/vblank-handler-test` — candidate `make -C utils/vblank-handler-test test` in an assigned checkout.
- `utils/fiber-vblank-test` — candidate `make -C utils/fiber-vblank-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/rtc`
- `examples/dreamcast/basic/threading/tmu1-channel`
- `examples/dreamcast/basic/threading/vblank-priority`
- `examples/dreamcast/basic/threading/fiber-vblank`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Emulated time and callback tests do not establish physical scanout timing or RTC electrical behavior.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
