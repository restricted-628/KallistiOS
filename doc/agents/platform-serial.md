# Startup, expansion probes and serial/debug paths

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform**.

## Scope and entry points

Architecture init/shutdown, boot flags, expansion probing, BIOS/syscall boundaries and SCIF/SPI/debug interfaces.

- `kernel/arch/dreamcast/kernel/init.c`
- `kernel/arch/dreamcast/kernel/entry.s`
- `kernel/arch/dreamcast/include/arch/init_flags.h`
- `kernel/arch/dreamcast/hardware/expansion.c`
- `kernel/arch/dreamcast/hardware/scif.c`
- `kernel/arch/dreamcast/hardware/scif_config.c`
- `kernel/arch/dreamcast/hardware/scif-spi.c`
- `kernel/arch/dreamcast/hardware/syscalls.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/low-level-capability-reconciliation.md`
- `doc/serial-audit.md`
- `doc/expansion-g2-audit.md`

## Preserve these boundaries

- Preserve the deliberate MMU startup default and explicit init-flag behavior; do not change boot policy while fixing a low-level mechanism.
- Probe conservatively and serialize register/driver ownership. A failed read or ID check is not permission to reset a live device.
- Preserve debug/loader availability and distinguish serial, network transport and raw restart/exit paths.
- Global teardown must respect subsystem stop failures and final callbacks; do not assume locally returned EBUSY is safe to ignore.

## Coordinate before changing

- GAPS/loader
- IRQ/DMA
- VFS final close
- Cache/MMU and networking

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/expansion-probe-test` — candidate `make -C utils/expansion-probe-test test` in an assigned checkout.
- `utils/scif-config-test` — candidate `make -C utils/scif-config-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/expansion-probe`
- `examples/dreamcast/basic/scif-status`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

No hardware probing, boot-ROM bypass changes or destructive diagnostics are implied by this guide.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
