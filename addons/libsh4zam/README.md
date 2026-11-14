# SH4ZAM KOS build adapter

SH4ZAM is developed by Falco Girgis and the SH4ZAM contributors at
https://github.com/gyrovorbis/sh4zam. Its upstream MIT license and author
notices are preserved unchanged. This directory supplies the KOS build
adapter and integration documentation; separate KOS helpers live under
`addons/include/kos/`. The dependency uses pristine upstream source.

## Source ownership and initialization

`upstream/` is a Git submodule pinned to
`5b7dfc22a91f7a21756f1e8f86b1ed3d99dd33bf`, upstream **v0.10 update**, from
`https://github.com/gyrovorbis/sh4zam.git`. Upstream history, author notices,
and license are retained; no local source patches are applied. Updating
the dependency remains a separate reviewed change, not an automatic checkout
of a moving branch.

This advances `ad2f8909`. It adds combined Euler and 16-bit-turn
XMTRX rotations, quaternion initializer/apply optimizations, register permutations,
scalar fixes and section-level assembly organization. The initial update retained
the alignment overlay; its later retirement is described below. This update does
not enable new consumer algorithms or claim hardware speedups. Clean GCC16.2 builds, host
regressions/sanitizers, consumer/provider links and selected Flycast tests pass.
Combined rotations, quaternion init/apply, control-mode preservation and native
bitwise permutations pass at O0 and O2 in interpreter and dynarec modes.
Physical hardware and performance qualification remain separate.

Callers must retain explicit angle types in C++ because float and uint16_t
overloads make an untyped integer literal ambiguous. Rotation extraction is not
a general scaled-TRS decomposition. Matrix negation now uses multiplication in
some lanes; do not assume sign-bit-only behavior for special floating values or
unchanged FPSCR status. No production consumer is switched to these operations.
The fork retains this upstream negation implementation by explicit decision.

Previously, `ad2f8909` advanced `9a214a2`. The adapter builds separate SH-4 and software
matrix translation units; their shared implementation fragment is not compiled
independently. The public matrix alignment correction is now upstream; the
residual raw-buffer/software-state patch has since been retired.
On SH-4, matrix size remains 64 bytes but alignment changes from 16 to 8;
`pvr_skin_prepared_joint_t` changes from 112 to 104 bytes. Cleanly rebuild all
libraries and applications. Native prepared palettes are not serialized assets.
This update does not opt consumers into new math algorithms or fast-math.

Previously, `9a214a2` advanced `c18faab`, adding XYZ/ZYX/ZXY/YXZ quaternion constructors in
radians and 16-bit turns, and correcting XYZ Euler extraction. The deprecated
`shz_quat_from_angles_xyz()` now forwards to `shz_quat_from_rotation_xyz()`;
its composition changes from Qy*Qx*Qz to the documented Qx*Qy*Qz. No tracked
KOS consumer calls the deprecated constructor. The sprite-cell consumer uses
Z-only quaternions, for which the corrected Z extraction retains its meaning.
External users of mixed-axis rotations must review their convention rather
than treating this as a name-only deprecation.

Previously, `c18faab` advanced `7747e5d`, adding weighted 3x4 XMTRX blending,
software-backend inverse-trigonometry fixes, target/TLS fixes and the new upstream documentation
site. The existing `shz_xmtrx_blend()` name remains a deprecated wrapper for
`shz_xmtrx_blend_4x4()`. The 3x4 variant preserves the fourth row; callers must
initialize the accumulator appropriately. No graphics consumer is switched to
the new blending algorithm by this dependency update.

Use <https://sh4zam.com/for-agents/> and its symbol index for API contracts;
check the site's source revision against this pin. Alignment, fiber context
and lifetime rules below still apply.

Earlier, `7747e5d` advanced the official pin `ae8d4c1`. That pin replaced PR #70
`0c1ccb5f5614314e36e2fec3179c8ca3ae844770`. The official release uses inline
FSCA for runtime SH-4 u16 calls even with fast-math, and fixes the portable
and constant-input paths to use 65536 units per turn. The temporary fork URL
and checker exception have been removed. `kos_shz_sincosu16()` remains for
source compatibility; new code can call the upstream API directly.

The earlier v0.8.1 tag changed upstream after our original integration. Old
reports identify the exact tested commit, `be71e8a1428374498e8f5e7506c7e244375f4399`,
and must not be interpreted as results for the subsequently moved tag.

Clone KOS with `--recurse-submodules`. For existing checkouts, or after changing
KOS revisions, run from the KOS root:

```sh
git submodule sync -- addons/libsh4zam/upstream
git submodule update --init --recursive
python3 utils/check-sh4zam-source.py
```

The checker verifies the Git index's reviewed pinned revision, pristine source,
official repository URL, public header symlink, and unchanged license copy.
Fork URLs, unexpected tracked changes and untracked source files are rejected.
Verification and builds never modify the dependency. The former
`--apply-local-patch` option is removed. For a checkout with the old overlay,
preserve its diff and recover `addons/libsh4zam/patches/matrix-alignment.patch`
from the KOS commit that installed it. Only reverse that exact patch after
confirming it is the entire local delta and that a reverse-apply check passes.
If other edits are present, reconcile them separately; do not reset or clean
the submodule. Rebuild libraries and applications after retirement. No upstream
history is rewritten and the dependency pin is unchanged.
Ordinary GitHub source ZIPs and `git archive` do not embed submodule contents;
use a recursive clone, or explicitly include the pinned dependency when making
a complete source distribution. A missing checkout produces an actionable
addon build error rather than downloading code during compilation.

## Alignment policy and retired patch

The original public typedef-versus-struct alignment bug was fixed upstream.
On 2026-11-13 the owner approved retiring the residual raw-buffer/software-state
patch after clean-upstream compiler and host regression checks. No current
execution failure requiring it was reproduced; retirement is not a speedup claim.
Native matrix objects use their type's alignment. Host SIMD layouts can differ
from SH-4, so do not infer a portable native layout from the target's 8-byte
matrix alignment.

Upstream raw-buffer dispatch and assembly are used unchanged. Two source-level
concerns remain recorded: raw transpose wrappers cast float buffers to a stricter
matrix pointer type, and software-state typedef/tag alignment differs on the
tested host. Current tests do not establish safety for every compiler or future
optimization. Keep the regressions and reopen a specific reproducer if one arises,
not a blanket vendor patch. Matrix storage remains 64 bytes; do not serialize
native prepared structures or mix old/new archives.

The release fixture covers float-buffer offsets 0, 4, 8 and 12 modulo 16 for
load/store, transpose and forward/reverse multiplication against a scalar
reference with guards. Host checks do not execute the SH-4 assembly paths;
target execution and physical-hardware validation remain separate.

## KOS build and linkage

The library remains a KOS addon. `addons/include/sh4zam` is a relative symlink
to the submodule's headers, so existing `#include <sh4zam/...>` users do not
change. The KOS recipe builds the upstream sources into `.build/` outside the
submodule and produces `addons/lib/dreamcast/libsh4zam.a`.

The target build defaults to the optimized SH-4 backend and implicit TLS. The
archive also includes upstream's software matrix, XMTRX and FFT objects, allowing the
software backend to be selected on SH-4. Host tests
explicitly select pthread TLS for the portable backend. KOS's
current default link group includes `-lsh4zam`; this migration does not make
the graphics stack optional or move it into a separate port.

KOS header/library search paths precede kos-ports. Preserve that ordering to
avoid mixing this pin with another installed SH4ZAM revision. KOS Doxygen
excludes symbolic links; SH4ZAM's own API documentation remains upstream rather
than being patched to fit KOS documentation groups.

## FFT compiler constraint

The former broad `memory`-clobber patch is removed. Upstream's explicit
read/write memory operands are used without alteration.

With the local SH-4 GCC 16.2.0 toolchain, the historical eight-point FFT failed
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

The `-O0` failure and `-O2` success were reproduced with the original 0.8.1
source. The v0.10 candidate compiles at `-O0` in the update review, but removing
the override requires a separate optimization-level and numerical/state check.
This compatibility update retains it rather than changing debug-build behavior.

Reproduction, after sourcing `environ.sh` (substitute any tested optimization):

```sh
kos-cc -O0 -std=gnu17 -Wextra -Werror -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT \
  -c addons/libsh4zam/upstream/source/sh4/shz_complex_sh4.c \
  -o /tmp/sh4zam-fft-probe.o
```

## Historical 0.8.1 fast-math issue and compatibility helper

The official 0.8.1 `shz_sincosu16()` SH-4 fast-math path converts the 16-bit
turn angle with `radians16 / SHZ_F_TAU`. The strict path passes that angle
directly to FSCA. These are not equivalent: the corresponding radian angle is
`radians16 * (SHZ_F_TAU / 65536.0f)`.

The separate `examples/dreamcast/sh4zam/integration/fast-trig-probe.elf`
reproducer compiles only the call under `-ffast-math`, with a strict validator.
Under GCC 16.2/Flycast, angle 16384 returns approximately `(0.07260841,
0.99736059)` instead of the quarter-turn pair `(1, 0)` on official 0.8.1.
With official v0.9.0 it is expected to report `RESULT: PASS`;
the test's expected values and tolerance have not changed. The default example
build includes this direct-API regression. It is not a physical-hardware test.

The default KOS environment does not enable `-ffast-math`, and no current KOS
kernel caller uses this u16 pair routine. Official v0.9.0 fixes the direct
SH-4 fast-math call, so it does not require disabling `-ffast-math`/`-Ofast`.
If reverting to the originally tested 0.8.1 commit, that restriction applies again.
The previously provided KOS-side helper remains available:

```c
#include <kos/sh4zam.h>
shz_sincos_t pair = kos_shz_sincosu16(angle);
```

This helper converts 65536 turn units to radians and calls the public
`shz_sincosf()` API. It supports fast-math without modifying upstream source,
replacing upstream symbols, or changing project-wide compiler flags. Including
the header alone does **not** redirect calls to `shz_sincosu16()`; the current
official release supplies that function's fix. No kernel calls need migration.

The helper consistently uses 65536 units per turn for constant/runtime inputs
on all backends. Upstream v0.9.0 now uses the same 65536-unit convention in
its software and constant-u16 paths. Results remain approximate, not bit-identical
across backends. Tests allow `3e-4` absolute error for the compatibility helper
and `1e-6` for the direct u16 API; these are regression thresholds, not
physical-hardware accuracy guarantees.

The separate `u16-trig-test.elf` checks both the upstream API and compatibility
helper at every 16-bit angle against a strict, double-precision reference in
C strict/fast-math/Ofast and C++ fast-math modes, plus a constant quarter-turn
in each of these eight lanes. The smaller direct-API reproducer is retained.

See the [standalone regression report](../../doc/sh4zam-0.8.1-fast-trig.md)
for cardinal-angle results and reproduction instructions.

## Historical 0.8.1 upgrade validation

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
