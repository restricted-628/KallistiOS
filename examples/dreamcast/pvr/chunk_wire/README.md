# Prepared compact-model wireframe

This example builds a four-reference triangle-strip cache and draws its
wireframe through constant-width screen-space quads. It cycles among the full
mesh, outside boundary, and consecutive-reference path policies so their
topology is directly visible.

The model contains no wireframe command. Cache storage, resolved-strip scratch,
profile, material header, scene, and list remain application-owned. The example
performs no allocation in its frame loop.

The cache is admitted once with `pvr_chunk_model_cache_draw_prepare()` and
frames use `pvr_chunk_model_cache_draw_emit_wire()`. Cache storage stays
immutable for the loop; only the topology profile changes. This avoids full
cache rescans and copies of unchanged deformation records. Its explicit
ASSUME_VISIBLE policy now reuses recently projected endpoints across neighboring
edges, clearing reuse after the header callback. Projection arithmetic and
line expansion retain their checked behavior; SPLIT/DROP callers still clip
homogeneous segments before projection. Success is
reported on screen and through SCIF after all 360 frames and pipeline checks.
