# Compact-model texture resources

This example binds the texture identifier stored in a compact model to one
fixed VRAM residency slot without creating a global asset registry. The
application reserves identifier 7, uploads and publishes its surface, then
prepares a caller-owned compact-model residency adapter before list emission.

For every strip, the established compact renderer:

1. decodes persistent model state;
2. resolves texture identifier 7 through the pre-acquired resident set;
3. compiles and submits an ordinary checked KOS polygon material; and
4. projects and emits the strip through the current PVR list sink.

The adapter holds one generation-checked pin across all 120 frames, so the slot
cannot be evicted while submitted materials may sample it. After final render
completion the application releases the pin, validates cache statistics, and
destroys the residency cache. It still owns upload, model data, the scene, the
list, and every lifetime; the bridge creates no worker or hidden allocation.
