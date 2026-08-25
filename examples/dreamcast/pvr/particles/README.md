# Caller-owned particles

This example uses one fixed 24-record pool with no particle-system allocation
or background execution. It demonstrates the complete composition:

1. `pvr_particle_spawn()` publishes deterministic seeds into inactive slots.
2. `pvr_particle_step()` advances the pool by an explicit `1/60` second.
3. `pvr_particle_emit_sprite_instances()` extracts visible particles.
4. `pvr_sprite_batch_compile_2d()` produces existing textured sprite packets.
5. `pvr_particle_compile_trail()` builds colored polygon trail segments from
   the same ordered pool.
6. Existing checked sprite/polygon materials and geometry sinks submit both
   batches to the caller-owned scene and list.

The runtime owns no clock, random-number policy, texture, material, PVR scene,
list, callback, thread, fiber, or hidden particle allocation. Applications can
instead place particle records inside extended structures using the explicit
stride.

The program renders 180 frames, checks the PVR fault record, prints a PASS
line, and leaves a green diagnostic screen.
