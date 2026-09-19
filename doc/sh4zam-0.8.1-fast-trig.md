# SH4ZAM 0.8.1 u16 fast-math regression

Reproduced against the unmodified official tag `v0.8.1`, commit
`be71e8a1428374498e8f5e7506c7e244375f4399`, using SH-4 GCC 16.2.0 and Flycast
interpreter and dynarec modes. No physical Dreamcast result is claimed.

## Symptom

`shz_sincosu16()` disagrees with its strict FSCA implementation when the caller
is compiled with `-ffast-math`. The inputs below are cardinal angles in the
unsigned 16-bit full-turn representation.

| Input | Expected sin, cos | Observed fast-math sin, cos |
| --- | --- | --- |
| 0 | 0, 1 | 0, 1 |
| 16384 | 1, 0 | 0.07260841, 0.99736059 |
| 32768 | 0, -1 | 0.14483349, 0.98945600 |
| 49152 | -1, 0 | 0.21610682, 0.97636974 |

The normal strict integration fixture passes the same cardinal-angle checks.

## Source-level cause

In `include/sh4zam/inline/sh4/shz_trig_sh4.inl.h`, the new fast-math path in
`shz_sincosu16_sh4()` calls both compiler trig builtins with
`radians16 / SHZ_F_TAU`. Those builtins take radians. The equivalent full-turn
conversion is `radians16 * (SHZ_F_TAU / 65536.0f)`, not division by tau.
This report identifies the discrepancy; it does not modify upstream source or
claim that a proposed upstream patch has been validated. The release commit
explicitly describes using compiler-generated FSCA for improved register
allocation; that optimization is intentional. The inconsistency concerns the
angle conversion, not the decision to use compiler builtins.

The portable and constant-input u16 paths use 65535 as their full-turn divisor,
while the direct FSCA path uses 65536-unit turn encoding. That smaller endpoint
convention discrepancy is distinct from the fast-path division by tau and
should be clarified separately with upstream.

## Reproduction in this KOS checkout

```sh
source environ.sh
make -C examples/dreamcast/sh4zam/integration -B fast-trig-probe.elf
```

Run the resulting ELF with serial output enabled. `fast-trig-call.c` contains
only the library call and is compiled with `-ffast-math`. Its separate driver
is compiled with `-fno-fast-math`, checks finiteness and a `3e-4` absolute
tolerance, and prints the actual and expected pairs. The executable reports
`RESULT: FAIL (SH4ZAM fast-math u16 trig)` and returns failure. It is deliberately
not counted as a passing integration test.

## Integration policy

KOS's default flags do not enable fast-math, and the kernel has no call to this
u16 routine. The official 0.8.1 submodule remains unmodified. With this pin,
compile callers of `shz_sincosu16()` without `-ffast-math`/`-Ofast`, or explicitly
use the local helper below. This is not an upstream fix. The existing FFT `-O2`
adapter workaround is unrelated and remains necessary for the local toolchain.

No issue or message has been posted upstream by this change.

## Local workaround

KOS provides `kos_shz_sincosu16()` in `<kos/sh4zam.h>`. It converts the input
using `angle * (SHZ_F_TAU / 65536.0f)` and calls upstream `shz_sincosf()`.
The name and header make ownership explicit: SH4ZAM's source and original
API behavior are unchanged. Existing direct calls do not get redirected.

Build `u16-trig-test.elf` in the integration example to exercise the helper.
This is a passing regression lane, separate from the original failing
`fast-trig-probe.elf`. All 65536 input angles are checked against strict
double-precision trig with `3e-4` absolute tolerance, plus constant quarter-turn
calls. The helper retains approximate FSCA behavior rather than promising
bit-identical output across compiler modes or host libraries.

Validation with this pin and SH-4 GCC 16.2.0 passed in Flycast interpreter and
dynarec modes for all four compiler lanes. The largest observed component
error was `9.58878754e-5`. GCC 14 GNU17/C23 and Apple Clang GNU17/C2x portable
tests passed too, including Clang ASan/UBSan; their largest observed error was
`5.39436868e-7`. These are observed test results, not universal error bounds.

Disassembly of the SH-4 fast-math runtime helper contains FSCA, with float
conversion/scaling instructions before it and no out-of-line trig call. It
is not instruction-identical to feeding the original integer directly into
FSCA. Physical-hardware accuracy and performance have not been measured.
