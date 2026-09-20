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

After the host consolidation, the target accumulator remained 368 bytes,
fixed-four apply 1128 bytes, and
span apply 1356 bytes with this toolchain, unchanged from before the host
consolidation. FIPR remains in the accumulator. This is not a speedup claim.
Per-influence matrix import/copy cost and repeated validation of mutable
palettes/weights remained performance-audit items. Removing validation requires
an explicit prepared-data lifetime/invalidation contract; the checked APIs
still accept caller-mutated data each call.

### Prepared skin palettes

The opt-in `dc/pvr_skin_prepared.h` API now validates and imports position and
normal matrices once per sampled pose with `pvr_skin_palette_prepare()`.
Caller-owned `pvr_skin_prepared_joint_t` storage contains actual SH4ZAM matrix
types, not casts of KOS storage. The source arrays are copied; the prepared
descriptor and imported joints must remain immutable during all applications.
Reprepare when the pose changes, and reuse it across meshes sharing the pose.
Failed preparation leaves existing storage and the descriptor untouched.

`pvr_skin_apply_prepared_palette()` and its variable-span counterpart skip
palette value scans and per-influence imports. The existing checked APIs
remain available. Both paths share blending arithmetic, weight/index checks,
changing source validation, normal normalization, and valid-prefix errors.
The new path rejects output overlap with the prepared descriptor or joints.
Weight validation and per-vertex normalization of weight sums still repeat in
these palette-only APIs; the fixed-four weight-plan path below removes them.

SH-4 GCC 16.2.0 (`-O2`, GNU17, `-m4-single`) emits a prepared branch that
directly addresses each 104-byte imported joint and bypasses the two `memcpy`
calls (64-byte position and 36-byte normal) in the checked branch. FIPR remains
in the shared accumulator. That accumulator is now 408 bytes versus 368
before, and retains stack space needed by the checked branch. No stack or
elapsed-time reduction is claimed. Preparation cost, storage and reuse count
must be included in a physical-hardware throughput comparison.

The shared host/SH-4 skin fixture covers checked and prepared fixed-four/span
APIs against the independent scalar oracle, including in-place output and
inactive out-of-range joints. It mutates the original matrices after
preparation to verify copy ownership. Differential cases compare output,
errno and valid-prefix progress for changing vertices, zero normals, malformed
weights, invalid strides and short output. Transaction tests reject late NaN
matrices, insufficient or overlapping storage and invalid descriptors without
publication. Every target case checks XMTRX preservation.

On 2026-09-19 the deformation suite passed GCC 14 GNU17/strict C23, Apple
Clang strict C2x, and Clang ASan/UBSan. Compact skin, shape and scene integration
host suites passed GCC 14. The KOS build, exported symbols, integration and
updated `chunk_skin` example built with SH-4 GCC 16.2.0; the public header also
passed a target C++ syntax check. The full integration fixture passed Flycast
interpreter/dynarec and the 120-frame skin example passed dynarec. The approved
SH4ZAM source pin remains clean. No upstream bug or hardware speedup is claimed.

### Fixed-four immutable weight plans

`pvr_skin_influences_prepare()` validates a strided fixed-four influence
stream against its joint count and copies normalized records into caller-owned
storage. All source values are checked before any destination write, so a
late malformed record leaves the old plan and storage intact. Original
influence arrays may change or be released afterward. Plan records retain
slot order and an active mask: a positive weight that rounds to zero during
division must still execute its transform, including any overflow-times-zero
failure that the checked path would report.

`pvr_skin_apply_prepared()` combines this immutable mesh plan with an immutable
pose palette. Counts must match. Weight scans, weight sums/divisions, palette
component scans and per-influence imports are skipped. Source vertices,
arithmetic, output capacity and overlap remain checked, with the same in-place
and valid-prefix behavior. Joint index meaning must remain stable; changes to
weights, ordering, indices or counts require rebuilding the plan. Pose-only
changes require refreshing the palette, not the weights. The `chunk_skin`
example demonstrates these two separate preparation lifetimes.

The new SH-4 apply symbol is 716 bytes with GCC 16.2.0 (`-O2`, GNU17,
`-m4-single`). Its disassembly has no weight-division call; it calls the same
XMTRX-preserving accumulator and checked normal normalization. The old checked
and palette-only paths are unchanged. This is removed repeated work, not a
measured hardware speedup. Variable-length spans use the separate path below.

Shared host/SH-4 fixtures compare output bytes, errno and progress against
the palette-only checked-weight path for strided records, repeated joints,
inactive out-of-range indices, in-place output, NaN positions, zero normals,
short output and invalid vertex stride. Successful values also match the
independent double-precision oracle. Tests poison the original weights after
preparation, check guards and transactional rejection, and specifically prove
that an active weight rounded to zero retains overflow rejection. Target
preparation and application checks preserve all XMTRX lanes.

On 2026-09-19 the expanded deformation suite passed GCC 14 GNU17/strict C23,
Apple Clang strict C2x and Clang ASan/UBSan. Existing Compact skin and shape
host regressions also passed GCC 14. SH-4 GCC 16.2.0 built KOS, both
new exports and both examples, and accepted the public header in C++. The full
integration fixture passed Flycast interpreter/dynarec; the 120-frame skin
example passed dynarec. SH4ZAM remains unmodified at the approved PR #70 pin.
Physical-hardware correctness and throughput are not established by these runs.

### Variable-span immutable weight plans

`pvr_skin_spans_prepare_query()` validates a strided span stream and reports
exact capacities for its independent normalized runs. `pvr_skin_spans_prepare()`
revalidates and copies those runs into caller-owned storage. Shared/overlapping
source spans are expanded independently: their normalization totals can differ.
Only originally zero weights are omitted; order, repeated joints and positive
weights rounded to zero are retained. Unreferenced source weights are ignored,
matching checked skinning. The query guards total storage overflow, and failed
query/preparation leaves every destination unchanged.

`pvr_skin_apply_spans_prepared()` pairs this immutable mesh plan with a prepared
pose palette. It checks counts, addresses and output overlap in constant-size
framing, then checks changing vertices and arithmetic while skinning. It does
not rescan span/weight/index values or recompute weight sums/divisions. Original
span/weight arrays are not borrowed. Rebuilding is required after influence or
vertex/joint mapping changes, while pose-only changes refresh just the palette.
The older APIs remain checked alternatives. The `chunk_scene` example now
prepares each general-skin plan at load time and palettes per sampled pose.

The SH-4 apply symbol is 760 bytes with GCC 16.2.0 (`-O2`, GNU17,
`-m4-single`); its disassembly contains no weight-division call and still calls
the shared XMTRX-preserving accumulator and dynamic normal normalization.
Expanded runs can cost more storage than shared input weights; this design
does not claim a net hardware throughput gain without measuring representative
content and pose reuse.

The shared `skin-span-plan-fixtures.h` checks exact query sizes, independently
normalized overlapping spans, repeated joints, omitted zero slots, ignored
unreferenced malformed records, empty plans, copied ownership and guards.
Checked/prepared bytes, errno and valid-prefix progress agree for in-place and
separate output, NaN vertices, zero normals, short output and invalid stride.
Independent double-precision expectations also check successful poses.
Transactional failures cover late malformed spans/weights, overflowed totals,
capacities, alignment, overlap, address-size overflow and invalid descriptors.
Positive weights rounded to zero retain checked overflow rejection; target
calls preserve XMTRX.

On 2026-09-20 the expanded deformation suite passed GCC 14 GNU17/strict C23,
Apple Clang strict C2x and Clang ASan/UBSan. Compact skin and shape host
regressions also passed GCC 14. The scene host golden and failure-cleanup
suite passed GCC 14 using the new path. SH-4 GCC 16.2.0 built KOS,
all three exports and both examples; the header also passed a target C++ syntax
check. The full integration fixture and 240-frame scene both passed Flycast
interpreter and dynarec, including scene pose goldens and final PVR fault checks.
The approved SH4ZAM source pin remains clean; no upstream defect or measured
physical-hardware speedup is claimed.

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

1. Run the new composition comparison harness on physical Dreamcast hardware
   before choosing a replacement. The benchmark-only XMTRX-preserving multiply
   candidate and emitted-code comparison are described below; host and emulator
   correctness checks do not establish a hardware performance winner.
2. Measure prepared skinning with representative mesh/pose reuse: fixed-four
   and variable-span plans now validate and normalize weights once per mesh;
   prepared palettes now remove palette rescans and per-influence imports
   when explicitly used, as tested above. Audit remaining prepared Compact-model
   draw variants. Ordinary, two-volume, and modifier paths now
   have explicit one-time admission and in-place projection; see the
   [draw-cache contract](pvr-chunk-model.md#prepare-once-ordinary-draws).
   Ordinary toon/outline now reuse the admitted draw view and borrow unchanged
   deformation data; clipping and changing lighting/profile data stay checked.
   Wire now reuses admission and borrows unchanged deformation records as
   described below. Two-volume toon now also reuses its existing admission
   view and borrows unchanged deformation records; packet packing and
   checked/admitted equivalence are tested below.
3. Add representative throughput scenes and collect physical-hardware
   numerical, image, and timing results. Neither host nor emulator PASS closes
   this gate.

## Matrix composition comparison harness

`examples/dreamcast/sh4zam/integration/compose-bench.elf` compares production
composition against a private save/multiply/restore candidate. It measures
independent pairs, parent chains and in-place composition with repeated,
alternating-order trials. Its shared host/target correctness gates cover full
4x4 matrices, aliasing, rejected arguments, output guards, independent
double-precision results, XMTRX and FP control-mode preservation. See the
[example methodology](../examples/dreamcast/sh4zam/integration/README.md#matrix-composition-comparison).

With SH-4 GCC 16.2.0 at the current `-O2`/`-m4-single` policy, object inspection
finds 16 FIPR instructions in production `mat_compose()` (688-byte symbol).
The candidate wrapper is 208 bytes but additionally calls upstream's assembly
load/apply/store routine, which contains four FTRV instructions. That callee
must not be omitted when comparing code or runtime cost. Both wrappers retain
three 64-byte memcpy calls for matrix imports/export. The candidate additionally
saves and restores 64 bytes of XMTRX state. Its own stack reservation plus saved
registers is 284 bytes versus 348 bytes in production, excluding callees.
These are emitted-code observations for this build, not cycle estimates.

On 2026-09-20 GCC 14 GNU17/strict C23, Clang strict C2x and Clang GNU17 with
ASan/UBSan passed. The SH-4 executable passed Flycast interpreter and dynarec,
including all timed sample checks. Physical hardware timing remains open.
Production math, public APIs and upstream SH4ZAM source remain unchanged.

## Wireframe follow-up

`pvr_chunk_model_cache_draw_emit_wire()` now accepts the existing prepared
ordinary-cache view. It removes full cache/base-deformation scans and avoids
copying unchanged deformation records when no resolver runs. Vertex callbacks
still receive the original deformation values, borrowed read-only. The checked
API remains available, but no longer validates a memory-sink cache twice just
to compute worst-case capacity. The public capacity query remains checked.

The wire path already reaches SH4ZAM through geometry projection and frustum
segment clipping on Dreamcast. This pass does not replace that math. It retains
homogeneous clipping before projection, all three clip policies, per-edge
projection/expansion, and validation of mutable inputs. Memory-sink sizing still
visits strip descriptors each draw; no throughput gain is claimed without
hardware timing. The rendered `chunk_wire` example admits its cache once.

Shared checked/admitted fixtures compare output bytes, errno, progress,
callback counts, and guards across three clip policies, three topologies,
two color modes, and all resolver/vertex/profile callback combinations.
They include filtered strips, callback errors, NaN deformation/vertex/profile
output, zero projection W, insufficient capacity, overlapping workspace, and
null admission.
They also prove deformation scratch stays untouched without a resolver.
Host coverage initially included triangle and four-reference strips; the
endpoint-reuse follow-up below adds an eight-reference strip on host and target.
Target integration verifies all XMTRX lanes after every draw.

On 2026-09-19 the wire fixtures and cache suite passed GCC 14 strict C23,
Apple Clang strict C2x, and Clang GNU17 with ASan/UBSan. The SH-4 GCC 16.2.0
KOS build and both examples linked, including the new exported entry point.
The integration example passed in Flycast interpreter and dynarec modes; the
360-frame wire example passed its draw and pipeline checks in dynarec mode.
These are correctness checks, not physical-hardware timing or image-quality
certification. No upstream SH4ZAM defect was demonstrated in this pass.

### ASSUME_VISIBLE endpoint reuse

Admitted wire draws now keep a three-slot, per-strip cache of projected
positions for the explicit ASSUME_VISIBLE policy. Reference-topology edges
span at most two indices, allowing neighboring edges to reuse endpoints without
allocation, a public layout change, or writing additional caller scratch.
Only positions are reused; flags, colors and line expansion remain per edge.
The existing checked geometry projector handles cache misses, preserving its
arithmetic and XMTRX save/restore. That first pass left SPLIT/DROP unchanged: clipped endpoints
depend on the complete segment and cannot reuse already-projected coordinates.

Projection stays lazy, in original edge order, so an unusable later endpoint
does not discard an already emitted prefix. Reuse is cleared after begin-strip
callbacks, which may change the live matrix or workspace, and at every strip.
Repeated source indices are keyed by strip-reference index, not source identity,
because per-reference vertex policies can produce different positions/colors.

Shared regression cases cover no-begin and begin callbacks, longer strips and
slot eviction, matrix/workspace mutation during begin, a zero-W later endpoint,
and a degenerate first edge. Host-only instrumentation delegates to the real
projector and counts requested endpoints: no tested draw requests more than
the checked path, and a no-begin path draw requests exactly N endpoints rather
than 2(N-1). Mesh/boundary cases also require a reduction without a begin
callback. This demonstrates less repeated work, not a hardware speed claim.
The subsequent SPLIT/DROP follow-up below reuses original homogeneous positions.

On 2026-09-20 the cache suite passed GCC 14 GNU17/strict C23, Clang strict
C2x and Clang GNU17 with ASan/UBSan. The full SH-4 GCC 16.2.0 KOS build and
both example links passed. Integration completed in Flycast interpreter and
dynarec; the 360-frame wire scene completed in dynarec. The integration link
now tracks both library archives so a library-only update cannot silently
reuse an old test executable. SH4ZAM's pinned source remains unmodified.
Physical Dreamcast correctness and throughput remain unverified.

### SPLIT/DROP homogeneous endpoint reuse

Admitted SPLIT/DROP draws now share the same three-slot local-storage budget,
but cache original transformed X/Y/W instead of screen-space positions. A
private frustum entry point shares the public segment clipper's validation,
arithmetic, intersection/attribute interpolation, projection and publication
logic. It reuses a position only for an unchanged strip reference. Neither
intersections nor interpolated colors are stored in the cache. Public API
layouts and the checked wire entry point remain unchanged.

The cache is cleared per strip and after begin callbacks. Every edge still
validates its live frustum and endpoint attributes. Misses use the existing
SH4ZAM FIPR transform, importing its matrix only when the edge has a miss;
XMTRX remains untouched. Lazy processing retains the emitted prefix on a later
overflow. The private helper adds no dynamic-link export or upstream change.

The shared wire fixture exercises all policies/topologies on three-, four-
and eight-reference strips, with and without callbacks. Two segments sharing
a behind-near endpoint have independently checked distinct intersections and
interpolated colors. Other cases cover begin-time matrix/workspace changes,
degenerate first edges and finite-input transform overflow on a later endpoint.
Host probes count requested cache misses: a no-begin path requests N transforms
instead of 2(N-1) for every policy. These are operation-count checks, not
hardware timing measurements. The rendered wire scene now exercises all nine
policy/topology combinations over 360 frames, including side-plane crossings.

On 2026-09-20 both the geometry and draw-cache host suites passed GCC 14
GNU17/strict C23, Clang strict C2x, and Clang GNU17 with ASan/UBSan. The full
SH-4 GCC 16.2.0 build and both example links passed. Integration completed in
Flycast interpreter and dynarec; the updated 360-frame wire scene passed in
dynarec. SH4ZAM source verification remained clean. No physical-hardware
numerical or performance certification is implied.

## Two-volume toon packet correction

The audit found a KOS-side correctness defect before introducing an admitted
toon path. `prepare_two_volume_triangle()` and the parallel-attribute clip
assembler produce full 64-byte union entries. Color-only projection and sinks
expect consecutive 32-byte packets, so they read union padding as a vertex
command. The existing toon test covered only 64-byte textured packets.

A new color-only regression failed on the prior implementation with `EILSEQ`
on a valid, inside triangle. Color packets are now compacted after all
union-indexed reads and before in-place projection, and clipped color output
is compacted before sink submission. Textured packets keep their 64-byte
layout. Callbacks still receive complete unions, including zeroed unused
color-packet tail bytes. No SH4ZAM code or public packet representation changes.

`two-volume-toon-fixtures.h` runs on host and SH-4. It covers both formats,
smooth/flat/IGNORE_LIGHT strips, all three clipping policies, shade-band
splitting, and inside/crossing geometry. It checks both attribute sets, packed
command positions, untouched output tails, invalid callback XYZ, zero W,
insufficient sink capacity, and XMTRX preservation. The unlit cases also have
independent position and area expectations (area 2 before clipping, 1.75 after
the selected left-plane clip), preventing success-only or shared-bug tests.

This packet correction is independent of the prepare-once optimization below.

On 2026-09-19 the complete cache suite passed GCC 14 GNU17/strict C23,
Apple Clang strict C2x, and Clang GNU17 with ASan/UBSan. The incremental
SH-4 GCC 16.2.0 KOS build and integration example built successfully, and
the full integration fixture passed Flycast interpreter and dynarec with
the new packet checks. No physical-hardware rendering or speed claim is made.

## Prepare-once two-volume toon follow-up

`pvr_chunk_model_two_volume_cache_draw_emit_toon()` reuses the existing
`pvr_chunk_two_volume_cache_draw_t` snapshot. It skips repeated static-cache
validation and, without a resolver, borrows the immutable deformation records
instead of copying and revalidating them. Source indices are read for vertex
callbacks only. The checked API remains available, and changing transforms,
profiles, workspace/sink ranges, callback output, shading and clipping retain
their validation. This does not change the SH4ZAM math or its XMTRX contract.

The shared two-volume toon fixture now compares checked/admitted output bytes,
progress, errno and callback counts for both packed formats, three shading
modes, all clip policies, inside/crossing triangles, and resolver/prepare
combinations. It covers filter skips/errors, begin errors, NaN deformation or
vertex output, invalid profiles, zero W, sink capacity and workspace overlap.
Admitted no-resolver draws leave deformation scratch untouched. The independent
packet/position/area assertions from the correction remain in place.

On 2026-09-19 the expanded cache suite passed GCC 14 GNU17/strict C23,
Apple Clang strict C2x, and Clang GNU17 with ASan/UBSan. The SH-4 GCC 16.2.0
KOS build exported the new entry point, and the rebuilt integration example
completed with RESULT: PASS in Flycast interpreter and dynarec modes.
The SH4ZAM source check remained clean at the approved PR #70 pin. No
upstream defect or physical-hardware throughput improvement is claimed.
