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

`pvr_chunk_environment_map_binding_t` composes with that boundary rather than
creating a second renderer. It forwards material setup, derives view-space
normals from a copied inverse-transpose matrix, generates UVs only for marked
environment strips, and then runs an optional application vertex callback.
Per-reference normals override indexed vertex normals so discontinuous hard
edges remain discontinuous. It allocates nothing and retains neither the
source matrix nor render storage.

## Fixed-slot residency integration

`pvr_chunk_residency_binding_t` connects the same material resolver to an
opt-in `pvr_txr_residency_t` without making the model renderer own texture
streaming. The caller provides parallel arrays for sorted texture bindings and
generation-checked residency handles.

Before opening the destination PVR list,
`pvr_chunk_residency_binding_prepare_model()` scans the admitted polygon stream
and pins every distinct texture identifier it references. Several models can
accumulate into one binding for a render. Missing, still-loading, or excess
textures therefore fail before geometry emission rather than from a later
strip callback after part of a list is already visible.

The matching begin-strip adapter resolves only from that pre-acquired set and
submits through the existing current-list or buffered-list material path. It
does not perform cache acquisition, eviction, upload, or waiting. After the PVR
render which can sample those materials is complete,
`pvr_chunk_residency_binding_release()` releases every held pin.

All slots in one residency cache have the same surface layout. An optional
preparation-time callback supplies the global palette-bank selector for each
identifier when that layout is 4-bit or 8-bit paletted. Embedded VQ codebooks
do not consume the global selector. The bridge allocates no memory and creates
no thread, transfer request, model copy, or scene owner.
