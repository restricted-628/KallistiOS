# 2D cells, sprites, tilemaps and particles

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Graphics**.

## Scope and entry points

Cell sprites/assets, tilemaps, sprite geometry, particles and their showcase consumers.

- `kernel/arch/dreamcast/hardware/pvr/pvr_cell.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_cell_asset.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_tilemap.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_particle.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_sprite_geometry.c`
- `kernel/arch/dreamcast/include/dc/pvr_cell.h`

Relevant existing documentation (dated claims must be rechecked):

- `doc/pvr-cell-sprites.md`
- `doc/pvr-cell-assets.md`
- `doc/pvr-tilemaps.md`
- `doc/pvr-particles.md`

## Preserve these boundaries

- Use direct SH4ZAM math with documented domains; preserve caller-owned workspace and output-capacity contracts.
- Keep asset decoding separate from packet emission and texture residency; validate indices, counts, alignment and clipping assumptions.
- Check opacity/list/material compatibility and ordering rather than assuming all styles share one path.
- Maintain bounded particle/tile traversal and deterministic failure behavior; do not add hidden per-frame allocation or mandatory services.

## Coordinate before changing

- Assets/textures for formats
- Video/PVR for packets/lists
- SH4ZAM for math contracts

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/pvr-cell-test` — candidate `make -C utils/pvr-cell-test test` in an assigned checkout.
- `utils/pvr-tilemap-test` — candidate `make -C utils/pvr-tilemap-test test` in an assigned checkout.
- `utils/pvr-particle-test` — candidate `make -C utils/pvr-particle-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/pvr/sprite_cells`
- `examples/dreamcast/pvr/cell_asset`
- `examples/dreamcast/pvr/tilemap`
- `examples/dreamcast/pvr/particles`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

A showcase demonstrates only its selected configuration; do not generalize to every hardware style combination.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
