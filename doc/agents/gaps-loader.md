# GAPS SRAM and resident-loader handoff

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Networking/BBA + storage**.

## Scope and entry points

Shared BBA bridge SRAM ownership, leases, copy-engine claims and dcload service protection.

- `kernel/arch/dreamcast/hardware/gaps.c`
- `kernel/arch/dreamcast/hardware/gaps_internal.h`
- `kernel/arch/dreamcast/include/dc/gaps.h`
- `kernel/arch/dreamcast/hardware/dcload_syscalls.c`
- `kernel/arch/dreamcast/fs/fs_dcload.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/gaps-ownership.md`

## Preserve these boundaries

- Separate owner token, allocated lease, DMA claim, autonomous NIC accesses and resident-loader readiness. A transfer claim is not an RX/TX fence.
- NETWORK and independent STAGING are deliberate roles; G1 DMA remains allowed through owner-authorized NETWORK leases.
- Do not assume releasing a KOS owner restores dcload state. Ordinary loader services, restart/exit and GDB transport may have different paths.
- Alternative apertures map existing SRAM; they do not create extra storage. Register and backing-range ownership must cover the full transfer.

## Coordinate before changing

- Networking for NIC/loader state
- Storage for G1 transfers
- IRQ/G2 for copy engines
- Platform shutdown

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/gaps-sram-test` — candidate `make -C utils/gaps-sram-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/cdrom/direct-gaps-stage`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Research reconciliation remains open. No automatic hot-swap, EEPROM writes or destructive bridge diagnostics on a live owner.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
