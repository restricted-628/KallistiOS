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
preserves the original authored hierarchy, inverse binds, clip, and opposing
morph curves. The generated glTF and PCM2 are build products, not external or
proprietary model assets.

Across both meshes there are 578 source vertices, 1,024 triangles, and 32
joined strips of 34 vertices each (1,088 emitted packets per frame). Two-joint
weights vary continuously by row; interior vertices exercise both influences.
Each mesh still has one sparse morph delta, on its last vertex. Normals point
along +Z and generated triangles have matching winding.

The converter binds UV0 to authored texture ID 7. The application verifies that
binding and the bounded cache layout once during loading, then allocates and
uploads a 64-by-64 RGB565 checker texture before rendering. A single opaque,
bilinear, modulated texture header serves both authored material colors. The
texture is released after rendering drains. No texture lookup, upload, static
strip validation, or heap allocation is added to the frame loop.

The six pose goldens independently derive every source vertex's expected
position from its grid row, joint blend, and mesh-specific morph curve. Packet
checks verify source-index resolution, position, material color, every strip
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
certification or a performance benchmark. It uses only two joints, translation
animation, one sparse delta per mesh, and one small resident texture. It does
not establish many-joint scaling, rotational skinning coverage, texture
streaming/bandwidth, clipping of skinned models, lighting under changing
normals, or physical-hardware performance. Hardware testing and addon
extraction remain deferred.

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
