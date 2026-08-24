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

Successful completion prints:

```text
RESULT: PASS (SH4ZAM 0.8 and opt-in XMTRX fibers)
```

The same result is shown on a green framebuffer for emulator or hardware
validation that does not provide a debug console.
