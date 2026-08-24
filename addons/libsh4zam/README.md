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
They must not span a lightweight fiber yield unless that fiber explicitly owns
a full floating-point context. KOS threads preserve their floating-point state.
