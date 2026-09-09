# Bundled SH4ZAM

KallistiOS builds SH4ZAM 0.8.0 as a first-class Dreamcast math component.
The public C and C++ headers are installed under `addons/include/sh4zam`, and
the static library is produced as `addons/lib/dreamcast/libsh4zam.a`.

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

Verified upstream revision (2026-09-08):
`ad353dc2cea596a7c8c7b56b05cbe2e07b84ed4a` (0.8.0).
Fetching upstream on this date found no newer commit. The bundled source
already incorporates this revision's implementation with these local changes:

- Whitespace normalization, balanced Doxygen conditional regions, and a
  distinct memory documentation group to avoid colliding with KOS groups.
- C++ quaternion documentation/conditional-boundary correction.
- GCC 16 FFT assembly operand-pressure fix: pointer inputs plus a memory
  clobber replace redundant read/write memory operands. The arithmetic and
  instruction sequence are unchanged. This patch is still local as of the
  verified upstream revision (KOS commit `6df19052`).

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
