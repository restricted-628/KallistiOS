# Complete fiber bundle validation

## VBlank addition (2026-09-24)

- Integrated master `cb73cbc2` adds compatible IRQ priorities and the optional
  `libfiber_vblank` adapter. The standalone upstream IRQ candidate is
  `pr/vblank-priority-safety` at `c05800f3`, based on upstream `804b3195`.
- Both complete bundles rebuilt with SH-4 GCC 16.2.0. The bundle checker builds
  the VBlank examples and audits their link maps for exactly one fiber provider.
  The addon provider has no KOS/libm math imports; the core has no executor.
- IRQ ordering and application-fiber dispatch passed Flycast interpreter and
  dynarec tests in both bundles. The addon Service Executor variant also passed.
  Deferred order was `13241324`; a mid-callback pause coalesced five or six
  frames into one next batch without overtaking or an unbounded queue.
- The shared adapter host suite passed 135 checks, including saturation and
  pending-work cancellation. GCC 14 GNU17/strict C23, Clang GNU17/C2x and
  Clang ASan/UBSan were exercised alongside the IRQ-list regressions.
- Math-context regressions were rerun on both providers; the addon service
  probe additionally exercised IRQ wake, deadlines and shutdown with XMTRX.
- No physical Dreamcast validation or timing guarantee is claimed.

## Original bundle baseline

This records software validation of the complete core bundle,
not physical hardware proof. The build began in a fresh isolated worktree of
integrated SDK `80be9175`, with pinned submodules initialized inside it.
No prebuilt KOS archive from another worktree was copied into the build.

- Full SH-4 GCC 16.2.0 SDK build passed.
- `make fiber-bundle-check` built core probes and RAM/VRAM/contract examples
  with `-Werror`; archive symbols and all three disc link maps select exactly
  one provider. No Service Executor definitions are in libkallisti.
- Five core-context/API/synchronization/math/teardown probes passed in both
  Flycast interpreter and dynarec modes. The disc adapter's real-queue test
  passed 135 checks, three sibling steps and seven callbacks in both modes.
- The adapted core-only LZ4 tests retain allocation, admission, dictionary,
  budget and failure checks without compiling the absent executor wrapper.
  Clang GNU17, GCC 14 strict C23 and Clang ASan/UBSan runs passed.
- GAPS ownership host tests passed (32863 checks, mostly SRAM byte assertions).
- LZ4 host tests passed.
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
