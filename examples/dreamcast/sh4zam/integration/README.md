# First-class SH4ZAM integration

This example verifies that the bundled SH4ZAM 0.8 library is available through
the normal KOS include and link environment. Its Makefile deliberately does not
add `-lsh4zam`; the standard grouped KOS libraries supply the implementation and
discard unused sections normally.

Before running graphics math, the example creates threads under both supported
rounding modes with denormal flushing enabled. It checks FPSCR inheritance,
child mode preservation across scheduling and isolation from the creator.
This exercises KOS master’s new-thread context fix without intentionally
triggering an FPU exception. It does not change the fiber contract: lightweight
fibers share their carrier thread's FP modes, while the opt-in math attachment
preserves XMTRX only.

The program also exercises the alias-safe bridge between established KOS
matrix/vector types and SH4ZAM types. New performance-sensitive graphics code
should keep SH4ZAM types throughout its transform pipeline and convert only at
an established API boundary.

The shared `utils/animation-test/matrix-fixtures.h` fixture also checks:

- Non-unit quaternions, rotation order, nonuniform/negative/zero scales, and
  translation against independently tabulated TRS matrices.
- Oblique camera poses with zero, positive, negative, and quarter-turn roll
  against a scalar double-precision reference.
- Matrix composition with separate output, output aliasing either input, and
  all three arguments aliasing the same matrix.
- Rejected inputs leave output unchanged. Every checked operation leaves a
  distinct 16-lane XMTRX sentinel unchanged.

The target tolerance is `3e-4 * max(1, abs(expected))` per matrix element; the
host fixture uses `3e-5`. Passing this finite fixture is not a bound over all
possible inputs. Success prints an additional line:

```text
SH4ZAM TRS, rolled camera, compose aliasing, XMTRX: PASS
```

Finally, it attaches the current thread with
`KFIBER_ATTACH_MATH_CONTEXT` and proves that the main fiber and a child fiber
retain independent XMTRX matrices across two cooperative transfers. Lightweight
fibers remain the default for applications that do not need this preservation.

The test also exercises the checked camera builders, frustum classification,
clipping, canonical vertex projection, and bounded compact-model emission
through their SH4ZAM target paths. It verifies that the caller's prior XMTRX
matrix is restored after complete and rejected geometry projection and remains
untouched by compact-model and one-off camera/frustum operations.

It additionally builds and admits an ordinary draw cache, compares its output
byte-for-byte with the checked immediate path, and rejects both a NaN produced
by a per-frame callback and zero homogeneous W without publishing a strip.
XMTRX is checked after each successful or rejected admitted draw. Success adds:

```text
Admitted Compact draw, rejection, XMTRX: PASS
```

The two-volume fixture covers both 32-byte color and 64-byte textured caches,
with and without a prepare callback. It compares checked/admitted output,
including buffer guards, and verifies the full temporary-union callback
contract for packed color packets. NaN callback output and zero W are rejected
without sink publication; XMTRX remains unchanged. Success adds:

```text
Admitted two-volume color/textured draws, rejection, XMTRX: PASS
```

Successful completion prints:

```text
RESULT: PASS (SH4ZAM 0.8 camera, frustum, geometry, and fibers)
```

The same result is shown on a green framebuffer for emulator or hardware
validation that does not provide a debug console.

On 2026-08-23, the camera, frustum, geometry, and fiber paths produced the
expected result in Flycast with both the SH-4 interpreter and dynarec. The
compact-model extension subsequently passed in Flycast on 2026-09-08 with
explicit interpreter and dynarec selections after the clean GCC 16.2.0 KOS
build incorporating official master through `33c6e0ba`. Both runs printed the
result above, covering model emission and matrix restoration as well as the
earlier checks. This is an emulator correctness fixture, not a physical-device
numerical or performance certification.
