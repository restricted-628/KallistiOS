/* KallistiOS ##version##

   dc/pvr_particle.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_particle.h
    \brief   Bounded caller-owned particle simulation and geometry.
    \ingroup pvr_particle

    This interface supplies deterministic particle stepping and prepares
    existing PVR geometry. It owns no pool, allocator, worker, clock, random
    source, texture, material, scene, list, or animation state.
*/

#ifndef __DC_PVR_PARTICLE_H
#define __DC_PVR_PARTICLE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_sprite_geometry.h>

/** \defgroup pvr_particle Particle geometry
    \brief                     Caller-owned simulation and PVR emission
    \ingroup                   pvr_geometry
    @{ */

/** \brief Persistent particle-state flags. */
typedef enum pvr_particle_flags {
    PVR_PARTICLE_NONE = 0,
    PVR_PARTICLE_ACTIVE = 1u << 0,  /**< Slot participates in simulation. */
    PVR_PARTICLE_HIDDEN = 1u << 1,  /**< Simulate without emitting geometry. */
    PVR_PARTICLE_FLIP_U = 1u << 2,  /**< Flip sprite-cell U coordinates. */
    PVR_PARTICLE_FLIP_V = 1u << 3   /**< Flip sprite-cell V coordinates. */
} pvr_particle_flags_t;

/** \brief One application-owned particle state.

    Active records use constant acceleration and independent linear scale and
    angular velocities. Age lies in `[0, lifetime)`. Scale values are positive
    multipliers. `color` is used by polygon billboards and trails; hardware
    sprite-cell packets instead use the uniform color in their material.

    Position/vector W fields and inactive-record fields other than `flags` are
    ignored. `cell_index` is checked later against the selected sprite atlas.
*/
typedef struct pvr_particle {
    point_t position;
    vector_t velocity;
    vector_t acceleration;
    float age;
    float lifetime;
    float scale_x;
    float scale_y;
    float scale_velocity_x;
    float scale_velocity_y;
    float rotation;
    float angular_velocity;
    uint32_t color;
    uint32_t flags;
    size_t cell_index;
} pvr_particle_t;

/** \brief Mutable bounded strided view over particle records. */
typedef struct pvr_particle_stream {
    void *particles;
    size_t particle_count;
    size_t stride;
} pvr_particle_stream_t;

/** \brief Result from one deterministic simulation step. */
typedef struct pvr_particle_step_result {
    size_t examined_particles;
    size_t active_before;
    size_t active_after;
    size_t expired_particles;
} pvr_particle_step_result_t;

/** \brief Result from one particle geometry operation. */
typedef struct pvr_particle_emit_result {
    size_t examined_particles;
    size_t emitted_items;       /**< Particles, or adjacent trail segments. */
    size_t produced_vertices;
} pvr_particle_emit_result_t;

/** \brief Shared description for polygon particle billboards.

    Width and height are positive base units multiplied by each particle's
    scale. The billboard axes and projection matrix use the same conventions
    as pvr_sprite_batch_compile_3d(). Full-range UVs are generated, allowing
    either colored or textured ordinary polygon materials.
*/
typedef struct pvr_particle_billboard_desc {
    float width;
    float height;
    pvr_sprite_billboard_basis_t basis;
    const matrix_t *world_to_screen;
} pvr_particle_billboard_desc_t;

/** \brief Shared description for ordered particle trail segments.

    Consecutive active, visible particles form independent camera-facing quad
    segments. `width` is multiplied by each endpoint's scale_x. `facing` is a
    finite nonzero world-space camera direction. Degenerate or exactly
    edge-on links break the trail and produce no geometry.
*/
typedef struct pvr_particle_trail_desc {
    float width;
    vector_t facing;
    const matrix_t *world_to_screen;
} pvr_particle_trail_desc_t;

/** \brief Clear only the particle prefix of every strided pool slot.

    Application extension fields after pvr_particle_t are preserved. This is
    the initialization operation for a pool whose inactive slots may otherwise
    contain unspecified data.
*/
int pvr_particle_pool_clear(const pvr_particle_stream_t *stream);

/** \brief Publish a particle into the first inactive pool slot.

    The seed is copied before the pool is examined, may alias a pool record,
    and has its age reset to zero and active flag set. All slot flags are
    checked before publication. On failure, the pool is unchanged.

    \param stream       Destination particle pool.
    \param seed         Complete active-state template.
    \param particle_index Optional destination for the selected slot index.

    \retval 0  Particle published.
    \retval -1 Invalid state or no free slot, with errno set.
*/
int pvr_particle_spawn(const pvr_particle_stream_t *stream,
                       const pvr_particle_t *seed,
                       size_t *particle_index);

/** \brief Advance every active particle by an explicit duration.

    Integration uses `p += v*dt + 0.5*a*dt^2`, followed by `v += a*dt`.
    Lifetime is clamped exactly at expiry. A scale reaching zero also expires
    the record. Every result is precomputed before the first record changes,
    so validation or arithmetic failure leaves the complete pool unchanged.
*/
int pvr_particle_step(const pvr_particle_stream_t *stream,
                      float delta_seconds,
                      pvr_particle_step_result_t *result);

/** \brief Compact visible active particles into sprite-cell instances.

    Output may be passed directly to pvr_sprite_batch_compile_2d() or
    pvr_sprite_batch_compile_3d(). No atlas is needed until that later call.
    Input and output ranges must not overlap.
*/
int pvr_particle_emit_sprite_instances(
    pvr_sprite_instance_t *output, size_t output_capacity,
    const pvr_particle_stream_t *stream,
    pvr_particle_emit_result_t *result);

/** \brief Compile visible particles into projected polygon billboards.

    Each particle produces two independent triangles (six canonical vertices)
    carrying its packed color and full-range UVs. The output can use the
    established canonical geometry sinks after an ordinary polygon material.
    On a projection failure, `result->produced_vertices` identifies the valid
    projected prefix.
*/
int pvr_particle_compile_billboards(
    pvr_vertex_t *output, size_t output_capacity,
    const pvr_particle_stream_t *stream,
    const pvr_particle_billboard_desc_t *description,
    pvr_particle_emit_result_t *result);

/** \brief Compile an ordered particle stream into projected trail segments.

    Every accepted adjacent pair produces two independent triangles. Hidden,
    inactive, degenerate, or edge-on records break continuity. Endpoint colors
    and scale_x widths are preserved. On projection failure,
    `result->produced_vertices` identifies the valid projected prefix.
*/
int pvr_particle_compile_trail(
    pvr_vertex_t *output, size_t output_capacity,
    const pvr_particle_stream_t *stream,
    const pvr_particle_trail_desc_t *description,
    pvr_particle_emit_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_PARTICLE_H */
