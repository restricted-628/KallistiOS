# Compact-model texture resources

This example exercises the complete compact-model content and rendering path.
Its Makefile converts a triangulated OBJ plus an explicitly selected material
library into one generated C translation unit. Conversion resolves material
name `checker` to texture identifier 7, emits persistent color/specular state,
preserves the authored normal, joins the two compatible faces into one strip,
validates the model, and embeds both streams with calculated bounds behind
`chunk_resource_model`.

At runtime, the application opens that immutable generated model, builds its
one-page caller-owned direct vertex index, and binds texture identifier 7 to
one fixed VRAM residency slot without creating a global asset registry. It
reserves identifier 7, uploads and publishes its surface, then prepares a
caller-owned compact-model residency adapter before list emission.
It also queries and allocates an ordinary draw cache, decodes the immutable
model once, and admits that cache with `pvr_chunk_model_cache_draw_prepare()`.

For every strip, the established compact renderer:

1. reads retained packets, per-corner normals, and persistent material state;
2. resolves texture identifier 7 through the pre-acquired resident set;
3. compiles and submits an ordinary checked KOS polygon material;
4. applies the admitted diffuse-plus-specular policy over one positive and one
   negative-intensity directional light without changing XMTRX; and
5. projects and emits the strip through the current PVR list sink.

There is no model-stream decoding or static-cache rescan in the frame loop.
A one-time memory-sink comparison still exercises the indexed stream path,
requiring byte-identical lit packets from the admitted cache and an intact
output guard before printing `KOSRESOURCES cached_parity=PASS guards=PASS`.
The prepared callback retains changing lighting/output checks; material and
residency validation remain active. This is an integration cleanup, not a
measured hardware speedup.

The adapter holds one generation-checked pin across all 120 frames, so the slot
cannot be evicted while submitted materials may sample it. After final render
completion the application releases the pin, validates cache statistics, and
destroys the residency cache. It still owns upload, model data, the scene, the
list, lighting, and every lifetime; the bridge creates no worker or hidden
allocation. The application releases its draw-cache allocation after rendering
and PVR shutdown, before final PASS.
