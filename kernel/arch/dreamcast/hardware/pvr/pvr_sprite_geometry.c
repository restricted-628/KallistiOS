/* KallistiOS ##version##

   pvr_sprite_geometry.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_sprite_geometry.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PVR_SPRITE_INSTANCE_FLAGS_ALL \
    (PVR_SPRITE_INSTANCE_FLIP_U | PVR_SPRITE_INSTANCE_FLIP_V | \
     PVR_SPRITE_INSTANCE_HIDDEN)

static int finite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static int cell_valid(const pvr_sprite_cell_t *cell) {
    return cell && isfinite(cell->width) && cell->width > 0.0f &&
           isfinite(cell->height) && cell->height > 0.0f &&
           isfinite(cell->origin_x) && isfinite(cell->origin_y) &&
           isfinite(cell->u0) && isfinite(cell->v0) &&
           isfinite(cell->u1) && isfinite(cell->v1) &&
           cell->u0 >= 0.0f && cell->v0 >= 0.0f &&
           cell->u1 <= 1.0f && cell->v1 <= 1.0f &&
           cell->u0 < cell->u1 && cell->v0 < cell->v1;
}

static int instance_valid(const pvr_sprite_instance_t *instance,
                          size_t cell_count, int screen_space) {
    return instance && instance->cell_index < cell_count &&
           finite3(instance->position.x, instance->position.y,
                   instance->position.z) &&
           (!screen_space || instance->position.z > FLT_MIN) &&
           isfinite(instance->rotation) && isfinite(instance->scale_x) &&
           isfinite(instance->scale_y) && instance->scale_x > 0.0f &&
           instance->scale_y > 0.0f &&
           !(instance->flags & ~PVR_SPRITE_INSTANCE_FLAGS_ALL);
}

static const pvr_sprite_instance_t *instance_at(
        const pvr_sprite_instance_stream_t *stream, size_t index) {
    return (const pvr_sprite_instance_t *)((const uint8_t *)stream->instances +
                                           index * stream->stride);
}

static int matrix_valid(const matrix_t *matrix) {
    size_t column;
    size_t row;

    if(!matrix || ((uintptr_t)matrix & (_Alignof(matrix_t) - 1u)))
        return 0;
    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
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
    /* Independent axes make the fourth hardware corner and its inferred
       depth unambiguous. Orthonormality is intentionally not required: a
       caller may use the shared basis for an explicit skew or scale. */
    return isfinite(x_length) && x_length > FLT_MIN &&
           isfinite(y_length) && y_length > FLT_MIN &&
           isfinite(cross_length) && cross_length > FLT_MIN;
}

static int source_preflight(pvr_sprite_txr_t *output, size_t output_capacity,
                            const pvr_sprite_atlas_t *atlas,
                            const pvr_sprite_instance_stream_t *stream,
                            int screen_space, size_t *visible_count) {
    size_t cell_bytes;
    size_t input_bytes;
    size_t output_bytes;
    size_t visible = 0;
    size_t i;

    if(!output || ((uintptr_t)output & 31u) || !atlas || !atlas->cells ||
       !atlas->cell_count ||
       ((uintptr_t)atlas->cells & (_Alignof(pvr_sprite_cell_t) - 1u)) ||
       !stream || (stream->instance_count && !stream->instances) ||
       (stream->instances &&
        ((uintptr_t)stream->instances &
         (_Alignof(pvr_sprite_instance_t) - 1u))) ||
       stream->stride < sizeof(pvr_sprite_instance_t) ||
       (stream->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(atlas->cell_count > SIZE_MAX / sizeof(*atlas->cells) ||
       output_capacity > SIZE_MAX / sizeof(*output) ||
       (stream->instance_count &&
        stream->instance_count - 1u >
        (SIZE_MAX - sizeof(pvr_sprite_instance_t)) / stream->stride)) {
        errno = ERANGE;
        return -1;
    }
    cell_bytes = atlas->cell_count * sizeof(*atlas->cells);
    input_bytes = stream->instance_count ?
        (stream->instance_count - 1u) * stream->stride +
        sizeof(pvr_sprite_instance_t) : 0;
    output_bytes = output_capacity * sizeof(*output);
    if(cell_bytes > UINTPTR_MAX - (uintptr_t)atlas->cells ||
       input_bytes > UINTPTR_MAX - (uintptr_t)stream->instances ||
       output_bytes > UINTPTR_MAX - (uintptr_t)output) {
        errno = ERANGE;
        return -1;
    }
    /* Compilation compacts hidden records, so even exact in-place operation
       could overwrite a strided input record that has not been examined. */
    if(output_bytes &&
       ((input_bytes && ranges_overlap((uintptr_t)output, output_bytes,
                                      (uintptr_t)stream->instances,
                                      input_bytes)) ||
        ranges_overlap((uintptr_t)output, output_bytes,
                       (uintptr_t)atlas->cells, cell_bytes))) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < stream->instance_count; ++i) {
        const pvr_sprite_instance_t *instance = instance_at(stream, i);

        if(!instance_valid(instance, atlas->cell_count, screen_space) ||
           !cell_valid(&atlas->cells[instance->cell_index])) {
            errno = EINVAL;
            return -1;
        }
        if(!(instance->flags & PVR_SPRITE_INSTANCE_HIDDEN))
            ++visible;
    }
    if(output_capacity < visible) {
        errno = ENOSPC;
        return -1;
    }
    *visible_count = visible;
    return 0;
}

static int build_packet(const pvr_sprite_cell_t *cell,
                        const pvr_sprite_instance_t *instance,
                        const pvr_sprite_billboard_basis_t *basis,
                        pvr_sprite_txr_t *output) {
    float sine;
    float cosine;
    float left = -cell->origin_x * cell->width * instance->scale_x;
    float right = (1.0f - cell->origin_x) * cell->width * instance->scale_x;
    float top = -cell->origin_y * cell->height * instance->scale_y;
    float bottom = (1.0f - cell->origin_y) * cell->height * instance->scale_y;
    float local_x[4] = { left, left, right, right };
    float local_y[4] = { bottom, top, top, bottom };
    float x[4];
    float y[4];
    float z[4];
    float u_left = cell->u0;
    float u_right = cell->u1;
    float v_top = cell->v0;
    float v_bottom = cell->v1;
    size_t i;

#ifdef __DREAMCAST__
    {
        shz_sincos_t value = shz_sincosf(instance->rotation);

        sine = value.sin;
        cosine = value.cos;
    }
#else
    sine = sinf(instance->rotation);
    cosine = cosf(instance->rotation);
#endif
    if(!isfinite(sine) || !isfinite(cosine)) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < 4u; ++i) {
        float rotated_x = local_x[i] * cosine - local_y[i] * sine;
        float rotated_y = local_x[i] * sine + local_y[i] * cosine;

        x[i] = instance->position.x + basis->x_axis.x * rotated_x +
               basis->y_axis.x * rotated_y;
        y[i] = instance->position.y + basis->x_axis.y * rotated_x +
               basis->y_axis.y * rotated_y;
        z[i] = instance->position.z + basis->x_axis.z * rotated_x +
               basis->y_axis.z * rotated_y;
        if(!finite3(x[i], y[i], z[i])) {
            errno = ERANGE;
            return -1;
        }
    }

    if(instance->flags & PVR_SPRITE_INSTANCE_FLIP_U) {
        float swap = u_left;
        u_left = u_right;
        u_right = swap;
    }
    if(instance->flags & PVR_SPRITE_INSTANCE_FLIP_V) {
        float swap = v_top;
        v_top = v_bottom;
        v_bottom = swap;
    }

    memset(output, 0, sizeof(*output));
    output->flags = PVR_CMD_VERTEX_EOL;
    output->ax = x[0];
    output->ay = y[0];
    output->az = z[0];
    output->bx = x[1];
    output->by = y[1];
    output->bz = z[1];
    output->cx = x[2];
    output->cy = y[2];
    output->cz = z[2];
    output->dx = x[3];
    output->dy = y[3];
    /* The existing sprite packet carries A/B/C UVs only. D is inferred by
       the TA from the same rectangle ordering as its missing depth. */
    output->auv = PVR_PACK_16BIT_UV(u_left, v_bottom);
    output->buv = PVR_PACK_16BIT_UV(u_left, v_top);
    output->cuv = PVR_PACK_16BIT_UV(u_right, v_top);
    return 0;
}

static int compile_packets(pvr_sprite_txr_t *output, size_t output_capacity,
                           const pvr_sprite_atlas_t *atlas,
                           const pvr_sprite_instance_stream_t *stream,
                           const pvr_sprite_billboard_basis_t *basis,
                           int screen_space,
                           pvr_sprite_batch_result_t *result) {
    pvr_sprite_batch_result_t progress = { 0, 0 };
    size_t visible_count;
    size_t produced = 0;
    size_t i;

    if(result)
        *result = progress;
    if(source_preflight(output, output_capacity, atlas, stream, screen_space,
                        &visible_count) < 0)
        return -1;
    if(!basis_valid(basis)) {
        errno = EINVAL;
        return -1;
    }

    /* Compute every visible rectangle once before publication so finite input
       that overflows only after scale/rotation cannot expose a partial
       batch. */
    for(i = 0; i < stream->instance_count; ++i) {
        const pvr_sprite_instance_t *instance = instance_at(stream, i);
        pvr_sprite_txr_t packet;

        if(!(instance->flags & PVR_SPRITE_INSTANCE_HIDDEN) &&
           build_packet(&atlas->cells[instance->cell_index], instance, basis,
                        &packet) < 0)
            return -1;
    }

    for(i = 0; i < stream->instance_count; ++i) {
        const pvr_sprite_instance_t *instance = instance_at(stream, i);

        if(instance->flags & PVR_SPRITE_INSTANCE_HIDDEN)
            continue;
        if(build_packet(&atlas->cells[instance->cell_index], instance, basis,
                        &output[produced]) < 0)
            return -1;
        ++produced;
    }
    progress.examined_instances = stream->instance_count;
    progress.produced_sprites = visible_count;
    if(result)
        *result = progress;
    return 0;
}

int pvr_sprite_batch_compile_2d(
        pvr_sprite_txr_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_sprite_instance_stream_t *stream,
        pvr_sprite_batch_result_t *result) {
    const pvr_sprite_billboard_basis_t basis = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f }
    };

    return compile_packets(output, output_capacity, atlas, stream, &basis, 1,
                           result);
}

int pvr_sprite_batch_compile_3d(
        pvr_sprite_txr_t *output, size_t output_capacity,
        const pvr_sprite_atlas_t *atlas,
        const pvr_sprite_instance_stream_t *stream,
        const pvr_sprite_billboard_basis_t *basis,
        const matrix_t *world_to_screen,
        pvr_sprite_batch_result_t *result) {
    pvr_sprite_batch_result_t progress = { 0, 0 };
    pvr_geometry_vertex_stream_t geometry;
    pvr_geometry_result_t projected;

    if(result)
        *result = progress;
    if(!matrix_valid(world_to_screen)) {
        errno = EINVAL;
        return -1;
    }
    if(compile_packets(output, output_capacity, atlas, stream, basis, 0,
                       &progress) < 0)
        return -1;

    geometry.vertices = output;
    geometry.vertex_count = progress.produced_sprites;
    geometry.stride = sizeof(*output);
    geometry.format = PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED;
    if(pvr_geometry_project_vertices(output, output_capacity, &geometry,
                                     world_to_screen, &projected) < 0) {
        progress.produced_sprites = projected.produced_vertices;
        if(result)
            *result = progress;
        return -1;
    }
    if(result)
        *result = progress;
    return 0;
}
