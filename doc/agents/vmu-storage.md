# VMU storage, packages and display

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform + storage**.

## Scope and entry points

VMU block/bank protocol, filesystem validation/maintenance/async requests, packages and LCD helpers.

- `kernel/arch/dreamcast/fs/vmufs.c`
- `kernel/arch/dreamcast/fs/vmufs_request.c`
- `kernel/arch/dreamcast/fs/vmufs_maintenance.c`
- `kernel/arch/dreamcast/fs/vmufs_validate.c`
- `kernel/arch/dreamcast/fs/fs_vmu.c`
- `kernel/arch/dreamcast/hardware/maple/vmu.c`
- `kernel/arch/dreamcast/hardware/maple/vmu_bank_protocol.c`
- `kernel/arch/dreamcast/util/vmu_pkg_codec.c`
- `kernel/arch/dreamcast/util/vmu_fb.c`

Use the public headers and example contracts; no complete standalone component guide was established in this inventory.

## Preserve these boundaries

- Keep transport completion, directory/FAT mutation and VFS lifetime distinct. Trace partial write/cancel/failure results rather than assuming a transaction.
- Validate image geometry, chains, ranges, bank selection and metadata before mutation. Do not test repair/format on user save media without explicit approval.
- Retain device/request buffers across Maple callbacks and teardown; serialize maintenance with ordinary I/O.
- Preserve package CRC/encoding and timestamp conventions. LCD and storage share a device but are not one API contract.

## Coordinate before changing

- Maple transport
- VFS and storage
- Clocks/RTC for timestamps

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/vmu-bank-test` — candidate `make -C utils/vmu-bank-test test` in an assigned checkout.
- `utils/vmu-storage-test` — candidate `make -C utils/vmu-storage-test test` in an assigned checkout.
- `utils/vmufs-image-test` — candidate `make -C utils/vmufs-image-test test` in an assigned checkout.
- `utils/vmufs-maintenance-test` — candidate `make -C utils/vmufs-maintenance-test test` in an assigned checkout.
- `utils/vmufs-validate-test` — candidate `make -C utils/vmufs-validate-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/vmu/vmufs-safety`
- `examples/dreamcast/vmu/vmu_pkg`
- `examples/dreamcast/vmu/vmu_lcd`
- `examples/dreamcast/vmu/vmu_clock`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Image-model tests do not prove physical VMU media integrity, power-loss behavior or device timing.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
