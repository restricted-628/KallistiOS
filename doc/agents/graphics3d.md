# 3D geometry, animation and rendering styles

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Graphics**.

## Scope and entry points

Compact models/scenes, transforms, culling, collision, deformation, skeleton/skin/morph, lighting, materials and toon/wire paths.

- `kernel/arch/dreamcast/math`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_model.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_scene.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_render.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skeleton_affine.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_skin.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_lighting.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_material.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_toon.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/high-level-3d-capability-audit.md`
- `doc/pvr-chunk-model.md`
- `doc/pvr-chunk-rendering.md`
- `doc/pvr-deformation.md`
- `doc/pvr-lighting.md`
- `doc/pvr-material-recipes.md`
- `doc/animation.md`

## Preserve these boundaries

- Keep matrix order/layout, homogeneous lanes, normal transforms and bounds explicit; 3x4 affine paths are not arbitrary projective matrices.
- Use SH4ZAM directly; approximate power is accepted for specular use but domains/error impact still need evidence.
- Caller-owned workspace and prepared data must outlive submission. Cache/OCRAM policy belongs to the kernel, not renderer-owned register mutation.
- Preserve geometry/material/list compatibility and test style combinations relevant to the change. High-level addon extraction remains unfinished.

## Coordinate before changing

- SH4ZAM
- Assets/compression and texture lifetime
- PVR pipeline
- Cache/MMU/SQ and fiber XMTRX context

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/animation-test` — candidate `make -C utils/animation-test test` in an assigned checkout.
- `utils/collision-test` — candidate `make -C utils/collision-test test` in an assigned checkout.
- `utils/matrix-camera-test` — candidate `make -C utils/matrix-camera-test test` in an assigned checkout.
- `utils/matrix-compose-test` — candidate `make -C utils/matrix-compose-test test` in an assigned checkout.
- `utils/matrix-stack-test` — candidate `make -C utils/matrix-stack-test test` in an assigned checkout.
- `utils/pvr-geometry-test` — candidate `make -C utils/pvr-geometry-test test` in an assigned checkout.
- `utils/pvr-chunk-model-test` — candidate `make -C utils/pvr-chunk-model-test test` in an assigned checkout.
- `utils/pvr-chunk-render-test` — candidate `make -C utils/pvr-chunk-render-test test` in an assigned checkout.
- `utils/pvr-chunk-scene-integration-test` — candidate `make -C utils/pvr-chunk-scene-integration-test test` in an assigned checkout.
- `utils/pvr-skeleton-affine-test` — candidate `make -C utils/pvr-skeleton-affine-test test` in an assigned checkout.
- `utils/pvr-chunk-skin-test` — candidate `make -C utils/pvr-chunk-skin-test test` in an assigned checkout.
- `utils/pvr-lighting-test` — candidate `make -C utils/pvr-lighting-test test` in an assigned checkout.
- `utils/pvr-material-test` — candidate `make -C utils/pvr-material-test test` in an assigned checkout.
- `utils/pvr-toon-test` — candidate `make -C utils/pvr-toon-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/pvr/material_gallery`
- `examples/dreamcast/pvr/chunk_toon`
- `examples/dreamcast/pvr/chunk_wire`
- `examples/dreamcast/pvr/chunk_skin`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Uncommitted normal/packet candidates remain experiments; do not enable them by default based on old host/emulator notes.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
