# Cell-sprite animation and composition

`dc/pvr_cell.h` adds a caller-owned layer above the established sprite atlas
and packet compiler. It supplies the missing relationship between reusable
cells, independently timed step streams, whole-sprite motion, signed priority,
material routing, and per-corner color without creating a retained scene or
animation manager.

The ownership path is explicit:

```text
application clock and stream views
        -> sampled caller-owned cell states
        -> resolved caller-owned cells
        -> compact hardware sprites or colored quad strips
        -> existing material/list submission
```

No function allocates, starts a worker, retains an input pointer after return,
or performs work unless called. Applications that use only the original sprite
geometry pay no runtime or storage cost for cell streams.

## Cell state and streams

A `pvr_cell_state_t` selects one atlas cell and carries its local offset,
rotation, scale, signed priority, visibility/flip flags, application material
identifier, and four diffuse plus four offset colors. The material identifier
is routing metadata; this layer deliberately does not own textures or submit
parameter headers.

A key replaces only fields named by its mask. Stream keys are ordered by
nondecreasing timestamp, allowing multiple slots to change together. Each
stream has its own offset, maximum time, and one-shot or repeat policy. A
repeating stream maps into `[0, time_max)`; a nonrepeating stream clamps into
`[0, time_max]`.

`pvr_cell_stream_list_sample()` first copies the sprite's complete base state
and then applies streams in list order. Later streams override only the fields
they name, so separate eye, mouth, equipment, and body streams can operate on
the same cell sprite without duplicating unrelated state. Output is published
only after every stream succeeds. A second caller-owned cell array is the
failure-atomic workspace.

Playback clocks and timestamp event meaning remain in the generic animation or
application layer. This keeps independently repeatable cell streams usable
without introducing a mandatory global clock.

`pvr_cell_stream_collect_events()` traverses an admitted generic event track
on one stream's own offset/repeat time base. It counts full repeated cycles
arithmetically and publishes only up to caller capacity in chronological order,
so a delayed frame cannot create unbounded work.

## Composition and priority

`pvr_cell_sprite_resolve()` composes every sampled local cell under one
position, rotation, and scale. It retains the source slot, signed priority,
material identifier, and globally modulated per-corner colors beside an
embedded `pvr_sprite_instance_t`. Because that instance is the first member,
the resolved array is directly consumable as a strided sprite-instance stream.

Priority does not silently modify reciprocal PVR depth. Applications may keep
author order or call `pvr_cell_resolved_sort()` for deterministic ascending
priority and slot order before list/material routing.

`pvr_cell_sprite_apply_transform()` binds the generic animation transform to
the whole sprite without creating another motion representation. Translation
and XY scale compose directly. A quaternion must represent planar Z rotation;
three-dimensional rotation fails with `ENOTSUP` instead of being flattened.

## Two geometry paths

Uniform-color cells should use `pvr_cell_sprite_compile_2d()` or
`pvr_cell_sprite_compile_3d()`. They retain the compact 64-byte hardware sprite
packet and established inferred fourth-corner behavior.

Cells that need independent A/B/C/D diffuse or offset colors use the colored
variants. Those emit one four-vertex textured strip per visible cell, costing
128 bytes of vertex data but preserving every corner color. The choice is
explicit and per batch, so basic sprites do not pay for the richer format.

Both paths compact hidden cells while preserving the order of visible input.
Material identifiers stay in the resolved array, allowing the application to
partition or submit batches through existing checked PVR material APIs.

## SH4ZAM integration

Dreamcast builds use SH4ZAM's paired sine/cosine operation for whole-sprite and
per-cell rotations. Three-dimensional hardware sprites and colored quads then
use the shared SH4ZAM-backed batch projection path. Host builds use scalar
`sinf()` and `cosf()` as an independent oracle.

The public state and stream contracts do not expose SH4ZAM types. This keeps
assets and application data portable while ensuring the target hot paths use
the optimized implementation. Tiny mask application, color packing, and
priority comparisons remain scalar because converting those operations into
vector math would add overhead without useful parallel work.

## Authoring and material-routing closure

The core stream/list, transform, priority, material metadata, color, and 2D/3D
geometry paths are complete. `pvr-cell-convert` provides host-side declarative
authoring for atlas regions, base cell tables, independent stream lists, and
every partial key field. It emits checked PCA1 state plus normalized atlas
geometry rather than introducing a target-side content parser.

`examples/dreamcast/pvr/cell_asset` exercises that production path end to end.
It materializes one authored asset, samples and priority-sorts one resolved
sprite, then filters its material identifiers into opaque, punch-through, and
translucent passes. The example also exercises per-corner colored quads; their
rectangle-order A/B/C/D attributes are reordered to the A/B/D/C topology
required by a complete triangle strip.

Texture conversion and general scene-editor import remain separate host-tool
concerns. They do not require another cell runtime or changes to the PCA1
contract.
