# G1, direct GD-ROM and optical filesystem I/O

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Storage/direct I/O**.

## Scope and entry points

G1 arbitration, direct/BIOS routing, range/request/stream lifetimes, CDDA and ISO9660.

- `kernel/arch/dreamcast/hardware/g1_bus.c`
- `kernel/arch/dreamcast/hardware/g1ata.c`
- `kernel/arch/dreamcast/hardware/gdrom_direct.c`
- `kernel/arch/dreamcast/hardware/gdrom_spi.c`
- `kernel/arch/dreamcast/hardware/cdrom_request.c`
- `kernel/arch/dreamcast/hardware/cdrom_range.c`
- `kernel/arch/dreamcast/fs/fs_iso9660.c`
- `kernel/arch/dreamcast/include/dc/gdrom_direct.h`
- `addons/libfiber_disc/fiber_disc.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/disc-backend-defaults.md`
- `doc/direct-gdrom-dma-invariants.md`
- `doc/g1-bus-ownership.md`
- `doc/direct-dma-examples.md`

## Preserve these boundaries

- Direct is the accepted fork default; BIOS selection is explicit, with no silent fallback. Preserve the agreed migration and low-density contribution policy.
- One arbiter must cover disc/ATA and the full command/DMA/cache/error lifetime.
- Requests remain live through finalizers/callback return; destination reuse requires actual retirement. Preserve halt-on-unquiesced-DMA instead of inventing quarantine.
- GAPS and cooperative stream adapters exist on master; check code before trusting older teaching documents. Application fiber waits need no Service Executor.

## Coordinate before changing

- GAPS/loader and networking
- VFS lifetime
- IRQ/cache and PVR targets
- Fibers

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/g1-bus-test` — candidate `make -C utils/g1-bus-test test` in an assigned checkout.
- `utils/gdrom-spi-test` — candidate `make -C utils/gdrom-spi-test test` in an assigned checkout.
- `utils/dma-memory-test` — candidate `make -C utils/dma-memory-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/cdrom/direct-lifecycle`
- `examples/dreamcast/cdrom/direct-recovery`
- `examples/dreamcast/cdrom/fiber-disc-contract`
- `examples/dreamcast/cdrom/fiber-vram`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Do not mix driver default policy with a narrow upstream G1 mechanism PR.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
