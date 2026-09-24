# Checked cell-sprite streams

This example builds two reusable atlas cells and a three-slot cell sprite. Two
independently offset looping streams animate visibility, flipping, and local
rotation while the whole sprite rotates continuously. Caller-owned state and
workspace arrays are sampled, resolved, priority-sorted, and compiled directly
into the existing compact textured PVR sprite packet.

The example deliberately keeps each ownership boundary visible:

- `pvr_cell_stream_list_sample()` combines independent timestamped streams;
- `pvr_cell_asset_open()` and `pvr_cell_asset_materialize()` can supply the
  same base cells and stream list from a checked pointer-free asset;
- `pvr_cell_sprite_resolve()` composes local cells under one transform;
- `pvr_cell_resolved_sort()` applies explicit signed-priority order;
- `pvr_cell_sprite_compile_2d()` owns no memory and changes no PVR state;
- `pvr_material_compile_sprite()` validates the established sprite context;
- `pvr_material_submit()` publishes the existing sprite header; and
- a `PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED` sink emits the compiled packets.

Texture state and uniform sprite color remain properties of the material
header. Applications that need independent corner colors can select the
colored-quad compiler instead. The target rotation path uses SH4ZAM paired
sine/cosine without exposing SH4ZAM types in the persistent cell data.

The application retains texture, material, scene, list, clock, sampled state,
workspace, and submission ownership. No cell function allocates memory or
starts a worker.

Production data can be authored with `utils/pvr-cell-convert`, which checks
pixel regions against the real atlas image and emits the same runtime state as
this deliberately explicit C example.

The program renders 180 frames, checks the PVR fault record, prints a PASS
line, and leaves a green diagnostic screen.
