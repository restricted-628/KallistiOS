/* KallistiOS ##version##

   pvr_particle.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_particle.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PVR_PARTICLE_FLAGS_ALL \
    (PVR_PARTICLE_ACTIVE | PVR_PARTICLE_HIDDEN | PVR_PARTICLE_FLIP_U | \
     PVR_PARTICLE_FLIP_V)

static int finite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static int stream_range(const pvr_particle_stream_t *stream,
                        size_t *stream_bytes) {
    size_t bytes = 0;

    if(!stream || (stream->particle_count && !stream->particles) ||
       (stream->particles &&
        ((uintptr_t)stream->particles & (_Alignof(pvr_particle_t) - 1u))) ||
       stream->stride < sizeof(pvr_particle_t) || (stream->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(stream->particle_count &&
       stream->particle_count - 1u >
       (SIZE_MAX - sizeof(pvr_particle_t)) / stream->stride) {
        errno = ERANGE;
        return -1;
    }
    if(stream->particle_count)
        bytes = (stream->particle_count - 1u) * stream->stride +
                sizeof(pvr_particle_t);
    if(bytes > UINTPTR_MAX - (uintptr_t)stream->particles) {
        errno = ERANGE;
        return -1;
    }
    *stream_bytes = bytes;
    return 0;
}

static pvr_particle_t *particle_at(const pvr_particle_stream_t *stream,
                                   size_t index) {
    return (pvr_particle_t *)((uint8_t *)stream->particles +
                              index * stream->stride);
}

static int flags_valid(uint32_t flags) {
    return !(flags & ~PVR_PARTICLE_FLAGS_ALL);
}

static int active_particle_valid(const pvr_particle_t *particle) {
    return particle && flags_valid(particle->flags) &&
           (particle->flags & PVR_PARTICLE_ACTIVE) &&
           finite3(particle->position.x, particle->position.y,
                   particle->position.z) &&
           finite3(particle->velocity.x, particle->velocity.y,
                   particle->velocity.z) &&
           finite3(particle->acceleration.x, particle->acceleration.y,
                   particle->acceleration.z) &&
           isfinite(particle->age) && particle->age >= 0.0f &&
           isfinite(particle->lifetime) && particle->lifetime > 0.0f &&
           particle->age < particle->lifetime &&
           isfinite(particle->scale_x) && particle->scale_x > 0.0f &&
           isfinite(particle->scale_y) && particle->scale_y > 0.0f &&
           isfinite(particle->scale_velocity_x) &&
           isfinite(particle->scale_velocity_y) &&
           isfinite(particle->rotation) &&
           isfinite(particle->angular_velocity);
}

static int stream_particles_valid(const pvr_particle_stream_t *stream,
                                  size_t *visible_count) {
    size_t visible = 0;
    size_t i;

    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);

        if(!flags_valid(particle->flags) ||
           ((particle->flags & PVR_PARTICLE_ACTIVE) &&
            !active_particle_valid(particle))) {
            errno = EINVAL;
            return -1;
        }
        if((particle->flags & PVR_PARTICLE_ACTIVE) &&
           !(particle->flags & PVR_PARTICLE_HIDDEN))
            ++visible;
    }
    if(visible_count)
        *visible_count = visible;
    return 0;
}

static int matrix_valid(const matrix_t *matrix) {
    size_t column;
    size_t row;

    if(!matrix || ((uintptr_t)matrix & (_Alignof(matrix_t) - 1u)))
        return 0;
    for(column = 0; column < 4u; ++column) {
        for(row = 0; row < 4u; ++row) {
            if(!isfinite((*matrix)[column][row]))
                return 0;
        }
    }
    return 1;
}

static int basis_valid(const pvr_sprite_billboard_basis_t *basis) {
    float x_length;
    float y_length;
    float cross_x;
    float cross_y;
    float cross_z;
    float cross_length;

    if(!basis || !finite3(basis->x_axis.x, basis->x_axis.y,
                          basis->x_axis.z) ||
       !finite3(basis->y_axis.x, basis->y_axis.y, basis->y_axis.z))
        return 0;
    x_length = basis->x_axis.x * basis->x_axis.x +
               basis->x_axis.y * basis->x_axis.y +
               basis->x_axis.z * basis->x_axis.z;
    y_length = basis->y_axis.x * basis->y_axis.x +
               basis->y_axis.y * basis->y_axis.y +
               basis->y_axis.z * basis->y_axis.z;
    cross_x = basis->x_axis.y * basis->y_axis.z -
              basis->x_axis.z * basis->y_axis.y;
    cross_y = basis->x_axis.z * basis->y_axis.x -
              basis->x_axis.x * basis->y_axis.z;
    cross_z = basis->x_axis.x * basis->y_axis.y -
              basis->x_axis.y * basis->y_axis.x;
    cross_length = cross_x * cross_x + cross_y * cross_y +
                   cross_z * cross_z;
    return isfinite(x_length) && x_length > FLT_MIN &&
           isfinite(y_length) && y_length > FLT_MIN &&
           isfinite(cross_length) && cross_length > FLT_MIN;
}

static int position_projectable(const matrix_t *matrix,
                                float x, float y, float z) {
    float tx = (*matrix)[0][0] * x + (*matrix)[1][0] * y +
               (*matrix)[2][0] * z + (*matrix)[3][0];
    float ty = (*matrix)[0][1] * x + (*matrix)[1][1] * y +
               (*matrix)[2][1] * z + (*matrix)[3][1];
    float tw = (*matrix)[0][3] * x + (*matrix)[1][3] * y +
               (*matrix)[2][3] * z + (*matrix)[3][3];
    float reciprocal_w;

    if(!isfinite(tx) || !isfinite(ty) || !isfinite(tw) || tw <= FLT_MIN)
        return 0;
    reciprocal_w = 1.0f / tw;
    return isfinite(tx * reciprocal_w) && isfinite(ty * reciprocal_w) &&
           isfinite(reciprocal_w);
}

static int output_range_valid(const void *output, size_t output_capacity,
                              size_t element_size, size_t required,
                              size_t alignment,
                              uintptr_t input, size_t input_bytes) {
    size_t output_bytes;

    if((required || output_capacity) &&
       (!output || ((uintptr_t)output & (alignment - 1u)))) {
        errno = EINVAL;
        return -1;
    }
    if(output_capacity > SIZE_MAX / element_size) {
        errno = ERANGE;
        return -1;
    }
    if(output_capacity < required) {
        errno = ENOSPC;
        return -1;
    }
    output_bytes = output_capacity * element_size;
    if(output_bytes > UINTPTR_MAX - (uintptr_t)output) {
        errno = ERANGE;
        return -1;
    }
    if(required && input_bytes &&
       ranges_overlap((uintptr_t)output, output_bytes, input, input_bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int step_particle(const pvr_particle_t *source, float delta_seconds,
                         pvr_particle_t *destination, int *expired) {
    float remaining = source->lifetime - source->age;
    float duration = delta_seconds < remaining ? delta_seconds : remaining;
    float half_duration_squared = 0.5f * duration * duration;
    pvr_particle_t next = *source;

    next.position.x += source->velocity.x * duration +
                       source->acceleration.x * half_duration_squared;
    next.position.y += source->velocity.y * duration +
                       source->acceleration.y * half_duration_squared;
    next.position.z += source->velocity.z * duration +
                       source->acceleration.z * half_duration_squared;
    next.velocity.x += source->acceleration.x * duration;
    next.velocity.y += source->acceleration.y * duration;
    next.velocity.z += source->acceleration.z * duration;
    next.scale_x += source->scale_velocity_x * duration;
    next.scale_y += source->scale_velocity_y * duration;
    next.rotation += source->angular_velocity * duration;
    next.age += duration;

    if(!finite3(next.position.x, next.position.y, next.position.z) ||
       !finite3(next.velocity.x, next.velocity.y, next.velocity.z) ||
       !isfinite(next.scale_x) || !isfinite(next.scale_y) ||
       !isfinite(next.rotation) || !isfinite(next.age)) {
        errno = ERANGE;
        return -1;
    }

    *expired = delta_seconds >= remaining || next.scale_x <= 0.0f ||
               next.scale_y <= 0.0f;
    if(*expired) {
        if(delta_seconds >= remaining)
            next.age = next.lifetime;
        next.flags &= ~PVR_PARTICLE_ACTIVE;
    }
    *destination = next;
    return 0;
}

int pvr_particle_pool_clear(const pvr_particle_stream_t *stream) {
    size_t stream_bytes;
    size_t i;

    if(stream_range(stream, &stream_bytes) < 0)
        return -1;
    (void)stream_bytes;
    for(i = 0; i < stream->particle_count; ++i)
        memset(particle_at(stream, i), 0, sizeof(pvr_particle_t));
    return 0;
}

int pvr_particle_spawn(const pvr_particle_stream_t *stream,
                       const pvr_particle_t *seed,
                       size_t *particle_index) {
    pvr_particle_t candidate;
    size_t stream_bytes;
    size_t available = SIZE_MAX;
    size_t i;

    if(particle_index)
        *particle_index = SIZE_MAX;
    if(!seed) {
        errno = EINVAL;
        return -1;
    }
    candidate = *seed;
    candidate.age = 0.0f;
    candidate.flags |= PVR_PARTICLE_ACTIVE;
    if(stream_range(stream, &stream_bytes) < 0)
        return -1;
    (void)stream_bytes;
    if(!active_particle_valid(&candidate)) {
        errno = EINVAL;
        return -1;
    }

    /* Find a slot only after checking every flag, keeping publication atomic
       even when a later extended record is malformed. */
    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);

        if(!flags_valid(particle->flags)) {
            errno = EINVAL;
            return -1;
        }
        if(available == SIZE_MAX &&
           !(particle->flags & PVR_PARTICLE_ACTIVE))
            available = i;
    }
    if(available == SIZE_MAX) {
        errno = ENOSPC;
        return -1;
    }
    *particle_at(stream, available) = candidate;
    if(particle_index)
        *particle_index = available;
    return 0;
}

int pvr_particle_step(const pvr_particle_stream_t *stream,
                      float delta_seconds,
                      pvr_particle_step_result_t *result) {
    pvr_particle_step_result_t progress = { 0, 0, 0, 0 };
    size_t stream_bytes;
    size_t i;

    if(result)
        *result = progress;
    if(!isfinite(delta_seconds) || delta_seconds < 0.0f) {
        errno = EINVAL;
        return -1;
    }
    if(stream_range(stream, &stream_bytes) < 0)
        return -1;
    (void)stream_bytes;

    /* Precompute every active transition before changing the first slot. */
    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);
        pvr_particle_t next;
        int expired;

        if(!flags_valid(particle->flags) ||
           ((particle->flags & PVR_PARTICLE_ACTIVE) &&
            !active_particle_valid(particle))) {
            errno = EINVAL;
            return -1;
        }
        if(!(particle->flags & PVR_PARTICLE_ACTIVE))
            continue;
        ++progress.active_before;
        if(step_particle(particle, delta_seconds, &next, &expired) < 0)
            return -1;
        if(expired)
            ++progress.expired_particles;
        else
            ++progress.active_after;
    }

    for(i = 0; i < stream->particle_count; ++i) {
        pvr_particle_t *particle = particle_at(stream, i);

        if(particle->flags & PVR_PARTICLE_ACTIVE) {
            pvr_particle_t next;
            int expired;

            if(step_particle(particle, delta_seconds, &next, &expired) < 0)
                __builtin_unreachable();
            *particle = next;
        }
    }
    progress.examined_particles = stream->particle_count;
    if(result)
        *result = progress;
    return 0;
}

int pvr_particle_emit_sprite_instances(
        pvr_sprite_instance_t *output, size_t output_capacity,
        const pvr_particle_stream_t *stream,
        pvr_particle_emit_result_t *result) {
    pvr_particle_emit_result_t progress = { 0, 0, 0 };
    size_t stream_bytes;
    size_t visible;
    size_t produced = 0;
    size_t i;

    if(result)
        *result = progress;
    if(stream_range(stream, &stream_bytes) < 0 ||
       stream_particles_valid(stream, &visible) < 0)
        return -1;
    if(output_range_valid(output, output_capacity, sizeof(*output), visible,
                          _Alignof(pvr_sprite_instance_t),
                          (uintptr_t)stream->particles, stream_bytes) < 0)
        return -1;

    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);
        pvr_sprite_instance_t *instance;

        if(!(particle->flags & PVR_PARTICLE_ACTIVE) ||
           (particle->flags & PVR_PARTICLE_HIDDEN))
            continue;
        instance = &output[produced++];
        instance->cell_index = particle->cell_index;
        instance->position = particle->position;
        instance->rotation = particle->rotation;
        instance->scale_x = particle->scale_x;
        instance->scale_y = particle->scale_y;
        instance->flags = PVR_SPRITE_INSTANCE_NONE;
        if(particle->flags & PVR_PARTICLE_FLIP_U)
            instance->flags |= PVR_SPRITE_INSTANCE_FLIP_U;
        if(particle->flags & PVR_PARTICLE_FLIP_V)
            instance->flags |= PVR_SPRITE_INSTANCE_FLIP_V;
    }
    progress.examined_particles = stream->particle_count;
    progress.emitted_items = visible;
    if(result)
        *result = progress;
    return 0;
}

static void particle_sincos(float angle, float *sine, float *cosine) {
#ifdef __DREAMCAST__
    shz_sincos_t value = shz_sincosf(angle);

    *sine = value.sin;
    *cosine = value.cos;
#else
    *sine = sinf(angle);
    *cosine = cosf(angle);
#endif
}

static void set_vertex(pvr_vertex_t *vertex, const point_t *position,
                       float u, float v, uint32_t color, uint32_t command) {
    memset(vertex, 0, sizeof(*vertex));
    vertex->flags = command;
    vertex->x = position->x;
    vertex->y = position->y;
    vertex->z = position->z;
    vertex->u = u;
    vertex->v = v;
    vertex->argb = color;
}

static int build_billboard(const pvr_particle_t *particle,
                           const pvr_particle_billboard_desc_t *description,
                           pvr_vertex_t output[6]) {
    float sine;
    float cosine;
    float half_width = description->width * particle->scale_x * 0.5f;
    float half_height = description->height * particle->scale_y * 0.5f;
    const float local_x[4] = {
        -half_width, -half_width, half_width, half_width
    };
    const float local_y[4] = {
        half_height, -half_height, -half_height, half_height
    };
    point_t corner[4];
    size_t i;

    particle_sincos(particle->rotation, &sine, &cosine);
    if(!isfinite(sine) || !isfinite(cosine) || !isfinite(half_width) ||
       !isfinite(half_height)) {
        errno = ERANGE;
        return -1;
    }
    for(i = 0; i < 4u; ++i) {
        float x = local_x[i] * cosine - local_y[i] * sine;
        float y = local_x[i] * sine + local_y[i] * cosine;

        corner[i].x = particle->position.x +
                      description->basis.x_axis.x * x +
                      description->basis.y_axis.x * y;
        corner[i].y = particle->position.y +
                      description->basis.x_axis.y * x +
                      description->basis.y_axis.y * y;
        corner[i].z = particle->position.z +
                      description->basis.x_axis.z * x +
                      description->basis.y_axis.z * y;
        corner[i].w = 1.0f;
        if(!finite3(corner[i].x, corner[i].y, corner[i].z) ||
           !position_projectable(description->world_to_screen,
                                 corner[i].x, corner[i].y, corner[i].z)) {
            errno = ERANGE;
            return -1;
        }
    }

    set_vertex(&output[0], &corner[0], 0.0f, 1.0f, particle->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[1], &corner[1], 0.0f, 0.0f, particle->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[2], &corner[2], 1.0f, 0.0f, particle->color,
               PVR_CMD_VERTEX_EOL);
    set_vertex(&output[3], &corner[0], 0.0f, 1.0f, particle->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[4], &corner[2], 1.0f, 0.0f, particle->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[5], &corner[3], 1.0f, 1.0f, particle->color,
               PVR_CMD_VERTEX_EOL);
    return 0;
}

int pvr_particle_compile_billboards(
        pvr_vertex_t *output, size_t output_capacity,
        const pvr_particle_stream_t *stream,
        const pvr_particle_billboard_desc_t *description,
        pvr_particle_emit_result_t *result) {
    pvr_particle_emit_result_t progress = { 0, 0, 0 };
    pvr_geometry_stream_t geometry;
    pvr_geometry_result_t projected;
    size_t stream_bytes;
    size_t visible;
    size_t required;
    size_t produced = 0;
    size_t i;

    if(result)
        *result = progress;
    if(!description || !isfinite(description->width) ||
       description->width <= 0.0f || !isfinite(description->height) ||
       description->height <= 0.0f || !basis_valid(&description->basis) ||
       !matrix_valid(description->world_to_screen)) {
        errno = EINVAL;
        return -1;
    }
    if(stream_range(stream, &stream_bytes) < 0 ||
       stream_particles_valid(stream, &visible) < 0)
        return -1;
    if(visible > SIZE_MAX / 6u) {
        errno = ERANGE;
        return -1;
    }
    required = visible * 6u;
    if(output_range_valid(output, output_capacity, sizeof(*output), required,
                          32u, (uintptr_t)stream->particles,
                          stream_bytes) < 0)
        return -1;

    /* Dry-run every rectangle before publishing world-space geometry. */
    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);
        pvr_vertex_t vertices[6];

        if((particle->flags & PVR_PARTICLE_ACTIVE) &&
           !(particle->flags & PVR_PARTICLE_HIDDEN) &&
           build_billboard(particle, description, vertices) < 0)
            return -1;
    }
    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);

        if(!(particle->flags & PVR_PARTICLE_ACTIVE) ||
           (particle->flags & PVR_PARTICLE_HIDDEN))
            continue;
        if(build_billboard(particle, description, output + produced) < 0)
            __builtin_unreachable();
        produced += 6u;
    }

    progress.examined_particles = stream->particle_count;
    progress.emitted_items = visible;
    if(!required) {
        if(result)
            *result = progress;
        return 0;
    }
    geometry.vertices = output;
    geometry.vertex_count = required;
    geometry.stride = sizeof(*output);
    if(pvr_geometry_project(output, output_capacity, &geometry,
                            description->world_to_screen, &projected) < 0) {
        progress.produced_vertices = projected.produced_vertices;
        if(result)
            *result = progress;
        return -1;
    }
    progress.produced_vertices = required;
    if(result)
        *result = progress;
    return 0;
}

static int trail_segment(const pvr_particle_t *first,
                         const pvr_particle_t *second,
                         const pvr_particle_trail_desc_t *description,
                         pvr_vertex_t output[6]) {
    float dx = second->position.x - first->position.x;
    float dy = second->position.y - first->position.y;
    float dz = second->position.z - first->position.z;
    float length_squared = dx * dx + dy * dy + dz * dz;
    float side_x = description->facing.y * dz -
                   description->facing.z * dy;
    float side_y = description->facing.z * dx -
                   description->facing.x * dz;
    float side_z = description->facing.x * dy -
                   description->facing.y * dx;
    float side_length_squared = side_x * side_x + side_y * side_y +
                                side_z * side_z;
#ifndef __DREAMCAST__
    float inverse_side_length;
#endif
    float first_half_width;
    float second_half_width;
    point_t corner[4];
    size_t i;

    if(!isfinite(length_squared) || !isfinite(side_length_squared)) {
        errno = ERANGE;
        return -1;
    }
    if(length_squared <= FLT_MIN || side_length_squared <= FLT_MIN)
        return 0;
#ifdef __DREAMCAST__
    {
        shz_vec3_t side = shz_vec3_normalize(
            shz_vec3_init(side_x, side_y, side_z));

        side_x = side.x;
        side_y = side.y;
        side_z = side.z;
    }
#else
    inverse_side_length = 1.0f / sqrtf(side_length_squared);
    side_x *= inverse_side_length;
    side_y *= inverse_side_length;
    side_z *= inverse_side_length;
#endif
    first_half_width = description->width * first->scale_x * 0.5f;
    second_half_width = description->width * second->scale_x * 0.5f;
    if(!finite3(side_x, side_y, side_z) ||
       !isfinite(first_half_width) || !isfinite(second_half_width)) {
        errno = ERANGE;
        return -1;
    }

    corner[0] = (point_t){
        first->position.x - side_x * first_half_width,
        first->position.y - side_y * first_half_width,
        first->position.z - side_z * first_half_width, 1.0f
    };
    corner[1] = (point_t){
        second->position.x - side_x * second_half_width,
        second->position.y - side_y * second_half_width,
        second->position.z - side_z * second_half_width, 1.0f
    };
    corner[2] = (point_t){
        second->position.x + side_x * second_half_width,
        second->position.y + side_y * second_half_width,
        second->position.z + side_z * second_half_width, 1.0f
    };
    corner[3] = (point_t){
        first->position.x + side_x * first_half_width,
        first->position.y + side_y * first_half_width,
        first->position.z + side_z * first_half_width, 1.0f
    };
    for(i = 0; i < 4u; ++i) {
        if(!finite3(corner[i].x, corner[i].y, corner[i].z) ||
           !position_projectable(description->world_to_screen,
                                 corner[i].x, corner[i].y, corner[i].z)) {
            errno = ERANGE;
            return -1;
        }
    }

    set_vertex(&output[0], &corner[0], 0.0f, 0.0f, first->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[1], &corner[1], 0.0f, 1.0f, second->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[2], &corner[2], 1.0f, 1.0f, second->color,
               PVR_CMD_VERTEX_EOL);
    set_vertex(&output[3], &corner[0], 0.0f, 0.0f, first->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[4], &corner[2], 1.0f, 1.0f, second->color,
               PVR_CMD_VERTEX);
    set_vertex(&output[5], &corner[3], 1.0f, 0.0f, first->color,
               PVR_CMD_VERTEX_EOL);
    return 1;
}

int pvr_particle_compile_trail(
        pvr_vertex_t *output, size_t output_capacity,
        const pvr_particle_stream_t *stream,
        const pvr_particle_trail_desc_t *description,
        pvr_particle_emit_result_t *result) {
    pvr_particle_emit_result_t progress = { 0, 0, 0 };
    pvr_geometry_stream_t geometry;
    pvr_geometry_result_t projected;
    const pvr_particle_t *previous = NULL;
    size_t stream_bytes;
    size_t segments = 0;
    size_t required;
    size_t produced = 0;
    float facing_length;
    size_t i;

    if(result)
        *result = progress;
    if(!description || !isfinite(description->width) ||
       description->width <= 0.0f ||
       !finite3(description->facing.x, description->facing.y,
                description->facing.z) ||
       !matrix_valid(description->world_to_screen)) {
        errno = EINVAL;
        return -1;
    }
    facing_length = description->facing.x * description->facing.x +
                    description->facing.y * description->facing.y +
                    description->facing.z * description->facing.z;
    if(!isfinite(facing_length) || facing_length <= FLT_MIN) {
        errno = EINVAL;
        return -1;
    }
    if(stream_range(stream, &stream_bytes) < 0 ||
       stream_particles_valid(stream, NULL) < 0)
        return -1;

    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);

        if(!(particle->flags & PVR_PARTICLE_ACTIVE) ||
           (particle->flags & PVR_PARTICLE_HIDDEN)) {
            previous = NULL;
            continue;
        }
        if(previous) {
            pvr_vertex_t vertices[6];
            int accepted = trail_segment(previous, particle, description,
                                         vertices);

            if(accepted < 0)
                return -1;
            if(accepted)
                ++segments;
        }
        previous = particle;
    }
    if(segments > SIZE_MAX / 6u) {
        errno = ERANGE;
        return -1;
    }
    required = segments * 6u;
    if(output_range_valid(output, output_capacity, sizeof(*output), required,
                          32u, (uintptr_t)stream->particles,
                          stream_bytes) < 0)
        return -1;

    previous = NULL;
    for(i = 0; i < stream->particle_count; ++i) {
        const pvr_particle_t *particle = particle_at(stream, i);

        if(!(particle->flags & PVR_PARTICLE_ACTIVE) ||
           (particle->flags & PVR_PARTICLE_HIDDEN)) {
            previous = NULL;
            continue;
        }
        if(previous) {
            int accepted = trail_segment(previous, particle, description,
                                         output + produced);

            if(accepted < 0)
                __builtin_unreachable();
            if(accepted)
                produced += 6u;
        }
        previous = particle;
    }

    progress.examined_particles = stream->particle_count;
    progress.emitted_items = segments;
    if(!required) {
        if(result)
            *result = progress;
        return 0;
    }
    geometry.vertices = output;
    geometry.vertex_count = required;
    geometry.stride = sizeof(*output);
    if(pvr_geometry_project(output, output_capacity, &geometry,
                            description->world_to_screen, &projected) < 0) {
        progress.produced_vertices = projected.produced_vertices;
        if(result)
            *result = progress;
        return -1;
    }
    progress.produced_vertices = required;
    if(result)
        *result = progress;
    return 0;
}
