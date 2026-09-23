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

The existing scene workload reports full pose/draw timings and can be used for
physical-console comparisons. No Dreamcast hardware speedup is claimed from
host or emulator results. Compact application palettes remain a further
candidate, requiring comparisons against conversion, alignment, and XMTRX
ownership costs before adoption.

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

Verified in this pass:

- Dedicated suite: GCC 14 GNU17/C23, Apple Clang 16 GNU17/C2x, and Clang
  AddressSanitizer/UndefinedBehaviorSanitizer all passed.
- GCC 16.2 SH-4 KOS build, all five exports, dedicated ELF, and all four scene
  variants built successfully.
- GCC host scene/grid/clipping integration passed its authored geometry,
  normal, lighting, UV, clipping, packet-guard, and failure-cleanup checks.
- Isolated 16 MiB MMU-capable Flycast: dedicated ELF passed interpreter and
  dynarec; scene passed interpreter; grid and clipping passed dynarec. These
  establish emulator behavior, not physical-console timing or precision bounds.

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
