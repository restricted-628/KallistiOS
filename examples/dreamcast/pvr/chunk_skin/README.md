# Explicit compact-model skinning

This example binds one canonical four-joint influence record to every vertex
in a prepared compact model. It builds the sparse constant-time pose lookup,
canonical deformation source, and ordinary-strip draw cache once in
caller-owned storage, then samples a four-joint palette for 120 frames. The
three vertices have one, two, and four active influences. Joints rotate around
the triangle's center with distinct nonuniform XYZ scales. Authored normals
are tilted away from the Z axis, so rotation and inverse-transpose scaling
actually change the lighting normals.

Each frame prepares the sampled joint palette into caller-owned SH4ZAM
matrices, then uses `pvr_skin_apply_prepared()` to produce a dense
pose. A prepared palette can be shared by multiple meshes; this small example
has only one. The immutable four-weight plan is validated and normalized once
before the frame loop; changing vertex values and arithmetic remain checked. The
draw cache resolves each retained original model index through that pose,
shades from the deformed normal, projects its already assembled PVR-native
vertex run, and emits the triangle through the established PVR list sink.
Neither compact stream is reparsed in the frame loop.
The immutable cache is admitted once with
`pvr_chunk_model_cache_draw_prepare()`; frames use
`pvr_chunk_model_cache_draw_emit()` so static cache metadata is not rescanned.
Dynamic pose resolution, shading, projection, and sink checks are retained.

The example allocates no hidden runtime state and starts no worker or service.
It prints `RESULT: PASS (explicit compact skinning)` after checking the
deformation count, render progress, and persistent PVR fault state.
This is correctness coverage, not a hardware throughput benchmark.

## Compact-palette comparison

`make` builds two executables from the same source and authored model:

- `chunk-skin.elf` keeps the original prepared palette and
  `pvr_skin_apply_prepared()` path.
- `chunk-skin-compact.elf` selects `CHUNK_SKIN_COMPACT`, imports the sampled
  palette with `pvr_skin_palette_prepare_compact()`, and applies the same
  immutable weights using `pvr_skin_apply_compact()`.

The first console line identifies the palette and record size: 104 bytes per
original joint or 84 per compact joint on the tested builds. Each executable
stores only its selected prepared representation, but both retain the source
4x4 and normal arrays to demonstrate the palette importer. This is not the
direct compact hierarchy/palette producer; see
[`chunk_scene`](../chunk_scene/README.md) for that integration.

Before every draw, an independent double-precision oracle evaluates analytic
rotation/scaling about the center, inverse-transpose normal transformation,
weighted blending, and final normal normalization. It uses the original
quantized weight table, not the prepared plan. The sample uses SH4ZAM sin/cos;
double libm is confined to the oracle. Absolute bounds are 0.0002 position
units and 0.00002 normal-component units on host, or 0.075 screen pixels and
0.0003 normal-component units on SH-4 (allowing FSCA angle quantization).
These are fixture-specific acceptance bounds, not universal precision claims.
Both paths also check homogeneous W values and an untouched output-tail guard.

`make -C utils/pvr-skin4-scene-test CC=gcc-14 test` from the KOS root runs both
variants through the same model admission, binding, source decoding, immutable
weight preparation, palette import, deformation and numerical checks on host.
Only video/PVR setup, draw-cache emission and shutdown are excluded there.
On Dreamcast/Flycast, both variants additionally exercise the actual cached
draw, lighting policy and list submission for all 120 frames, and reject any
persistent PVR fault. Neither path adds production-library checks or changes
the default palette used by other examples. Real-console performance remains
unmeasured; use `sh4zam/integration/skin-bench.elf` for controlled timings.
