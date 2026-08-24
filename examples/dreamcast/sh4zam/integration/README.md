# First-class SH4ZAM integration

This example verifies that the bundled SH4ZAM 0.8 library is available through
the normal KOS include and link environment. Its Makefile deliberately does not
add `-lsh4zam`; the standard grouped KOS libraries supply the implementation and
discard unused sections normally.

The program also exercises the alias-safe bridge between established KOS
matrix/vector types and SH4ZAM types. New performance-sensitive graphics code
should keep SH4ZAM types throughout its transform pipeline and convert only at
an established API boundary.

Finally, it attaches the current thread with
`KFIBER_ATTACH_MATH_CONTEXT` and proves that the main fiber and a child fiber
retain independent XMTRX matrices across two cooperative transfers. Lightweight
fibers remain the default for applications that do not need this preservation.

The test also exercises the checked camera builders, frustum classification,
clipping, canonical vertex projection, and bounded compact-model emission
through their SH4ZAM target paths. It verifies that the caller's prior XMTRX
matrix is restored after complete and rejected geometry projection and remains
untouched by compact-model and one-off camera/frustum operations.

Successful completion prints:

```text
RESULT: PASS (SH4ZAM 0.8 camera, frustum, geometry, and fibers)
```

The same result is shown on a green framebuffer for emulator or hardware
validation that does not provide a debug console.

On 2026-08-23, the camera, frustum, geometry, and fiber paths produced the
expected result in Flycast with both the SH-4 interpreter and dynarec. The
compact-model extension has completed its GCC 16.2.0 target build and still
requires an updated emulator run.
