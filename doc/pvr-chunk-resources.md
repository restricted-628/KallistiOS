# Compact-model texture and material binding

Compact polygon streams store small texture identifiers rather than VRAM
addresses. The resource-binding layer resolves those identifiers into existing
checked PVR texture surfaces while keeping the namespace, allocation, and
lifetime policy in application-owned memory.

## Texture tables

`pvr_chunk_texture_table_t` is a bounded array of identifier, palette, and
surface bindings. Identifiers must be strictly increasing, so an admitted
`pvr_chunk_texture_table_view_t` provides deterministic logarithmic lookup
without allocating an index.

Table admission checks:

- the complete array address range;
- the 13-bit identifier range and strict ordering;
- checked surface metadata and an allocated 64-bit VRAM binding;
- capacity against both the encoded surface size and remaining VRAM; and
- palette selection: zero for ordinary textures, 0-3 for 8-bit palettes, and
  0-63 for 4-bit palettes.

The view borrows both the entries and their surfaces. They must remain
immutable while a compact-model emission uses the view. Releasing or rebinding
a surface first invalidates the application contract; KOS does not retain the
surface or hide a reference-counted texture manager.

## Material resolution

`pvr_chunk_material_resolve()` copies an application-supplied polygon context,
applies the compact draw state, and publishes an ordinary checked
`pvr_material_t`. The base context continues to define policy that model data
does not own, including the target list, depth comparison and writes, clipping,
fog, texture environment, and color clamping.

The resolved compact state supplies:

- source and destination blending;
- primary and secondary texture identifiers;
- surface format, dimensions, address, and mipmap presence;
- filter, mip bias, UV flip and clamp, and supersampling;
- alpha use, flat or Gouraud shading, double-sided culling; and
- specular-header enablement.

Missing identifiers report `ENOENT`. Invalid model state, mutated surface
metadata, incompatible one/two-volume contexts, and invalid palette or mip
state leave the destination material unchanged.

## Renderer integration

`pvr_chunk_material_binding_t` copies the base context and admitted table view
into a small caller-owned callback adapter. Its begin-strip callback resolves,
compiles, and submits exactly one material header through either the current
PVR list or an explicit buffered list. It can be passed directly to
`pvr_chunk_model_emit()` and `pvr_chunk_model_emit_two_volume()`.

The adapter never begins or finishes a scene or list. It owns no texture,
model, namespace, material cache, allocator, thread, fiber, clock, or callback
dispatcher. Applications that need persistent material caching or a different
asset lookup policy can continue supplying their own begin-strip callback.
