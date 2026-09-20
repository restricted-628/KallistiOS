# First-class SH4ZAM integration

This example verifies that the bundled SH4ZAM 0.8.1 library is available through
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

The shared `utils/pvr-deform-test/skin-fixtures.h` tests fixed-four and
variable-span skinning against independent double-precision arithmetic.
It covers nonsymmetric position/normal matrices, normalized weight blending,
in-place output, inactive weights, output guards, rejected inputs, and XMTRX
preservation. It exercises both checked and prepared palettes, verifies that
preparation copies the original matrices, and rejects late invalid joints
without modifying the prior snapshot or storage. Checked/prepared results
match byte-for-byte for dynamic vertex/weight failures and valid-prefix
publication. Capacity, overlapping output, invalid descriptors and storage
guards are also checked. Absolute component tolerances are `3e-4` on target and `2e-5`
on host.

The same fixture also tests the fixed-four immutable weight plan. It compares
checked/prepared output, errors and progress with strided weights and in-place
vertices, checks copied ownership and failed preparation transactions, and
preserves the overflow behavior of active weights that normalize to zero.
Success adds:

```text
SH4ZAM skinning, spans, in-place, rejection, XMTRX: PASS
```

`skin-span-plan-fixtures.h` extends that same check to variable-length plans:
overlapping/shared spans with different normalization totals, repeated joints,
omitted zero slots, unreferenced malformed weights, empty plans, copied input
ownership, exact query capacities, transactional failures, output overlap and
dynamic valid-prefix errors. Prepared output matches the checked span path
byte-for-byte and the independent scalar oracle. Active weights rounded to
zero retain checked overflow rejection. Every apply is checked for XMTRX
preservation on target.

On 2026-09-19 this skin fixture and the full integration example passed
Flycast interpreter and dynarec after the skinning host-backend consolidation.
No hardware performance or numerical certification is implied.

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

The shared wire fixture compares checked/admitted output, callback counts,
progress, errno and output guards across every clip policy, topology, color
mode, and resolver/vertex/profile callback combination. It checks callback
errors, NaN output, zero projection W, capacity and overlap rejection, and
verifies deformation scratch is not written when no resolver is supplied.
XMTRX is preserved after successful and rejected wire calls. Success adds:

```text
Admitted wire policies, clipping, rejection, XMTRX: PASS
```

The wire fixture also covers eight-reference strips with repeated source
indices, all topologies with and without a begin callback, and the admitted
ASSUME_VISIBLE endpoint-reuse path. Begin-time matrix/workspace changes must
invalidate reused positions. A late zero-W endpoint retains the earlier emitted
edge; a degenerate first edge does not call begin. Checked/admitted output,
errno, progress and XMTRX still agree. Host-only instrumentation counts actual
projection requests: reuse cannot increase them in these cases, and no-callback
path topology projects exactly one endpoint per strip reference. No production
instrumentation or extra caller workspace is introduced.

The same reuse checks now include SPLIT/DROP's original homogeneous X/Y/W
cache. Two near-plane-crossing edges share an endpoint but require different
intersections and interpolated colors; independent expectations check both.
Begin-time mutation, degenerate edges and late transform overflow retain
checked output/progress/error behavior across all clip policies. Host probes
count requested homogeneous cache misses (not completed instructions on error)
as well as ASSUME_VISIBLE projection requests. Per-edge clipping is never
replaced by reuse of an intersection or a perspective-divided endpoint.

The two-volume fixture covers both 32-byte color and 64-byte textured caches,
with and without a prepare callback. It compares checked/admitted output,
including buffer guards, and verifies the full temporary-union callback
contract for packed color packets. NaN callback output and zero W are rejected
without sink publication; XMTRX remains unchanged. Success adds:

```text
Admitted two-volume color/textured draws, rejection, XMTRX: PASS
```

The modifier fixture compares checked/admitted 64-byte triangles with and
without a policy callback, including user words, packet fields, buffer guards,
and numeric positions. It rejects NaN and zero W independently at each of the
three corners of the second triangle. The first triangle remains published,
the unfinished volume is not counted, the failing workspace matches the
checked path, and XMTRX remains unchanged after every draw. Success adds:

```text
Admitted modifier triangles, rejection, XMTRX: PASS
```

The shared `utils/pvr-chunk-cache-test/toon-draw-fixtures.h` compares ordinary
toon and outline checked/admitted paths across smooth, flat, and IGNORE_LIGHT
strips, all three clipping policies, inside/intersecting/outside geometry, and
all resolver/vertex-policy/profile-policy combinations. It checks output bytes,
buffer guards, callback counts, progress and errno on success and partial
failure, including capacity, overlap, malformed dynamic profiles, callback
NaNs, and unusable W. No-resolver admitted draws leave deformation scratch
untouched. Every target draw must preserve XMTRX. Success adds:

```text
Admitted toon/outline policies, clipping, rejection, XMTRX: PASS
```

The shared two-volume toon packet regression exercises both 32-byte color
and 64-byte textured formats with smooth, flat and unlit strips; all three
clipping policies; shade-band subdivision; and inside/crossing triangles.
It checks packed commands, both color/UV sets, exact unlit positions and
clipped area, output guards, and NaN/zero-W/capacity rejection. It also compares
checked emission against the admitted two-volume toon entry point, including
resolver/prepare combinations, filtering, callback errors, invalid profiles
and workspace overlap. Output bytes, errno, progress and callback counts must
match. Admitted draws without a resolver leave deformation scratch untouched.
It verifies XMTRX after each call. Success adds:

```text
Two-volume toon color/textured packets, clipping, XMTRX: PASS
```

The 0.8.1 release probes also check header/library version agreement, generic
memory copies for every source/destination offset 0..7 and size 0..132,
aligned 2-byte/4-byte variants within their documented size constraints, return
pointers and surrounding guards. Math probes cover each zero-scale axis,
negative scales, screen initialization after NaN/Inf XMTRX contents, and
cardinal unsigned-16-bit angles under the normal strict compiler policy.

```text
SH4ZAM 0.8.1 memory copies and guards: PASS
SH4ZAM 0.8.1 zero scales, screen init, strict u16 trig: PASS
```

Successful completion prints:

```text
RESULT: PASS (SH4ZAM 0.8.1 camera, frustum, geometry, and fibers)
```

The same result is shown on a green framebuffer for emulator or hardware
validation that does not provide a debug console.

On 2026-08-23, the camera, frustum, geometry, and fiber paths produced the
expected result in Flycast with both the SH-4 interpreter and dynarec. The
compact-model extension subsequently passed in Flycast on 2026-09-08 with
explicit interpreter and dynarec selections after the clean GCC 16.2.0 KOS
build incorporating official master through `33c6e0ba`. Both runs printed the
then-current 0.8 result, covering model emission and matrix restoration as well as the
earlier checks. This is an emulator correctness fixture, not a physical-device
numerical or performance certification.

The 0.8.1 upgrade was force-rebuilt with SH-4 GCC 16.2.0 on 2026-09-19.
Interpreter and dynarec runs passed the complete integration fixture including
the new release probes. The optional fast-math diagnostic failed in both
modes; that failure is documented separately below rather than counted as a
passing integration result.

The subsequent temporary PR #70 pin was force-rebuilt on the same date.
The main integration fixture and direct fast-math probe both passed in
Flycast interpreter and dynarec modes; the helper's full-angle sweep passed
all four lanes on GCC 14 host and Flycast dynarec. Hardware remains untested.

## Matrix composition comparison

After sourcing `environ.sh`, build and run `compose-bench.elf` separately.
It compares production `mat_compose()` (`kos-compose`) with an example-private
SH4ZAM `shz_mat4x4_mult()` wrapper (`saved-xmtrx`). The wrapper saves/restores
XMTRX and retains the alias-safe imports/exports and argument checks. It is
not linked into libkallisti and does not replace the production implementation.

Both implementations must pass the same deterministic full-4x4 input set,
five input/output alias arrangements, and six null/misaligned rejection cases.
Results are compared with an independent double-precision multiply, using
`3e-4 * (1 + abs(expected))` on target and `2e-5 * (1 + abs(expected))` on host.
These finite tests do not establish an all-input numerical bound. Tests check
output guards, unchanged output on failure, all 16 XMTRX lanes and unchanged
FPSCR FR/SZ/PR/DN/rounding modes; sticky arithmetic flags are not compared.
The host uses SH4ZAM's actual software XMTRX backend and KOS's scalar compose
fallback, so it validates contracts rather than SH-4 instruction behavior.

On target, timing follows correctness checks for three 32-matrix workloads:
independent pairs, a parent chain, and destination-aliases-left multiplication.
Each sample contains 128 batches (4096 calls). Seven samples alternate which
implementation runs first after warmup; raw microseconds and min/median/max
are printed. Interrupts stay enabled. No empty-loop time is subtracted.
The in-place workload includes its common batch-reset copy. Validation and
printing are outside each timer interval, and separate non-LTO translation
units keep the calls real. Capture the complete debug-console output along
with the hardware, compiler, KOS/dependency revisions and build flags.

Success ends with:

```text
RESULT: PASS (matrix composition comparison; production unchanged)
```

From the repository root, host correctness lanes are:

```sh
make -C utils/matrix-compose-test -B test CC=gcc-14
make -C utils/matrix-compose-test -B test CC=gcc-14 HOST_CSTD=c23 HOST_PEDANTIC=-pedantic
make -C utils/matrix-compose-test -B test CC=clang HOST_CSTD=c2x HOST_PEDANTIC=-pedantic
make -C utils/matrix-compose-test -B test CC=clang CFLAGS='-O1 -g -std=gnu17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
```

The shared host runner discovers this suite automatically. The pinned upstream
software `.c` file lacks a final newline; only that object's Clang build disables
the formatting warning. Upstream source stays untouched. On 2026-09-20 all four
lanes passed, and the SH-4 GCC 16.2.0 executable passed Flycast interpreter and
dynarec, including every timed sample's correctness checks. **Emulator timings
are not physical Dreamcast speed evidence.** No production replacement is
selected until hardware measurements establish the tradeoff.

## Direct API fast-math regression

After sourcing `environ.sh`, build `make fast-trig-probe.elf` in this directory.
Only `fast-trig-call.c` is compiled with `-ffast-math`; the validator is strict.
This diagnostic fails with official 0.8.1 because its new u16-angle fast path
uses the wrong angle conversion. KOS now temporarily pins the exact commit
in [SH4ZAM PR #70](https://github.com/gyrovorbis/sh4zam/pull/70), containing
Falco's suggested fix. The default example build includes the probe and it
must now report `RESULT: PASS (SH4ZAM fast-math u16 trig)`. Expected values
and tolerance are unchanged. See the [adapter restriction and helper](../../../../addons/libsh4zam/README.md#081-fast-math-restriction-and-local-helper)
for source provenance and the policy if reverting to the official 0.8.1 tag.

## Local KOS u16 helper regression

`#include <kos/sh4zam.h>` supplies `kos_shz_sincosu16(angle)`. This explicit
KOS helper routes 65536-units-per-turn inputs through SH4ZAM's radians API;
it neither overrides the upstream function nor edits the submodule.

The default example build also produces `u16-trig-test.elf`. Run it separately
to validate all 65536 angles against strict double-precision `sin`/`cos`, with
an absolute tolerance of `3e-4`. Callers are separate, non-LTO translation
units built as strict C, fast-math C, Ofast C, and fast-math C++. Each lane
also checks a constant quarter-turn. Success ends with:

```text
RESULT: PASS (KOS SH4ZAM u16 adapter)
```

Portable-backend tests use the same sources. From this directory, without
needing a sourced KOS environment:

```sh
make -f Makefile.host-test CC=gcc-14 CXX=g++-14
make -f Makefile.host-test clean
make -f Makefile.host-test CC=clang CXX=clang++
make -f Makefile.host-test clean
```

Keep compiler runs in separate build directories or clean between them. Host
compiler overrides use `CFLAGS`, `CXXFLAGS`, and `LDFLAGS`; the validator always
appends `-fno-fast-math -fno-lto`. These tests do not establish physical-hardware
accuracy or performance.
