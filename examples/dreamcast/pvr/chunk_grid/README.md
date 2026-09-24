# Imported grid and homogeneous clipping workload

`chunk-grid.elf` complements `chunk_scene/chunk-workload.elf`: instead of
repeating two tiny meshes, it loads one independently authored 32-by-16-cell
grid with 561 shared positions and 1,024 triangles. The build-time Python
generator emits OBJ faces in explicit triangle-strip order. The Compact
converter's `--join-strips` option produces 16 strips of 66 vertices; no
proprietary model or implementation is used. The embedded PCM2 asset is raw
and CRC-checked during loading. No input file I/O is timed on Dreamcast.

The example builds the existing page-indexed vertex plan once. It uses
`pvr_chunk_model_emit_clipped_prepared()` rather than introducing a new renderer
or claiming a cooked-cache clipping fast path. That API still traverses polygon
records and assembles strips per emission; the plan avoids repeated vertex
searches. Existing ordinary-cache benchmarks cover a different path. The grid
is static and untextured: it does not add larger skinned assets, many-joint
animation, texture streaming, or lighting benchmarks.

## Six cases and independent checks

| Case | Policy | Expected geometric result |
| --- | --- | --- |
| Visible | ASSUME_VISIBLE | All 1,024 triangles, 163,840 square screen units |
| Side crop | SPLIT | Fractional-cell rectangle, area 57,040 |
| Side crop | DROP | 280 wholly inside triangles, area 44,800 |
| Near/far W | SPLIT | Projected trapezoid spanning object X=12.25 through 28.25 |
| Near/far W | DROP | 480 triangles spanning object X=13 through 28 |
| Outside | SPLIT | Whole-model rejection, zero packets |

The depth matrix uses W=.25+X/16. Near/far bounds deliberately cut inside cells;
clipping must precede division by W. An independent double-precision trapezoid
formula computes the expected projected area. Packet checks sum triangle-strip
areas, verify complete strip commands, finite positions and positive reciprocal
W, crop bounds, and inverse-project coordinates back into the authored grid.
An affine red/green gradient checks interpolated base colors (within 1.1 byte
levels); alpha, blue, and offset color stay exact. Area error must be at most
0.2 square screen units. These bounded tolerances are fixture-specific, not
universal precision guarantees.

Each case also compares prepared and non-prepared output byte-for-byte and
checks the first unwritten packet's sentinel. A deliberately undersized memory
sink must return ENOSPC without publishing any vertex or changing its guard.
The host harness additionally tests truncated and CRC-damaged assets, including
cleanup under sanitizers. Host hardware-submission stubs abort if reached.

## Rendering, timing, and ownership

The target first runs the same memory-sink checks as the host. It then submits
20 frames per case: four warmup frames and 16 measured frames, 120 frames in
total. The serial `mean_emit_us` is an integer mean of CPU emission intervals:
frustum construction, polygon traversal, vertex policy, clipping/projection,
header callbacks, and direct PVR submission. It excludes frame-ready waits,
scene/list begin and finish, loading, correctness preflight, and reporting.
Interrupts remain enabled. GPU work overlaps submission; this is neither GPU
duration nor uncapped FPS. Fixed case order and a simple mean are suitable for
execution characterization, not a comparative optimization decision. Emulator
timings must not be presented as physical-console measurements.

Every rendered frame checks its emitted count against the validated memory
result. Each case drains rendering and checks persistent PVR fault state before
printing its result. Final cleanup displays a green PASS or red FAIL card.
There is no per-frame allocation, retained scene graph, worker, or fiber. The
caller owns the decoded asset storage, page index, strip workspace, clipping
scratch, and output buffers. Two conservative memory-output buffers each hold
21 vertices per source triangle plus one guard (688,160 bytes each); they exist
for correctness preflight, not as required renderer overhead or GPU staging.
Both output buffers are freed before PVR startup; only the expected counts
remain for per-frame checks.
The strip scratch is 2,112 bytes for this fixture and the clip scratch 672 bytes.

Build from a sourced KOS environment with `make -C examples/dreamcast/pvr/chunk_grid`.
Run the shared desktop checks with `make -C utils/pvr-chunk-grid-test test`.
Check complete emulator/console serial logs with
`python3 utils/pvr-chunk-grid-test/test.py --log run.log` (this verifies report
completeness, not hardware provenance or timer accuracy).
Physical validation and benchmark-driven production changes remain deferred.

## Recorded validation

On 2026-09-20 the shared grid suite passed GCC 14 and Clang GNU17, GCC 14
strict C23, Clang strict C2x, and Clang ASan/UBSan. All six geometric cases,
prepared/checked packet parity, guards, short-sink rejection, truncated input,
and CRC-failure cleanup passed. SH-4 GCC 16.2 built the example. Flycast
interpreter and dynarec each passed the same geometric preflight, all six
render cases (120 frames), and final cleanup; complete serial logs were checked
with the host harness. These are execution checks, not visual certification or
physical-console performance results. No production renderer or upstream
SH4ZAM source was changed.
