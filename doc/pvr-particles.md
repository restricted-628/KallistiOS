# Caller-owned PVR particles

The particle layer is an allocation-free bridge between application-owned
state and existing PVR geometry. It is not a retained renderer and does not
create a thread, fiber, timer, callback, random-number generator, texture,
material, scene, or list.

## Pool ownership

`pvr_particle_stream_t` describes a bounded strided array. The prefix of each
record is `pvr_particle_t`; application data may follow it. Pool clearing and
all mutations touch only that prefix.

Inactive records need only have a valid `flags` field. Call
`pvr_particle_pool_clear()` before first use when the pool does not already
contain initialized records. Spawning copies its seed before inspecting the
pool, validates every slot flag, and publishes into the first inactive slot
only after complete validation. A seed may therefore alias a pool record.

No free-list allocation is hidden inside the API. Applications needing a
different replacement policy can write their own inactive slot and continue
using the same stepping and geometry functions.

## Deterministic stepping

The application supplies elapsed seconds explicitly. Active particles use
constant-acceleration integration:

```text
position += velocity * dt + 0.5 * acceleration * dt * dt
velocity += acceleration * dt
```

Scale and rotation advance linearly. A step crossing a lifetime boundary is
clamped to that exact boundary before the active flag is cleared. A scale
reaching zero also expires the record. Every active transition is computed in
a dry run before the first mutation, so invalid state or floating-point
overflow cannot expose a partially advanced pool.

This deterministic contract lets a game, fixed-step loop, replay system, or
fiber choose scheduling policy without the particle layer silently reading a
global clock.

## Geometry paths

Three output paths share the same state:

- `pvr_particle_emit_sprite_instances()` compacts active visible particles
  into `pvr_sprite_instance_t`. The result goes directly to the checked 2D or
  3D sprite-cell compiler. Cell selection, scale, rotation, and UV flips are
  retained. The sprite material owns uniform color because the hardware
  sprite vertex packet has no per-corner colors.
- `pvr_particle_compile_billboards()` expands each visible particle into two
  independent canonical triangles. It supplies full-range UVs and the
  particle's packed color, so either colored or textured ordinary polygon
  materials may be used.
- `pvr_particle_compile_trail()` connects adjacent active visible records with
  independent camera-facing quad segments. Inactive, hidden, coincident, or
  edge-on records break continuity. Endpoint widths use `scale_x`, and endpoint
  colors are preserved for Gouraud interpolation.

Billboard and trail output is projected through the established checked
geometry path and uses the same memory/current-list/buffered-list sinks as
other canonical vertices. Material and list submission remain explicit.

## Failure and resource behavior

All counts, strides, address ranges, flags, finite values, capacities, and
input/output overlap are checked. Geometry construction performs a dry run
before publication. If final projection ever rejects a vertex, the returned
`produced_vertices` identifies the complete projected prefix.

The implementation has no initialization hook and consumes no permanent RAM.
An application pays only for the records and output buffers it declares, and
link-time section garbage collection removes unused entry points.
