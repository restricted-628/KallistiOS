# SH4ZAM 3x4 affine skeleton integration

This first production consumer of SH4ZAM 0.9.0's expanded 3x4 API targets pose
palette construction. It does not change PCM/PSK serialization, the general
4x4 hierarchy API, projection, or the existing prepared skinning joint ABI.

## Usage and lifetime

Include `<dc/pvr_chunk_skeleton_affine.h>`.

1. At asset load, call `pvr_chunk_skeleton_affine_prepare` to copy the admitted
   inverse binds and node indices into caller-owned compact joint storage.
   Materialized source joints can then be released. Reprepare if binds change.
2. At asset load, call `pvr_chunk_hierarchy_affine_prepare` to copy the topology
   and TRS-suppression flags into caller-owned records. For each sampled local
   TRS array, call `pvr_chunk_hierarchy_pose_build_affine` to produce compact
   world matrices directly. Share the resulting pose across all skeletons
   using that hierarchy. For existing 4x4 pose producers, the original
   `pvr_chunk_skeleton_pose_prepare_affine` packing bridge remains available.
3. Call `pvr_chunk_skeleton_palette_build_affine` for each skeleton to produce
   the existing prepared skin palette directly. Reuse that palette with
   `pvr_skin_apply_spans_prepared` or the other prepared skinning consumers.

Snapshots and backing storage must remain immutable during consumption. They
are not general manually constructed matrix views. All arrays are caller-owned;
there are no allocations. Admission checks finite components and the exact
affine bottom row `[0, 0, 0, 1]`. Perspective matrices are rejected, not silently
truncated or approximately classified. Use the original 4x4 API for them.

The compact hierarchy producer requires a nonempty parent-before-child tree
(multiple roots and interleaved subtrees are supported). It evaluates hidden
nodes and honors translation/rotation/scale suppression. Prune-children flags
are rejected at preparation: a partial render traversal cannot provide a
complete skeleton pose. Static node matrices, model views, and user data are
not retained or inspected; all local transforms come from the supplied TRS
array. Changing topology/flags requires preparation again.

Hierarchy boundary errors leave outputs unchanged. During evaluation, world
storage is scratch and the output descriptor is empty until success. A late
invalid local transform or arithmetic overflow therefore requires discarding
the partially written world array (and any older descriptors referencing it).
The caller can reuse the storage on its next build. This avoids a second world
array or duplicate hierarchy arithmetic solely for transactional publication.
The existing packing and palette APIs retain their unchanged-on-failure rules.

## Work performed

Each palette entry is `world[node] * inverse_bind`, using
`shz_xmtrx_load_apply_3x4`. Its full result is stored for the existing prepared
skinning consumer. Nonuniform scale, shear, and reflections retain the existing
scaled inverse-transpose normal calculation.

XMTRX is saved/restored once per whole palette build, including error exits.
There are no callbacks or FPSCR mode changes inside that scope, and no new
inline assembly or compiler memory clobbers. Input component and joint-index
validation is not repeated inside the composition loop. Output-normal checks
remain necessary because valid inputs can compose into a singular or overflowing
result. A dry arithmetic pass preserves unchanged outputs on late failure;
the publishing pass repeats the arithmetic without rescanning admitted inputs.

Hierarchy evaluation uses `shz_xmtrx_load_apply_store_3x4` for parent/local
composition. Topology is validated only at setup; each dynamic TRS is validated
and its quaternion normalized once per evaluation. Suppressed source components
still must be valid, matching the general traversal. Finite-result checks catch
arithmetic overflow. XMTRX is saved/restored once across the complete hierarchy,
including failures. SH4ZAM's quaternion rotation initializer still uses a small
temporary 4x4 rotation matrix; persistent world storage and all parent/child
composition are compact. Projection continues to use full 4x4 matrices.

The animated `chunk_scene` example now uses this path, including the grid,
clipping, and workload variants. It removes persistent legacy materialized
joint records and separate position/normal palette arrays, plus the subsequent
`pvr_skin_palette_prepare` copy/validation pass. It now also removes the full
4x4 world array and the subsequent packing pass. No serialized format changes
are needed.

## Storage and performance boundary

On the SH-4 build, each compact matrix is 48 bytes rather than 64; each compact
inverse-bind joint record is 52 rather than 72 bytes. These are per-record
measurements, not a claim of a 25% reduction in the entire scene or palette.
The prepared palette consumed during skinning deliberately retains its existing
4x4 position plus 3x3 normal layout and its XMTRX-preserving one-off transforms.

## Optional compact skinning palette

An additional, opt-in runtime representation is now available:

- `pvr_skin_compact_joint_t`: column-major `shz_mat3x4_t` position and
  `shz_mat3x3_t` inverse-transpose normal. It is 84 bytes versus 104 bytes for
  the existing prepared joint on the tested host and SH-4 builds (including
  padding). The position portion alone drops from 64 to 48 bytes.
- `pvr_chunk_skeleton_palette_build_compact`: builds these records directly
  from the admitted hierarchy pose and inverse binds. Normal calculation,
  singular/overflow rejection, unchanged-on-failure publication, and batch
  XMTRX preservation are shared with the original affine palette builder.
- `pvr_skin_palette_prepare_compact`: bridges existing KOS palettes, rejecting
  any position matrix whose bottom row is not exactly `[0, 0, 0, 1]`. Normals
  are copied as supplied, not inferred from the position matrix.
- `pvr_skin_apply_spans_compact`: consumes the existing immutable variable-span
  weight plan with this palette.
- `pvr_skin_apply_compact`: consumes the existing immutable fixed-four plan.
  Its active mask preserves positive-source weights rounded to zero and skips
  only originally zero slots. Both compact APIs retain canonical in-place
  processing, valid-prefix errors and XMTRX preservation. No serialized layout
  or old API changes; the established consumers remain available.

The consumer uses SH4ZAM's `shz_vec4_dot3` on the three position rows and its
existing 3x3 normal transform. This avoids constructing a temporary 4x4 matrix
or loading XMTRX for each influence. Dynamic source/result checks, normal
normalization, canonical in-place operation, source weight order, repeated
joints, originally-positive weights that normalize to zero, and valid-prefix
errors retain the established skinning semantics. Palette components and
prepared weights/indices are not rescanned inside the influence loop.

GCC 16.2 SH-4 object-code inspection shows six FIPR instructions in the compact
influence body (three position, three normal), versus seven in the existing
`skin_accumulate` body. The compact influence body has no function calls,
FTRV, FRCHG, or FSCHG. These are code-generation facts, not cycle measurements:
loads, register pressure, cache-line crossings from the 84-byte stride, and
workload mix still matter. No new inline assembly or memory clobbers were added.

The original scene, workload, grid and clipping executables keep the original
prepared palette. `chunk-skin-compact.elf` is an explicit grid/clipping comparison
variant, selected by `CHUNK_SCENE_COMPACT_PALETTE`. It uses the same authored
geometry, normal, lighting, UV and clipping checks as `chunk-skin-clip.elf` and
reports `skin_palette=compact joint_bytes=84`. Compare those two variants on
physical hardware before treating smaller storage/fewer FIPRs as a speedup.

The synthetic `sh4zam/integration/skin-bench.elf` now includes a
`compact+weights` application lane for both fixed-four and variable-span plans,
plus a separate `compact-palette-setup` timing mode. It compares twelve workloads
against an independent double-precision oracle, rotates timed lane order, and
reports compact and original palette storage separately. This is a console
measurement harness, not evidence that compact skinning is faster. See its
[methodology](../examples/dreamcast/sh4zam/integration/README.md#skinning-workload-benchmark).

The existing scene workload reports full pose/draw timings and can be used for
physical-console comparisons. No Dreamcast hardware speedup is claimed from
host or emulator results. Default-path adoption of the optional compact palette
still needs representative physical-console comparisons.

## Validation and reproduction

The dedicated suite compares noncommuting, sheared, reflected, translated,
nonuniformly scaled transforms against a scalar multiplication oracle and the
original palette builder over 24 poses. It also exercises existing prepared
skinning, immutable snapshot copies, late singular/overflow errors, non-affine
and NaN rejection, capacity/overlap/size failures, and full XMTRX preservation.
The hierarchy tests compare 24 animated poses against general 4x4 traversal,
with all suppression combinations, hidden nodes, interleaved subtrees, multiple
roots, nonunit quaternions, root shear/reflection, negative/nonuniform and zero
scale. They also cover copied topology, rejected pruning, boundary overlap,
late invalid TRS/overflow, empty publication on failure, workspace reuse, and
compact matrices aligned to four bytes but not eight or thirty-two.
Compact skinning is compared against the existing prepared consumer over the
same 24 poses, including importer/direct-builder agreement, strided inputs,
in-place operation, repeated/shared spans, zero weights, late invalid sources,
arithmetic overflow, collapsed normals, normalized-to-zero positive weights,
palette version/count/overlap failures, non-affine rejection, and late palette
construction failure without publishing partial output.
The fixed-four tests additionally cover all one-through-four active counts,
noncontiguous active slots, ignored invalid inactive indices, copied source
weights, normalized-to-zero active weights, and failed-plan/count/alignment
admission. Both consumers share palette framing and arithmetic without adding
per-influence calls or validation scans.

Validation recorded across these integration passes:

- Dedicated suite: GCC 14 GNU17/C23, Apple Clang 16 GNU17/C2x, and Clang
  AddressSanitizer/UndefinedBehaviorSanitizer all passed.
- GCC 16.2 SH-4 KOS build, all nine exports, dedicated ELF, and all five scene
  variants built successfully.
- GCC host scene/grid/clipping integration passed its authored geometry,
  normal, lighting, UV, clipping, packet-guard, and failure-cleanup checks,
  including the additional compact-palette grid/clipping variant.
- Isolated 16 MiB MMU-capable Flycast: dedicated ELF passed interpreter and
  dynarec; scene passed interpreter; grid and clipping passed dynarec. These
  establish emulator behavior, not physical-console timing or precision bounds.
- Compact-palette pass: dedicated and 144-frame compact grid/clipping ELFs
  passed both interpreter and dynarec. The default clipping variant passed
  dynarec again. Existing deformation tests passed the four compiler/language
  lanes and sanitizers; Compact skin asset and skin benchmark regressions passed.
- Fixed-four compact pass: dedicated, deformation, and twelve-case benchmark
  suites passed all four compiler/language lanes and Clang ASan/UBSan. The
  final inlined SH-4 build passed the dedicated suite in interpreter and
  dynarec, plus the benchmark and compact grid/clipping scene in dynarec.
  Both compact influence loops retain six FIPRs without a helper call. The
  public header passed the SH-4 GNU++23 check, and the official v0.9.0 source
  pin and all eleven source-verifier tests passed unchanged.

```sh
make -C utils/pvr-skeleton-affine-test CC=gcc-14 test
source environ.sh
make -j4
make -C utils/pvr-skeleton-affine-test dreamcast
make -C utils/pvr-chunk-scene-integration-test CC=gcc-14 test
make -C examples/dreamcast/pvr/chunk_scene -j4
```

Run `pvr-skeleton-affine-test.elf` and verify its `RESULT: PASS` line. The scene
executables print `KOSSCENE result=PASS` after their authored pose/render checks.
