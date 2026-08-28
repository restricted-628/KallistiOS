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

For every strip, the established compact renderer:

1. resolves each indexed vertex in constant time through the prepared page;
2. decodes the generated persistent model state;
3. resolves texture identifier 7 through the pre-acquired resident set;
4. compiles and submits an ordinary checked KOS polygon material;
5. applies the admitted diffuse-plus-specular policy over one positive and one
   negative-intensity directional light without changing XMTRX; and
6. projects and emits the strip through the current PVR list sink.

The adapter holds one generation-checked pin across all 120 frames, so the slot
cannot be evicted while submitted materials may sample it. After final render
completion the application releases the pin, validates cache statistics, and
destroys the residency cache. It still owns upload, model data, the scene, the
list, lighting, and every lifetime; the bridge creates no worker or hidden
allocation.
