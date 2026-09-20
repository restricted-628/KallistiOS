# SH4ZAM consumer audit: checked animation matrices and skinning

## Scope and source ownership

This pass follows the source-dependency migration on KOS master (`6f8b3394`).
The original audit used unmodified SH4ZAM at
`0bacf4b336368c0b47864ce9eeb59e7c07904b51`; the dependency subsequently moved to
official 0.8.1 and is now temporarily pinned to PR #70's
`0c1ccb5f5614314e36e2fec3179c8ca3ae844770` for the u16 fast-math fix, pending
upstream review. See the addon README for source provenance. All changes in
this consumer audit are to KOS consumers, bridge assertions, tests, and
documentation. Graphics-library extraction is deferred; no public matrix
layouts or API contracts change.

## Verified findings and changes

- `mat_compose()` already uses SH4ZAM's one-off FIPR transforms on SH-4.
  Its public contract permits output to alias either input and forbids changing
  XMTRX. Upstream `shz_mat4x4_mult()` explicitly clobbers XMTRX; it is not a
  direct replacement. Keep the current path until a save/restore alternative
  is measured against representative workloads. The alias-safe bridge remains;
  no type-punning casts were introduced.
- `anim_transform_matrix_build()` now uses SH4ZAM's quaternion matrix builder,
  vector scaling, and translation setter on both host and target. The duplicate
  host quaternion-to-matrix formula was removed. Scale is applied to column XYZ
  only, preserving the affine row and `T * R * S` ordering. The full matrix
  `apply_scale()` API would clobber XMTRX and is intentionally not used.
- `anim_camera_view_matrix_build()` now uses SH4ZAM vector subtraction,
  reciprocal magnitude, cross/dot products, scaling/addition, and sin/cos on
  both platforms. KOS's Rodrigues-roll and checked look-at conventions remain.
- The KOS bridge's C `_Static_assert` declarations now include messages, as
  required before C23. This fixes Apple Clang's GNU17 `-Werror` build when the
  bridge is used by host animation tools; upstream headers are unchanged.

Other animation host fallback paths and other graphics consumers are not
converted by this pass. Validation remains at the checked API boundaries;
removing those checks requires a separately defined trusted/batched path.

## Skinning follow-up

The fixed-four and variable-span Compact skin APIs delegate to
`pvr_skin_apply()` and `pvr_skin_apply_spans()` in `pvr_deform.c`. Their common
accumulator already used SH4ZAM's one-off point/normal transforms on Dreamcast;
this was not a remaining scalar KOS transform on the target. The duplicate
host formulas are now removed, letting SH4ZAM choose its portable backend.
Alias-safe matrix imports and the XMTRX-preserving target path remain intact.
The host normal-normalization fallback remains separate from SH-4 FSRRA.

`utils/pvr-deform-test/skin-fixtures.h` now runs on host and target. It compares
both skin APIs against independent double-precision arithmetic using three
vertices, four distinct nonsymmetric joint matrices, separate normal matrices,
non-unit weight totals, repeated joints in a six-entry span, and an inactive
out-of-range joint. It checks in-place and separate output, canonical W values,
an output guard, and no publication on invalid active joints or NaN palettes.
The target additionally verifies all 16 XMTRX lanes after successful and
rejected calls. Absolute tolerances are `2e-5` on host and `3e-4` on SH-4;
these fixtures do not establish a universal numerical error bound.

On 2026-09-19 the deformation suite passed GCC 14 GNU17/strict C23, Apple
Clang strict C2x, and Clang ASan/UBSan. Compact skin, shape, and scene-integration
host suites passed GCC 14. The incremental KOS build and force-built integration
example passed SH-4 GCC 16.2.0; the full integration fixture passed Flycast
interpreter and dynarec, including the new skin/XMTRX checks. No new upstream
defect was demonstrated. This pass changes no upstream source.
As a negative control, temporarily transposing the normal transform made the
new scalar-reference fixture fail; restoring the intended call passed again.

The target accumulator remains 368 bytes, fixed-four apply 1128 bytes, and
span apply 1356 bytes with this toolchain, unchanged from before the host
consolidation. FIPR remains in the accumulator. This is not a speedup claim.
Per-influence matrix import/copy cost and repeated validation of mutable
palettes/weights remain performance-audit items. Removing validation requires
an explicit prepared-data lifetime/invalidation contract; the checked APIs
still accept caller-mutated data each call.

## Generated-code evidence

With the local SH-4 GCC 16.2.0 release environment (`-O2`, GNU17,
`-m4-single`), object symbol sizes were:

| Function | Before | After |
| --- | ---: | ---: |
| `anim_transform_matrix_build` | 704 bytes | 704 bytes |
| `anim_camera_view_matrix_build` | 568 bytes | 588 bytes |

The camera function changes from zero to two inline FIPR instructions (length
squared and dot product); FSRRA and FSCA remain. Size includes the function's
literal pool. These observations do **not** establish fewer cycles or a
hardware speedup. In particular, vector register setup can offset instruction
savings; an elapsed-time benchmark is still required.

## Validation

The shared host/SH-4 fixture checks nine TRS combinations, four oblique-camera
rolls, all four output alias arrangements for composition, and rejected-input
publication. Expected TRS bases are tabulated independently; camera and matrix
product references use scalar double arithmetic, not the APIs under test.
The target callback checks a distinct 16-value XMTRX sentinel after each call.

On 2026-09-19, the animation suite passed GCC 14 GNU17/strict C23, Apple Clang
16 strict C2x, and Clang GNU17 with AddressSanitizer/UndefinedBehaviorSanitizer.
The affected Compact-model, Compact-animation, and scene-integration host
suites also passed with GCC 14. The KOS/addon build and target integration
example built successfully with SH-4 GCC 16.2.0.

Run the host fixture with:

```sh
make -C utils/animation-test -B test CC=gcc-14
make -C utils/animation-test -B test CC=gcc-14 HOST_CSTD=c23 HOST_PEDANTIC=-pedantic
make -C utils/animation-test -B test CC=clang HOST_CSTD=c2x HOST_PEDANTIC=-pedantic
CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  make -C utils/animation-test -B test CC=clang
```

Build the target fixture after sourcing `environ.sh`:

```sh
make -j4
make -C examples/dreamcast/sh4zam/integration -B
```

Flycast's interpreter and dynarec both pass the new matrix fixture and the
existing geometry/frustum/fiber/FPSCR integration checks. This is emulator
correctness evidence, not physical Dreamcast certification. Host tolerances
and target tolerances are documented beside the example.

## Remaining work

1. Compare the current composition implementation with an XMTRX-preserving
   multiply candidate, including emitted code, copy/register costs, and actual
   workload timing before choosing a replacement.
2. Continue the skinning performance audit (prepared palettes/weights and
   per-influence matrix imports); the checked transform integration is now
   covered by the shared fixture above. Audit remaining prepared Compact-model
   draw variants. Ordinary, two-volume, and modifier paths now
   have explicit one-time admission and in-place projection; see the
   [draw-cache contract](pvr-chunk-model.md#prepare-once-ordinary-draws).
   Ordinary toon/outline now reuse the admitted draw view and borrow unchanged
   deformation data; clipping and changing lighting/profile data stay checked.
   Two-volume toon and wire variants still need their own audit.
3. Add representative throughput scenes and collect physical-hardware
   numerical, image, and timing results. Neither host nor emulator PASS closes
   this gate.
