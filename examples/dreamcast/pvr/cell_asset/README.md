# Authored cell-sprite asset

This example runs the complete production-facing cell-sprite path. A strict
text manifest and a deterministic atlas image are compiled at build time into
a pointer-free PCA1 animation asset and normalized atlas geometry. The target
opens and materializes that asset into caller-owned arrays; no runtime parser,
allocator, clock, or service thread is introduced.

Three independent step streams animate atlas selection, local rotation,
flipping, scale, and per-corner diffuse color. The resolved six-cell sprite is
priority-sorted and filtered by its authored material identifiers into opaque,
punch-through, and translucent PVR lists. A single ARGB4444 texture supplies
fully opaque, one-bit masked, and half-alpha atlas regions.

The colored-quad compiler preserves every A/B/C/D color while reusing the
existing checked material and geometry submission layers. Derived pass arrays
are bounded by the six authored cells; the PCA1 image, atlas table, base state,
keys, and stream views are each stored only once.

The program renders 240 frames, reconciles every produced sprite with the
resolver's visible-cell count, checks the persistent PVR fault record, and
reports success.
