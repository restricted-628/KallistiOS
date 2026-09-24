# Complete fiber bundle validation

This records software validation of the complete SH4ZAM addon bundle,
not physical hardware proof. The build began in a fresh isolated worktree of
integrated SDK `80be9175`, with pinned submodules initialized inside it.
No prebuilt KOS archive from another worktree was copied into the build.

- Full SH-4 GCC 16.2.0 SDK build passed.
- `make fiber-bundle-check` built core probes and RAM/VRAM/contract examples
  with `-Werror`; archive symbols and all three disc link maps select exactly
  one provider. No fiber runtime definitions are in libkallisti; the full provider is in libfiber_sh4zam.
- Five core-context/API/synchronization/math/teardown probes passed in both
  Flycast interpreter and dynarec modes. The disc adapter's real-queue test
  passed 135 checks, three sibling steps and seven callbacks in both modes.
- Service Executor probe, synchronization and queue tests passed in both modes.
  The standalone addon audit passed eight forced-provider link maps and found
  no KOS/libm math imports. ADX-fiber and Compact asset service examples linked.
- GAPS ownership host tests passed (32863 checks, mostly SRAM byte assertions).
- LZ4 host tests passed, including the executor wrapper's scheduler-double tests.
- A generated 32 KiB RGB565 texture was read by direct disc DMA into guarded
  VRAM, verified and rendered to completion in both emulator modes. A corrupted
  texture was rejected before rendering in both modes. No screenshot/visual
  fidelity claim is made.
- A deliberately mixed-provider relocatable link failed with duplicate
  `fiber_attach_ex` definitions, as expected.

During packaging, an installed sysroot KOS-header symlink could satisfy an
absent Service Executor header. The core bundle now deliberately rejects that
header and excludes executor-only consumers. Older examples also requested
strict C11, which rejects SH4ZAM's GNU inline assembly; fiber examples now use
the SDK's GNU17 policy. The final checks above passed after those corrections.

Emulator tests used Flycast with REIOS, private settings, no user BIOS/media,
and 16 MiB RAM. Physical hardware, BBA DMA/driver handoff timing, 32 MiB modes,
MMU-specific behavior and performance remain separate gates.
