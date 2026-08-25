# Checked sprite cells

This example builds two reusable atlas cells and three caller-owned instances,
then compiles the visible instances directly into the existing textured PVR
sprite packet. One instance is animated through its ordinary rotation field;
another alternates between visible and hidden to demonstrate output
compaction.

The example deliberately keeps each ownership boundary visible:

- `pvr_sprite_batch_compile_2d()` owns no memory and changes no PVR state;
- `pvr_material_compile_sprite()` validates the established sprite context;
- `pvr_material_submit()` publishes the existing sprite header; and
- a `PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED` sink emits the compiled packets.

Texture state and the uniform sprite color remain properties of the material
header because the hardware sprite packet has no per-corner color fields.
Cells therefore describe geometry and UV regions, while applications retain
normal material, texture, scene, list, timing, and instance ownership.

The program renders 180 frames, checks the PVR fault record, prints a PASS
line, and leaves a green diagnostic screen.
