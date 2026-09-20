# SH4ZAM KOS build adapter

SH4ZAM is developed by Falco Girgis and the SH4ZAM contributors at
https://github.com/gyrovorbis/sh4zam. Its upstream MIT license and author
notices are preserved unchanged. This directory supplies the KOS build
adapter and integration documentation; separate KOS helpers live under
`addons/include/kos/`. The library is not a KOS-authored fork.

## Source ownership and initialization

`upstream/` is a Git submodule pinned to
`0c1ccb5f5614314e36e2fec3179c8ca3ae844770` from
`https://github.com/restricted-628/sh4zam.git`, the exact commit submitted in
[upstream PR #70](https://github.com/gyrovorbis/sh4zam/pull/70). This temporary
pin is official `v0.8.1` (`be71e8a1428374498e8f5e7506c7e244375f4399`) plus
Falco's suggested fast-math u16 conversion fix and its regression tests. It
is **not an official release or a claim of upstream approval**. Upstream
history, author notices, and license are retained; no build-time source
patches are applied. Updating the dependency remains a separate reviewed
change, not an automatic checkout of a moving branch.

After PR #70 is merged, select and test an official commit containing the fix,
restore the official `.gitmodules` URL, and remove the exact-commit exception
in `utils/check-sh4zam-source.py`. Do not assume a squash/rebase merge will
retain this PR commit's SHA. The KOS-side helper can remain for compatibility.

Clone KOS with `--recurse-submodules`. For existing checkouts, or after changing
KOS revisions, run from the KOS root:

```sh
git submodule sync -- addons/libsh4zam/upstream
git submodule update --init --recursive
python3 utils/check-sh4zam-source.py
```

The checker verifies the Git index's pinned revision, clean submodule checkout,
official repository URL or this exact temporary fork/commit pair, public
header symlink, and unchanged license copy. Other fork revisions are rejected.
It replaces the former copied-source hash manifest and maintenance patch.
Ordinary GitHub source ZIPs and `git archive` do not embed submodule contents;
use a recursive clone, or explicitly include the pinned dependency when making
a complete source distribution. A missing checkout produces an actionable
addon build error rather than downloading code during compilation.

## KOS build and linkage

The library remains a KOS addon. `addons/include/sh4zam` is a relative symlink
to the submodule's headers, so existing `#include <sh4zam/...>` users do not
change. The KOS recipe builds the upstream sources into `.build/` outside the
submodule and produces `addons/lib/dreamcast/libsh4zam.a`.

The target build uses the optimized SH-4 backend and implicit TLS. The upstream
portable software backend remains available for host applications. KOS's
current default link group includes `-lsh4zam`; this migration does not make
the graphics stack optional or move it into a separate port.

KOS header/library search paths precede kos-ports. Preserve that ordering to
avoid mixing this pin with another installed SH4ZAM revision. KOS Doxygen
excludes symbolic links; SH4ZAM's own API documentation remains upstream rather
than being patched to fit KOS documentation groups.

## FFT compiler constraint

The former broad `memory`-clobber patch is removed. Upstream's explicit
read/write memory operands are used without alteration.

With the local SH-4 GCC 16.2.0 toolchain, the unchanged eight-point FFT fails
register allocation at `-O0` (`GENERAL_REGS`, impossible asm constraints).
The same source compiles at `-O1`, `-O2`, `-O3`, `-Os`, and `-Og`, using the
KOS GNU17, `-m4-single` environment. This establishes a configuration-specific
build limitation; it does not establish which configuration caused the
historical failure or quantify a performance difference.

The adapter defaults `SHZ_FFT_CFLAGS=-O2` for that translation unit only, after
the caller's general flags. This also permits an otherwise `-O0` debug build
without rewriting upstream assembly constraints. Debug information is retained;
optimized stepping may differ. To choose the tested debug-oriented alternative:

```sh
make -C addons/libsh4zam SHZ_FFT_CFLAGS=-Og
```

The `-O0` failure and `-O2` success were reproduced again with the unmodified
0.8.1 source. This release does not change the FFT translation unit; retain the
adapter workaround rather than modifying upstream memory operands.

Reproduction, after sourcing `environ.sh` (substitute any tested optimization):

```sh
kos-cc -O0 -std=gnu17 -Wextra -Werror -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT \
  -c addons/libsh4zam/upstream/source/sh4/shz_complex_sh4.c \
  -o /tmp/sh4zam-fft-probe.o
```

## 0.8.1 fast-math restriction and local helper

The official 0.8.1 `shz_sincosu16()` SH-4 fast-math path converts the 16-bit
turn angle with `radians16 / SHZ_F_TAU`. The strict path passes that angle
directly to FSCA. These are not equivalent: the corresponding radian angle is
`radians16 * (SHZ_F_TAU / 65536.0f)`.

The separate `examples/dreamcast/sh4zam/integration/fast-trig-probe.elf`
reproducer compiles only the call under `-ffast-math`, with a strict validator.
Under GCC 16.2/Flycast, angle 16384 returns approximately `(0.07260841,
0.99736059)` instead of the quarter-turn pair `(1, 0)` on official 0.8.1.
With the temporary PR #70 pin it is now expected to report `RESULT: PASS`;
the test's expected values and tolerance have not changed. The default example
build includes this direct-API regression. It is not a physical-hardware test.

The default KOS environment does not enable `-ffast-math`, and no current KOS
kernel caller uses this u16 pair routine. The temporary PR pin fixes the direct
SH-4 fast-math call, so it no longer requires disabling `-ffast-math`/`-Ofast`.
If reverting to the unmodified 0.8.1 tag, that restriction applies again.
The previously provided KOS-side helper remains available:

```c
#include <kos/sh4zam.h>
shz_sincos_t pair = kos_shz_sincosu16(angle);
```

This helper converts 65536 turn units to radians and calls the public
`shz_sincosf()` API. It supports fast-math without modifying upstream source,
replacing upstream symbols, or changing project-wide compiler flags. Including
the header alone does **not** redirect calls to `shz_sincosu16()`; the current
submodule pin supplies that function's fix. No kernel calls need migration.

The helper consistently uses 65536 units per turn for constant/runtime inputs
on all backends. Upstream's software and constant-u16 paths use 65535 instead;
this small convention difference is intentional in the KOS helper to match
the SH-4 turn encoding. Results remain approximate, not bit-identical across
backends. The target test allows `3e-4` absolute error; this is a regression
threshold, not a physical-hardware accuracy guarantee.

The separate `u16-trig-test.elf` checks every 16-bit angle against a strict,
double-precision reference in C strict/fast-math/Ofast and C++ fast-math modes,
plus a constant quarter-turn in each mode. It remains separate from the
direct-API regression so both entry points continue to be covered.

See the [standalone regression report](../../doc/sh4zam-0.8.1-fast-trig.md)
for cardinal-angle results and reproduction instructions.

## 0.8.1 upgrade validation

The entire KOS target build was forced with SH-4 GCC 16.2.0 to rebuild inline
header consumers as well as the addon archive. Header/library version checks
and the complete SH4ZAM integration fixture passed in Flycast interpreter and
dynarec modes, including the new release-specific memory/scale/screen probes.
The rendered toon/outline and Compact scene examples passed in dynarec mode.
Portable animation tests passed GCC 14 GNU17 and strict C23, Apple Clang strict
C2x, and Clang ASan/UBSan; host scene and converter regressions also passed.
The fast-math diagnostic failed on that original release; the subsequent
temporary PR pin fixes it as described above. No physical-hardware
certification is implied.

The temporary PR #70 pin was separately validated on 2026-09-19: the addon
and all integration executables were force-rebuilt with SH-4 GCC 16.2.0.
The main integration fixture and direct fast-math probe passed in both
Flycast interpreter and dynarec modes. The KOS helper's 65536-angle sweep
passed all four compiler lanes on GCC 14 host and Flycast dynarec. A fresh
clone fetched the exact dependency commit, and source-policy checks passed.
This scoped revalidation did not repeat the full KOS kernel build or run on
physical hardware.

## Floating-point context

SH4ZAM routines using XMTRX clobber the floating-point register back bank.
Retaining that matrix across a cooperative yield requires the fiber runtime's
`KFIBER_ATTACH_MATH_CONTEXT` option, which saves/restores a 64-byte XMTRX.
It does not supply a separate FPSCR, FPUL, or TLS environment per fiber.
Restore temporary FPU mode and exception-enable changes before yielding.
KOS threads preserve floating-point state through their scheduler.

Build, host, and emulator checks are not physical-hardware performance or
numerical certification. The broader KOS-consumer math audit remains separate
from this source-dependency migration.
