# Flash configuration and persistent settings

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform + networking**.

## Scope and entry points

Flash partitions/records, system configuration, play history and network-settings platform boundaries.

- `kernel/arch/dreamcast/hardware/flashrom.c`
- `kernel/arch/dreamcast/hardware/flashrom_layout.c`
- `kernel/arch/dreamcast/include/dc/flashrom.h`

Relevant existing documentation (dated claims must be rechecked):

- `doc/flash-configuration-audit.md`

## Preserve these boundaries

- Keep inspection/parsing separate from mutation. Malformed input must never trigger automatic repair, erase or append.
- Validate record geometry, CRC, versions, bitmap gaps and latest-record selection; preserve unknown payload fields.
- A multi-record append is not atomic; report partial/ambiguous completion and power-loss limits.
- Network settings stay a platform service. Do not conflate console flash, VMU settings and NIC EEPROM, or automatically write any of them.

## Coordinate before changing

- Networking for configuration translation
- RTC for timestamps
- Platform BIOS access

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/flashrom-layout-test` — candidate `make -C utils/flashrom-layout-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/flashrom/read-only`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Only synthetic/host fixtures during default validation; persistent hardware writes require explicit scope.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
