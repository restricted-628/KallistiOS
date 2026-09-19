# Multi-model Compact scene integration

This companion to `chunk_asset` compiles an authored glTF scene into PCM2 at
build time. It joins existing model-table, hierarchy, skeleton, general-skin,
sparse-morph, animation-catalog, transform-clip, morph-weight, and cooked-cache
APIs. It does not introduce a scene graph, renderer, resource manager, or new
library API.

The fixture has two opaque, untextured triangles with different materials, a
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

## Recorded validation

On 2026-09-05, the full GNU17 host sweep passed 52/52 suites. This integration
suite also passed GCC 14 strict C23, Clang strict C2x, and ASan/UBSan, including
truncation and second-model corruption cleanup. The SH-4 example built and
passed its numerical checks and render completion in Flycast interpreter and
dynarec modes; both colored models were visually inspected. Doxygen built
successfully. No physical-hardware result is implied by these checks.
