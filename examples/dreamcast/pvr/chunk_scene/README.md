# Multi-model Compact scene integration

This companion to `chunk_asset` compiles an authored glTF scene into PCM2 at
build time. It joins existing model-table, hierarchy, skeleton, general-skin,
sparse-morph, animation-catalog, transform-clip, morph-weight, and cooked-cache
APIs. It does not introduce a scene graph, renderer, resource manager, or new
library API.

The original fixture has two opaque, untextured triangles with different materials, a
translated root, and two joints. The base vertices follow the base joint; the
top vertex follows the animated tip joint. Each model has one sparse shape
target. The two mesh instances have opposing, independently serialized morph
curves in the same logical `bend` clip.

The application resolves metadata by semantic type and model/clip ordinals,
never by hardcoded directory positions. It samples the complete hierarchy,
builds position and inverse-transpose normal palettes from the serialized
inverse binds, applies morphs before skinning, and resolves completed vertices
into the ordinary prepared-cache renderer. Skin output is already in scene
world space; the mesh hierarchy transform must not be applied a second time.
The left/right screen placement is an explicit application display transform.

Each ordinary cooked cache is admitted once with
`pvr_chunk_model_cache_draw_prepare()` during loading. Host golden checks and
target frames then call `pvr_chunk_model_cache_draw_emit()`, avoiding static
strip/base-data rescans. The cache and draw snapshot stay immutable; animated
resolver output is still checked each frame.

General-skin spans are also validated and normalized once during model loading
with `pvr_skin_spans_prepare_query()` / `pvr_skin_spans_prepare()`. Fixed fixture
arrays hold the queried run and weight counts. Each sampled palette is imported
once per model with `pvr_skin_palette_prepare()`, then
`pvr_skin_apply_spans_prepared()` skins the changing morphed vertices. The weight
plan stays immutable across poses; its original decoded inputs are not borrowed.
Output arithmetic and changing vertex data remain checked. This removes static
weight scans/divisions, not the dynamic morph or skeleton evaluation work.

## Checks and expected result

Before rendering, the example evaluates times 0, .25, .5, 1, 1.5, and 2 seconds.
Independently calculated positions check the translated root, inverse-bind
offset, joint motion, and separate morph weights. The same prepared emitter
then writes a memory sink, checking source-index resolution, projected
positions, command order, and preserved material colors. These checks also
run on the desktop from the exact same C source:

```sh
make -C utils/pvr-chunk-scene-integration-test test
```

The Dreamcast example renders 240 frames of an orange/red left triangle and a
blue right triangle. Both tips move with the joint while their opposing morph
curves produce different shapes. It holds the final frame for ten seconds,
checks PVR fault status, cleans up, and displays a persistent green PASS or red
FAIL card. The serial log includes the original failing stage and errno.

The input is intentionally tiny and fixed-capacity. This is a conformance
fixture, not an arbitrary-model viewer. The host build aborts if a hardware
submission or matrix-register function is accidentally reached. The normal
GNU17 lane, separate GCC/Clang C23 lanes, and sanitizers can all run this test.

## Ownership and cost

Application storage owns all model views, pose arrays, palettes, deformation
workspaces, and section bindings. Cache storage and any required persistent
stream decode storage are queried, allocated once, and freed after rendering.
There is no per-frame heap allocation, service thread, fiber, or hidden clock.
The raw embedded asset deliberately keeps this test independent of LZ4;
`chunk_asset` separately covers cooperative compressed loading and teardown.

Physical hardware is still required to validate numerical tolerances and
cache/store-queue behavior outside emulator coverage. This fixture does not
claim exhaustive content-import or rendering-policy coverage.

## Authored-asset draw workload

The same Makefile also builds `chunk-workload.elf` from the same scene loader
and pose code. This is a repeated-instance workload, **not** a larger or more
complex asset: the two original three-vertex meshes are loaded once and reused
in grids of 1, 16, and 256 pairs (2, 32, and 512 triangles per frame).

Each grid runs two cases. Shared-pose mode samples the hierarchy, morphs, and
skinning once per frame and reuses those two completed meshes for every pair.
Independent-pose mode samples each pair at its own deterministic clip phase;
scratch is reused only after that pair has been submitted. Both run the same
prepared-cache path, a checked ambient-plus-directional lighting callback, and
direct PVR list submission. Screen placement is applied after world-space
skinning. No per-frame allocation, asset reload, texture binding, or new
renderer API is introduced. This exercises draw-call/pose scaling; it does not
exercise large strips, many joints, texture bandwidth, clipping, streaming,
OCRAM, or independently retained instance state. Those are separate workloads.

Before any target rendering, the workload ELF and the host suite run 18
memory-sink checks: three grids, two pose modes, and three frame phases. The
original `chunk-scene.elf` keeps its small conformance loop. The independent
pose goldens above are retained. Workload checks verify projected coordinates,
the analytically expected 0.75 lighting factor on the authored +Z normals,
vertex command flags, emitted counts, and a guarded output tail.

Each target case has eight warmup frames followed by 24 measured frames.
`KOSWORKLOAD` serial records report min/median/max microseconds for:

- `pose`: accumulated sampling, hierarchy, palette, morph, and skin work;
- `draw`: accumulated prepared emission, lighting, and direct submission;
- `ready`: time waiting for PVR admission before opening the frame;
- `cpu-frame`: scene/list begin through scene finish, excluding `ready`.

The CPU-frame interval includes per-draw result checks, placement, bookkeeping,
and timer overhead, so it is not simply pose plus draw. Interrupts stay enabled;
pose/draw intervals include any interruption while they execute. Timers around
every pose and draw add overhead, especially at the smallest sizes. There are
no serial writes inside the frame loop. The median is the integer mean of the
middle two sorted samples. Cases run in fixed ascending-size, shared/independent
order; this is not a randomized comparative benchmark. Each case drains rendering
before reporting and verifies persistent PVR fault state. `drain_us` includes
that final wait and status check, not the sum of all GPU rendering times. GPU
work overlaps CPU work and frame admission may include display synchronization;
these numbers are not standalone GPU time or uncapped game FPS.

There are six cases and 192 submitted frames in total. The final 256-pair image
is held for ten seconds before cleanup. Host tests deliberately have no timing
source. Flycast runs establish execution/correctness only; physical-console
performance measurements remain deferred.

Check complete serial logs (including the final cleanup result) with:

```sh
python3 utils/pvr-chunk-scene-integration-test/check-workload-log.py run.log
```

The checker rejects missing/duplicate cases, missing stages, wrong counts,
unsorted summary statistics, and a missing final PASS. It cannot establish
that a log came from physical hardware or validate the timer's accuracy.

## Animated textured grid integration

`chunk-skin-grid.elf` reuses the same serialized scene loader and pose pipeline,
but `generate-skin-grid.py` expands each mesh into a 16-by-16-cell grid. It
preserves the original authored hierarchy, inverse binds, translation, and
opposing morph curves, and adds tip-joint rotation/scale tracks to the clip.
The generated glTF and PCM2 are build products, not external or
proprietary model assets.

Across both meshes there are 578 source vertices, 1,024 triangles, and 32
joined strips of 34 vertices each (1,088 emitted packets per frame). Two-joint
weights vary continuously by row; interior vertices exercise both influences.
Each mesh still has one sparse morph delta, on its last vertex. The grid lies
on Z=X/2, with unit normal (-1,0,2)/sqrt(5) and matching triangle winding.
Over the 0..1..2 second clip the tip rotates 0..90..0 degrees about Y while
its scale changes (1,1,1)..(2,1.5,.5)..(1,1,1). The tilted input normal makes
using a position matrix instead of an inverse-transpose normal matrix visible
even for vertices influenced by only the tip joint.

The converter binds UV0 to authored texture ID 7. The application verifies that
binding and the bounded cache layout once during loading, then allocates and
uploads a 64-by-64 RGB565 checker texture before rendering. A single opaque,
bilinear, modulated texture header serves both authored material colors. The
texture is released after rendering drains. No texture lookup, upload, static
strip validation, or heap allocation is added to the frame loop.

The six pose goldens independently derive every source vertex's expected
position and normal from scalar double-precision formulas in `grid-goldens.h`,
not the matrix/animation/skinning helpers under test. They cover inverse-bind
pivot order, T*R*S composition, morph-before-skin order, and a root translation
applied only once. The normal contract is to blend the joint inverse-transpose
results and then normalize, not to reconstruct geometric normals from the
deformed triangles. Position, normal components, and normal squared length
use a 0.0005 absolute tolerance.

Both the memory-sink checks and target rendering apply a +Z directional light
with .2 ambient and .65 diffuse intensity. Independent packed-color goldens
allow one RGB code point at quantization boundaries and require exact alpha.
The callback exercises the existing checked lighting API one sample at a time;
this is a conformance path, not an optimized lighting-throughput benchmark.
Packet checks verify source-index resolution, position, lit material color, every strip
terminator, UV coordinates, and an output-tail guard. The host suite also checks
the generator's winding, weights, and sparse delta, plus truncated input and
second-model skin corruption cleanup. These checks run alongside the original
triangle and repeated-instance tests, which are unchanged in purpose.

The target draws 240 animated frames, checks PVR fault state, holds the last
image for ten seconds, and releases all owned resources before final PASS.
Check a complete serial report with:

```sh
python3 utils/pvr-chunk-scene-integration-test/test-skin-grid.py --log run.log
```

This is larger combined-path conformance coverage, not a game-asset importer
certification or a performance benchmark. It uses only two joints, positive
non-singular scales, a single-axis rotation, one sparse position delta per mesh,
and one small resident texture. It does not establish many-joint scaling,
negative/singular scale policy, normal morph deltas, texture streaming/bandwidth,
physical-hardware performance. The separate fixture below adds clipping of skinned
models. Hardware testing and addon
extraction remain deferred.

The tilted-normal fixture exposed a converter bug: glTF normals were retained
only as strip attributes, so indexed skin/morph source construction fell back
to +Z. The converter now also emits glTF per-vertex normals in the existing
indexed normal-bearing record types. Rebuild previously converted PCM2 assets
to obtain the fix; old assets are not silently repaired by the runtime.

## Animated model clipping

`chunk-skin-clip.elf` reuses the textured grid, serialized animation, independent
pose/normal goldens, and checked lighting above. It adds six cases: wholly
visible, side-plane SPLIT/DROP, depth-plane SPLIT/DROP, and wholly outside.
Each model occupies its own half-screen pane. Depth cases vary homogeneous W
with both X and Z, so rotation/scale/morph affect the near/far crossings.

Clipping uses `pvr_chunk_model_emit_clipped_prepared()`, with a callback resolving
the completed skinned pose before projection. This is the **prepared vertex-index
plan** path, not the admitted cooked-cache draw path used by `chunk-skin-grid`.
The clipped emitter still traverses the strip stream. This fixture does not add
a clipped cooked-cache API or claim to optimize that traversal.

After each pose, the example scans its completed positions once with
`pvr_deform_bounds_calculate()`. A private copy of the admitted plan receives that
center/radius, while borrowing the original immutable streams/index table. The
asset, rest-pose plan, and cache remain unchanged. Both whole-model rejection
and the wholly-inside clipping bypass require a conservative **current-pose**
bound; the serialized rest-pose sphere alone is not safe for animation.

Before rendering, 72 memory-sink checks cover six poses, two models, and six
cases. Independent double-precision formulas reconstruct the authored triangles
and clip convex polygons in a different plane order, comparing projected area
with a tolerance of 0.5 square pixels plus 0.005 percent. UV coordinates locate
each emitted point in an original grid triangle; barycentric reconstruction then
checks position (0.02 pixels), reciprocal W (0.0002), lit base color, and a
synthetic offset-color gradient. Packed colors allow four code points for
successive intersection quantization and lighting rounding; base alpha is exact.
All six plane crossings must occur. These are numerical checks, not screenshot
comparisons.

The checks also require raw/prepared emission byte parity, expected strip or
independent-triangle terminators, exact DROP counts, no callback on whole-model
rejection, and guarded output tails. A one-packet-short sink must fail with
ENOSPC, report its valid prefix, and leave its capacity guard intact. Existing
truncated-asset and second-model skin-corruption cleanup tests cover this new
executable too. The original grid and small-scene tests remain in the host suite.

The target renders 24 animated frames per case (144 total), drains rendering,
checks PVR fault state, holds the last frame, and frees owned storage before final
PASS. The final wholly-outside case is intentionally blank. Scratch is bounded:
34 strip vertices, 21 clip vertices, caller-owned pose arrays, and no per-frame
allocation. The two temporary worst-case conformance output buffers are allocated
before PVR initialization and released before rendering. Per-vertex lighting and
raw-path parity are conformance costs, not a performance benchmark.

```sh
python3 utils/pvr-chunk-scene-integration-test/test-skin-grid.py --clip-log run.log
```

The serial checker requires all checks, 144 frames, six cases, zero PVR faults,
and final cleanup PASS, rejecting missing/duplicate/reordered or altered records.
Emulator execution cannot establish physical-console performance or pixel-level
visual correctness. Many-joint scaling, mirrored/singular transforms, normal
morph deltas, texture streaming, and hardware validation remain separate work.

## Recorded validation

On 2026-09-05, the full GNU17 host sweep passed 52/52 suites. This integration
suite also passed GCC 14 strict C23, Clang strict C2x, and ASan/UBSan, including
truncation and second-model corruption cleanup. The SH-4 example built and
passed its numerical checks and render completion in Flycast interpreter and
dynarec modes; both colored models were visually inspected. Doxygen built
successfully. No physical-hardware result is implied by these checks.

On 2026-09-20, the added workload checks and existing malformed-asset cleanup
tests passed GCC 14/Clang GNU17, GCC 14 strict C23, Clang strict C2x, and
Clang ASan/UBSan. The serial-log checker passed positive and negative tests.
SH-4 GCC 16.2 built both ELFs; Flycast interpreter and dynarec each completed
all six workload cases, 192 rendered frames, and final cleanup with passing
serial-log verification. The original small scene also completed a dynarec
smoke run. These are execution checks; no new visual or hardware-performance
certification is claimed.

Also on 2026-09-20, the animated textured grid and original scene host tests
passed GCC 14/Clang GNU17, GCC 14 strict C23, Clang strict C2x, and Clang
ASan/UBSan, including both malformed-input cleanup paths. Generator invariants
and positive/negative serial-log tests passed. SH-4 GCC 16.2 built all three
scene ELFs. Flycast interpreter and dynarec each passed the grid goldens,
240 rendered frames, zero reported PVR faults, and final cleanup; the serial
checker accepted both logs. The original small scene also passed a dynarec
smoke test. Host/target grid PCM2 files were byte-identical. These are targeted
integration and execution results, not visual certification, a full host-suite
sweep, or physical-hardware validation. No SDK implementation or SH4ZAM pin was
changed in this batch.

The rotation/scale/lighting follow-up on 2026-09-20 first reproduced a +Z
fallback normal at time zero on the tilted grid, then passed after the indexed
glTF normal fix. The scene suite passed GCC 14/Clang GNU17, GCC 14 strict C23,
Clang strict C2x, and Clang ASan/UBSan. The converter's complete regression
suite and new indexed-normal/mixed-layout/4-, 6-, and 7-word boundary tests
passed Clang GNU17, GCC 14 strict C23, Clang strict C2x, and Clang ASan/UBSan.
The strict Clang converter build used the existing test-runner exception
`HOST_LZ4_WARNINGS=-Wno-constant-logical-operand` for bundled LZ4; no dependency
source was changed.

SH-4 GCC 16.2 rebuilt all three scene ELFs. Flycast interpreter and dynarec
each passed the TRS, normal, lighting, UV, and packet checks, all 240 rendered
frames, zero reported PVR faults, and final cleanup. The serial checker accepted
both logs, the original small scene passed a dynarec smoke test, and the host
and target grid PCM2 files were byte-identical. The SH4ZAM source-pin check
remained clean. This was targeted converter/integration validation, not a full
host-suite sweep, visual certification, or physical-hardware validation.

The animated-clipping follow-up on 2026-09-20 passed the complete scene
integration suite (small scene, textured grid, and clipped grid) under GCC 14
and Clang GNU17, GCC 14 strict C23, Clang strict C2x, and Clang ASan/UBSan,
including truncated-input and second-model corruption cleanup. Generator and
positive/negative serial-checker tests passed. SH-4 GCC 16.2 built all four
scene ELFs. Flycast interpreter and dynarec each passed all 72 clipping checks,
144 rendered frames, zero reported PVR faults, and final cleanup; the serial
checker accepted both complete logs. The original small scene passed a dynarec
smoke run, host/target grid assets were byte-identical, and the SH4ZAM pin check
remained clean. No new runtime defect was exposed by this fixture, and neither
the core clipping implementation nor SH4ZAM was changed. Physical hardware,
pixel-level visual verification, and a full repository host-suite sweep were
not part of this batch.
