# SH4ZAM 0.8.1 u16 fast-math regression

Reproduced against the unmodified official tag `v0.8.1`, commit
`be71e8a1428374498e8f5e7506c7e244375f4399`, using SH-4 GCC 16.2.0 and Flycast
interpreter and dynarec modes. No physical Dreamcast result is claimed.

**Current KOS status:** official v0.9.0, commit
`0fd3a1e1fa0809d33198c062632b1494ec2f57df`. The temporary
[PR #70](https://github.com/gyrovorbis/sh4zam/pull/70) fork pin has been retired.
The new release uses runtime SH-4 inline FSCA even under fast-math and fixes
the portable/constant paths to use 65536 units per turn. The failure results
below are historical and apply to the exact original 0.8.1 commit above;
upstream subsequently moved that tag. See [0.9.0 integration](sh4zam-0.9.0-upgrade.md).

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
The initial report identified the discrepancy without changing upstream.
The subsequent PR applies Falco's exact proposed expression,
`radians16 * (SHZ_F_TAU / (float)(UINT16_MAX + 1))`. The release commit
explicitly describes using compiler-generated FSCA for improved register
allocation; that optimization is intentional. The inconsistency concerns the
angle conversion, not the decision to use compiler builtins.

The original portable and constant-input u16 paths used 65535 as their full-turn divisor,
while the direct FSCA path uses 65536-unit turn encoding. That smaller endpoint
convention discrepancy is distinct from the fast-path division by tau and
was also fixed upstream in v0.9.0.

## Reproduction in this KOS checkout

```sh
source environ.sh
make -C examples/dreamcast/sh4zam/integration -B fast-trig-probe.elf
```

Run the resulting ELF with serial output enabled. `fast-trig-call.c` contains
only the library call and is compiled with `-ffast-math`. Its separate driver
is compiled with `-fno-fast-math`, checks finiteness and a `3e-4` absolute
tolerance, and prints the actual and expected pairs. On official 0.8.1 it
reports `RESULT: FAIL (SH4ZAM fast-math u16 trig)`. With the current release it
must report `RESULT: PASS (SH4ZAM fast-math u16 trig)`. The probe is now built
by the default integration example target; expectations were not relaxed.

## Integration policy

KOS's default flags do not enable fast-math, and the kernel has no call to this
u16 routine. The current submodule is a clean checkout of official v0.9.0,
not a dirty or build-time-patched release. Direct SH-4 calls to
`shz_sincosu16()` now support fast-math. If reverting to the original 0.8.1 commit,
disable `-ffast-math`/`-Ofast` for those callers or use the local helper below.
The FFT `-O2` adapter workaround is unrelated and remains necessary.

The issue was reported to Falco and PR #70 was submitted under `restricted-628`.
The v0.9.0 upgrade returns to the official repository and removes the
temporary exact-commit exception from the source checker.

## Local workaround

KOS provides `kos_shz_sincosu16()` in `<kos/sh4zam.h>`. It converts the input
using `angle * (SHZ_F_TAU / 65536.0f)` and calls upstream `shz_sincosf()`.
The name and header make ownership explicit: the helper itself neither edits
SH4ZAM nor redirects existing direct calls. It remains available for
compatibility even though the official dependency now fixes the direct API.

Build `u16-trig-test.elf` in the integration example to exercise both APIs.
This is a passing regression lane, separate from the direct API's
`fast-trig-probe.elf`. All 65536 input angles are checked against strict
double-precision trig with `3e-4` absolute tolerance for the helper and `1e-6`
for the upstream API, plus constant quarter-turn
calls. The helper retains approximate FSCA behavior rather than promising
bit-identical output across compiler modes or host libraries.

Validation of this helper with official 0.8.1 and SH-4 GCC 16.2.0 passed in
Flycast interpreter and dynarec modes for all four compiler lanes. The largest observed component
error was `9.58878754e-5`. GCC 14 GNU17/C23 and Apple Clang GNU17/C2x portable
tests passed too, including Clang ASan/UBSan; their largest observed error was
`5.39436868e-7`. These are observed test results, not universal error bounds.

Disassembly of the SH-4 fast-math runtime helper contains FSCA, with float
conversion/scaling instructions before it and no out-of-line trig call. It
is not instruction-identical to feeding the original integer directly into
FSCA. Physical-hardware accuracy and performance have not been measured.
