# ASIC events, G2 DMA and shared transfer safety

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform**.

## Scope and entry points

IRQ/ASIC ownership, DMA range admission, G2 channel lifecycle and common transfer contracts.

- `kernel/arch/dreamcast/hardware/asic.c`
- `kernel/arch/dreamcast/hardware/g2dma.c`
- `kernel/arch/dreamcast/hardware/dma_memory.h`
- `kernel/arch/dreamcast/include/dc/asic.h`
- `kernel/arch/dreamcast/include/dc/g2bus.h`
- `kernel/arch/dreamcast/kernel/stack.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/asic-event-ownership.md`
- `doc/g2-dma-safety.md`
- `doc/expansion-g2-audit.md`

## Preserve these boundaries

- Check existing startup ownership before claiming an event. BUSY can mean a legitimate current owner.
- Track completion generations, active waiters, channel/lease lifetime and callback retirement through cancel and shutdown.
- The dirty offset-aperture rejection is existing work, not a new task's patch. Coordinate any change with GAPS/networking and storage.
- Do not let software cancellation imply hardware idle. Review top-level teardown ordering as well as local stop return values.

## Coordinate before changing

- GAPS/networking
- Disc/G1
- Audio/SPU
- PVR and kernel shutdown

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/asic-event-test` — candidate `make -C utils/asic-event-test test` in an assigned checkout.
- `utils/g2dma-test` — candidate `make -C utils/g2dma-test test` in an assigned checkout.
- `utils/dma-memory-test` — candidate `make -C utils/dma-memory-test test` in an assigned checkout.
- `utils/irq-stack-test` — candidate `make -C utils/irq-stack-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/asic-event-claim`
- `examples/dreamcast/basic/dma`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

An identified teardown or aperture gap remains a candidate until its exact call path and failure conditions are established.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
