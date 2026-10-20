# Direct-disc file residency probe

Build with the selected complete fiber provider and its matching KOS environment.
The integration source is the original deterministic `VM4DATA.BIN` file in an
actual ISO9660 disc image. The guest explicitly selects and verifies
`FS_ISO9660_BACKEND_DIRECT` before opening `/cd/VM4DATA.BIN`. There is no BIOS
backend or RAM-filesystem fallback. Direct mount/read failures fail the probe.

With a sourced KOS environment, `make` builds the ELF and `make disc
DISC_DIR=/absolute/new/directory` also builds a raw binary and an ISO9660/CUE
fixture. The image builder requires macOS `hdiutil` and the built KOS
`utils/scramble/scramble`. It generates the 20,000-byte payload, scrambles the
probe binary, normalizes ISO file version suffixes and writes original minimal
boot metadata/code. Run the resulting `disc.cue`, not the standalone ELF, in the
existing REIOS runtime so the guest has a mounted optical image. The small
bootstrap jumps to the executable loaded by REIOS; it is not a physical-console
boot/license bootstrap. Hardware testing needs an appropriate independently
prepared bootable disc with the same payload and program.

A successful guest must print all of the following; process exit is insufficient:

```
VM4DISC BACKEND direct=1 source=/cd/VM4DATA.BIN
VM4DISC SOURCE_OPEN /cd/VM4DATA.BIN
VM4DISC RESULT failures=0 translation=1 loads=6 yields=2 blocked=1 abandoned=1 transfers=1 reloads=1 wrapped_reads=4 closes=2 direct=1
```

The first three page loads use `vm_file_open` directly on `/cd/VM4DATA.BIN`.
They check exact translated bytes from an unaligned source offset, final zero
padding, pin exclusion and eviction. Subsequent concurrency tests use a small
VFS adapter that adds a controlled synchronous delay and delegates all actual
open/stat/seek/read/close operations to the same `/cd` file. They check carrier
blocking with an independent KOS thread providing progress, a READ pin across
child yield, cancellation while an external worker reads, destruction of a
separate waiting child, acknowledged ownership after producer thread exit, and
reload from the retained open. `wrapped_reads` counts only adapter reads, not
drive commands or filesystem cache misses. Synthetic delay is not device timing.

No mount removal is performed. The application keeps the optical medium stable
and joins consumers/workers before close, space destruction and context reuse.
Filesystem caches may satisfy a reload; VM eviction does not force a physical
read. This probe does not claim ISO mount drain, media replacement, physical DMA/
cache/failure retirement, hardware timing, loader return or physical teardown.

`-DVM_FILE_FATAL=1` builds a separate absent-page diagnostic after a successful
source open. It should print `VM4DISC FATAL_ARMED reads=0` followed by the normal
unhandled MMU exception and abort, with no wrapped page read or success result.
Source opening itself can perform mount/metadata I/O. The existing host and
previous fatal probes remain separate evidence for the resolver boundary.

The 8301-byte reservation touches two page-table spans and permits two resident
pages. Its measured SH-4 VM envelope is 24,716 bytes (fixed 4232, table reserve
12,292, backing capacity 8192); the configured ceiling is 40,000. The payload
is not embedded as a resident image. VFS/ISO handles, filesystem/driver caches
and buffers, provider state, the 8192-byte child stack, default 32,768-byte worker
stacks, allocator overhead and shared context are external costs. This is not a
whole-system RAM cap or proof of a measured memory reduction.

Run under both mutually exclusive providers. The probe does not use a Service
Executor or introduce cooperative VM waiting. If the existing runtime cannot
exercise direct optical I/O, retain its exact failure and leave disc acceptance
open; do not substitute RAM-backed results or modify the emulator to pass.
