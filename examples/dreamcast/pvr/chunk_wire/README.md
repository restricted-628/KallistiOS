# Prepared compact-model wireframe

This example builds a four-reference triangle-strip cache and draws its
wireframe through constant-width screen-space quads. It cycles among the full
mesh, outside boundary, and consecutive-reference path policies. Within each
topology it runs SPLIT, DROP and ASSUME_VISIBLE for 40 frames apiece. SPLIT/DROP
cut the right half at X=320, exercising crossing and rejected edges while
retaining a visible left edge in every topology.

The model contains no wireframe command. Cache storage, resolved-strip scratch,
profile, material header, scene, and list remain application-owned. The example
performs no allocation in its frame loop.

The cache is admitted once with `pvr_chunk_model_cache_draw_prepare()` and
frames use `pvr_chunk_model_cache_draw_emit_wire()`. Cache storage stays
immutable for the loop; only the topology profile changes. This avoids full
cache rescans and copies of unchanged deformation records. ASSUME_VISIBLE
reuses projected positions; SPLIT/DROP reuse original homogeneous positions,
then clip and project each segment separately. Reuse is cleared after the
header callback. Transform arithmetic, attribute interpolation and line
expansion retain their checked behavior. Success is
reported on screen and through SCIF after all 360 frames and pipeline checks.
