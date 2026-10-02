# Maple bus and input/capture devices

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform**.

## Scope and entry points

Enumeration, queues/IRQs, controller/keyboard/mouse/lightgun/rumble, MIE, Dreameye and SIP capture.

- `kernel/arch/dreamcast/hardware/maple`
- `kernel/arch/dreamcast/include/dc/maple.h`
- `kernel/arch/dreamcast/include/dc/maple/controller.h`
- `kernel/arch/dreamcast/include/dc/maple/dreameye.h`
- `kernel/arch/dreamcast/include/dc/maple/sip.h`

Relevant existing documentation (dated claims must be rechecked):

- `doc/camera-driver-audit.md`
- `doc/sound-input-audit.md`
- `doc/background-execution-audit.md`

## Preserve these boundaries

- Match actual function descriptors and capabilities, not just the first connected device. Preserve payload alignment/endian and attach-state initialization.
- Trace frame allocation, DMA/cache state, queue publication, response validation, hot unplug and callback teardown together.
- Callbacks may unregister themselves/peers; snapshot ownership must survive mutation. Do not move arbitrary callbacks into a cooperative executor.
- Coordinate SIP audio capture and VMU storage before changing shared Maple transport.

## Coordinate before changing

- VMU storage
- Audio capture
- IRQ/cache and deferred workers

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/sip-stream-test` — candidate `make -C utils/sip-stream-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/maple/controller-snapshot`
- `examples/dreamcast/maple/keyboard-snapshot`
- `examples/dreamcast/maple/lightgun-capture`
- `examples/dreamcast/maple/sip-buffered`
- `examples/dreamcast/dreameye/basic`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

There is no blanket host suite proving the full Maple bus; target/device checks must be scoped and reported honestly.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
