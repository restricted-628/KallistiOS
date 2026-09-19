# Bundled SH4ZAM

KallistiOS builds SH4ZAM 0.8.0 as a first-class Dreamcast math component.
The public C and C++ headers are installed under `addons/include/sh4zam`, and
the static library is produced as `addons/lib/dreamcast/libsh4zam.a`.

This is a KOS addon build of vendored upstream files, not a Git submodule.
The addon build location and the source dependency mechanism are separate
choices. Upstream authorship remains with Falco Girgis and SH4ZAM contributors;
KOS integration files and the local patch inventory are maintained separately.

The implementation is maintained as a distinct attributed component under
its MIT license. KOS-facing graphics facilities may use it directly while
preserving established KOS entry points where source or binary compatibility
requires them.

The target build uses the optimized SH-4 backend. The software backend remains
available to independent host-side validation, but it is not built into normal
Dreamcast applications.

SH4ZAM routines that use XMTRX clobber the floating-point register back bank.
Retaining that matrix across a cooperative yield requires the fiber runtime's
`KFIBER_ATTACH_MATH_CONTEXT` option, which saves/restores a 64-byte XMTRX.
It does not supply a separate FPSCR, FPUL, or TLS environment per fiber.
Restore any temporary FPU mode or exception-enable changes before yielding.
KOS threads preserve their floating-point state through their scheduler.

## Source revision and local maintenance

Upstream: https://github.com/gyrovorbis/sh4zam

Verified upstream revision (2026-09-17):
`0bacf4b336368c0b47864ce9eeb59e7c07904b51` (0.8.0).
This adds SH-4 ABI-based backend detection (`__SH4_SINGLE__` or
`__SH4_SINGLE_ONLY__`) instead of requiring `__DREAMCAST__`. It preserves the
optimized Dreamcast path and also recognizes other compatible SH-4 builds.
Upstream's separate GitLab emulator-test setting is not a bundled source file;
our emulator checks remain integration checks, not numerical hardware proof.
The bundled source incorporates this revision with these local changes:

- Whitespace normalization, balanced Doxygen conditional regions, and a
  distinct memory documentation group to avoid colliding with KOS groups.
- C++ quaternion documentation/conditional-boundary correction.
- Local GCC 16 FFT assembly operand-pressure workaround: pointer inputs plus
  a broad memory clobber replace explicit read/write memory operands. The
  assembly instruction sequence is unchanged, but the compiler's memory
  dependency information differs. This is not a demonstrated performance
  improvement. Reproduction of the original compiler failure and comparison
  with upstream constraints remain required before retaining or revising it.
  The patch is still local as of the verified upstream revision (KOS commit
  `6df19052`).

`source-lock.json` records upstream and bundled SHA-256 hashes for every
vendored header, source file, and license. `local-changes.patch` records the
upstream-to-bundle text delta using upstream-relative paths, with line endings
normalized for review; the hashes retain exact byte identity. The KOS build
recipe and this integration guide are maintained separately from that delta.
Run `python3 utils/check-sh4zam-source.py` from the KOS root to check the
bundled hashes. Supplying `--upstream PATH` also verifies the pinned upstream
checkout and its hashes. After reviewing an intentional update, use
`--record --upstream PATH` to regenerate both records.

The bundled headers and archive are authoritative for this branch. Default
`environ_base.sh` search paths put both before kos-ports. Applications must
preserve this order; overriding only one side can mix different revisions.
