/* KallistiOS ##version##

   pvr_geometry.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_geometry.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef __DREAMCAST__
#include "pvr_internal.h"
#endif

_Static_assert(sizeof(pvr_vertex_t) == 32,
               "canonical PVR vertices must occupy one TA block");
_Static_assert(sizeof(pvr_vertex_pcm_t) == 32,
               "two-volume color vertices must occupy one TA block");
_Static_assert(sizeof(pvr_vertex_tpcm_t) == 64,
               "textured two-volume vertices must occupy two TA blocks");
_Static_assert(sizeof(pvr_modifier_vol_t) == 64,
               "modifier triangles must occupy two TA blocks");
_Static_assert(sizeof(pvr_sprite_txr_t) == 64,
               "textured sprites must occupy two TA blocks");

static int polygon_list(pvr_list_t list) {
    return list == PVR_LIST_OP_POLY || list == PVR_LIST_TR_POLY ||
           list == PVR_LIST_PT_POLY;
}

static int modifier_list(pvr_list_t list) {
    return list == PVR_LIST_OP_MOD || list == PVR_LIST_TR_MOD;
}

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
}

static int matrix_finite(const matrix_t *matrix) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            if(!isfinite((*matrix)[column][row]))
                return 0;
        }
    }

    return 1;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static size_t vertex_format_size(pvr_geometry_vertex_format_t format) {
    switch(format) {
        case PVR_GEOMETRY_VERTEX_CANONICAL:
            return sizeof(pvr_vertex_t);
        case PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR:
            return sizeof(pvr_vertex_pcm_t);
        case PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED:
            return sizeof(pvr_vertex_tpcm_t);
        case PVR_GEOMETRY_VERTEX_MODIFIER:
            return sizeof(pvr_modifier_vol_t);
        case PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED:
            return sizeof(pvr_sprite_txr_t);
        default:
            return 0;
    }
}

static int project_position(const matrix_t *matrix, float x, float y, float z,
                            float *screen_x, float *screen_y, float *depth) {
    float tx;
    float ty;
    float tw;
    float reciprocal_w;

    if(!isfinite(x) || !isfinite(y) || !isfinite(z)) {
        errno = EDOM;
        return -1;
    }
#ifdef __DREAMCAST__
    (void)matrix;
    {
        shz_vec4_t transformed = shz_xmtrx_transform_vec4(
            shz_vec4_init(x, y, z, 1.0f));

        tx = transformed.x;
        ty = transformed.y;
        tw = transformed.w;
    }
#else
    tx = (*matrix)[0][0] * x + (*matrix)[1][0] * y +
         (*matrix)[2][0] * z + (*matrix)[3][0];
    ty = (*matrix)[0][1] * x + (*matrix)[1][1] * y +
         (*matrix)[2][1] * z + (*matrix)[3][1];
    tw = (*matrix)[0][3] * x + (*matrix)[1][3] * y +
         (*matrix)[2][3] * z + (*matrix)[3][3];
#endif
    if(!isfinite(tx) || !isfinite(ty) || !isfinite(tw)) {
        errno = ERANGE;
        return -1;
    }
    if(tw <= FLT_MIN) {
        errno = EDOM;
        return -1;
    }
    reciprocal_w = 1.0f / tw;
    *screen_x = tx * reciprocal_w;
    *screen_y = ty * reciprocal_w;
    *depth = reciprocal_w;
    if(!isfinite(*screen_x) || !isfinite(*screen_y) || !isfinite(*depth)) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

int pvr_geometry_project_vertices(
    void *output, size_t output_capacity,
    const pvr_geometry_vertex_stream_t *stream,
    const matrix_t *matrix, pvr_geometry_result_t *result) {
    pvr_geometry_result_t progress = { 0, 0 };
    uintptr_t input_start;
    uintptr_t output_start;
    size_t input_bytes;
    size_t output_bytes;
    size_t vertex_size;
    size_t i;
#ifdef __DREAMCAST__
    shz_mat4x4_t saved_xmtrx;
    shz_mat4x4_t transform;
#endif

    if(result)
        *result = progress;

    vertex_size = stream ? vertex_format_size(stream->format) : 0;
    if(!output || !stream || !matrix || !stream->vertices || !vertex_size ||
       ((uintptr_t)output & 31u) ||
       ((uintptr_t)stream->vertices & 3u) || !matrix_aligned(matrix) ||
       stream->stride < vertex_size || (stream->stride & 3u)) {
        errno = EINVAL;
        return -1;
    }

    if(output_capacity < stream->vertex_count) {
        errno = ENOSPC;
        return -1;
    }

    if(!matrix_finite(matrix)) {
        errno = EDOM;
        return -1;
    }

    if(!stream->vertex_count)
        return 0;

    /* Establish both complete byte ranges before doing pointer arithmetic or
       allowing the first output write. */
    if(stream->vertex_count - 1u >
       (SIZE_MAX - vertex_size) / stream->stride ||
       stream->vertex_count > SIZE_MAX / vertex_size) {
        errno = ERANGE;
        return -1;
    }

    input_bytes = (stream->vertex_count - 1u) * stream->stride +
                  vertex_size;
    output_bytes = stream->vertex_count * vertex_size;
    input_start = (uintptr_t)stream->vertices;
    output_start = (uintptr_t)output;

    if(input_bytes > UINTPTR_MAX - input_start ||
       output_bytes > UINTPTR_MAX - output_start) {
        errno = ERANGE;
        return -1;
    }

    /* Forward processing is safe only for an exact packed in-place stream.
       Shifted overlap could overwrite an input vertex not yet consumed. */
    if(!(input_start == output_start &&
         stream->stride == vertex_size) &&
       ranges_overlap(input_start, input_bytes, output_start, output_bytes)) {
        errno = EINVAL;
        return -1;
    }

#ifdef __DREAMCAST__
    /* A geometry stream amortizes one XMTRX load across every vertex. Preserve
       the caller's matrix so this checked memory-to-memory API remains free of
       observable accelerator state even on a rejected vertex. */
    shz_kos_matrix_import(&transform, matrix);
    shz_xmtrx_store_4x4(&saved_xmtrx);
    shz_xmtrx_load_4x4(&transform);
#endif

    for(i = 0; i < stream->vertex_count; ++i) {
        const uint8_t *source = (const uint8_t *)stream->vertices +
                                i * stream->stride;
        pvr_vertex_tpcm_t packet;
        uint8_t *packet_bytes = (uint8_t *)&packet;
        uint32_t flags;
        float x;
        float y;
        float z;

        /* Stage the complete one- or two-block packet so a rejected vertex
           cannot expose partially transformed state. */
        memcpy(packet_bytes, source, vertex_size);
        memcpy(&flags, packet_bytes, sizeof(flags));
        memcpy(&x, packet_bytes + 4u, sizeof(x));
        memcpy(&y, packet_bytes + 8u, sizeof(y));
        memcpy(&z, packet_bytes + 12u, sizeof(z));

        if(flags != PVR_CMD_VERTEX && flags != PVR_CMD_VERTEX_EOL) {
            errno = EILSEQ;
            goto fail;
        }

        if(stream->format == PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED) {
            static const size_t offsets[] = { 4u, 16u, 28u };
            float source_x[3];
            float source_y[3];
            float source_z[3];
            float fourth_x;
            float fourth_y;
            float fourth_z;
            size_t position;

            for(position = 0; position < 3u; ++position) {
                size_t offset = offsets[position];

                memcpy(&source_x[position], packet_bytes + offset,
                       sizeof(float));
                memcpy(&source_y[position], packet_bytes + offset + 4u,
                       sizeof(float));
                memcpy(&source_z[position], packet_bytes + offset + 8u,
                       sizeof(float));
            }
            memcpy(&fourth_x, packet_bytes + 40u, sizeof(fourth_x));
            memcpy(&fourth_y, packet_bytes + 44u, sizeof(fourth_y));
            fourth_z = source_z[0] + source_z[2] - source_z[1];
            if(!isfinite(fourth_x) || !isfinite(fourth_y) ||
               !isfinite(fourth_z)) {
                errno = EDOM;
                goto fail;
            }

            for(position = 0; position < 3u; ++position) {
                size_t offset = offsets[position];

                if(project_position(matrix, source_x[position],
                                    source_y[position], source_z[position],
                                    &x, &y, &z) < 0)
                    goto fail;
                memcpy(packet_bytes + offset, &x, sizeof(x));
                memcpy(packet_bytes + offset + 4u, &y, sizeof(y));
                memcpy(packet_bytes + offset + 8u, &z, sizeof(z));
            }
            if(project_position(matrix, fourth_x, fourth_y, fourth_z,
                                &x, &y, &z) < 0)
                goto fail;
            memcpy(packet_bytes + 40u, &x, sizeof(x));
            memcpy(packet_bytes + 44u, &y, sizeof(y));
        }
        else {
            if(project_position(matrix, x, y, z, &x, &y, &z) < 0)
                goto fail;
            memcpy(packet_bytes + 4u, &x, sizeof(x));
            memcpy(packet_bytes + 8u, &y, sizeof(y));
            memcpy(packet_bytes + 12u, &z, sizeof(z));
        }

        if(stream->format == PVR_GEOMETRY_VERTEX_MODIFIER) {
            static const size_t offsets[] = { 16u, 28u };
            size_t position;

            for(position = 0; position < 2u; ++position) {
                size_t offset = offsets[position];

                memcpy(&x, packet_bytes + offset, sizeof(x));
                memcpy(&y, packet_bytes + offset + 4u, sizeof(y));
                memcpy(&z, packet_bytes + offset + 8u, sizeof(z));
                if(project_position(matrix, x, y, z, &x, &y, &z) < 0)
                    goto fail;
                memcpy(packet_bytes + offset, &x, sizeof(x));
                memcpy(packet_bytes + offset + 4u, &y, sizeof(y));
                memcpy(packet_bytes + offset + 8u, &z, sizeof(z));
            }
        }
        memcpy((uint8_t *)output + i * vertex_size, packet_bytes, vertex_size);
        ++progress.consumed_vertices;
        ++progress.produced_vertices;
    }

#ifdef __DREAMCAST__
    shz_xmtrx_load_4x4(&saved_xmtrx);
#endif
    if(result)
        *result = progress;

    return 0;

fail:
#ifdef __DREAMCAST__
    shz_xmtrx_load_4x4(&saved_xmtrx);
#endif
    if(result)
        *result = progress;

    return -1;
}

int pvr_geometry_project(pvr_vertex_t *output, size_t output_capacity,
                         const pvr_geometry_stream_t *stream,
                         const matrix_t *matrix,
                         pvr_geometry_result_t *result) {
    pvr_geometry_vertex_stream_t typed_stream;

    if(!stream)
        return pvr_geometry_project_vertices(output, output_capacity, NULL,
                                             matrix, result);

    typed_stream.vertices = stream->vertices;
    typed_stream.vertex_count = stream->vertex_count;
    typed_stream.stride = stream->stride;
    typed_stream.format = PVR_GEOMETRY_VERTEX_CANONICAL;
    return pvr_geometry_project_vertices(output, output_capacity,
                                         &typed_stream, matrix, result);
}

int pvr_geometry_sink_init_memory(pvr_geometry_sink_t *sink,
                                  pvr_vertex_t *vertices, size_t capacity) {
    if(!sink || !vertices || !capacity || ((uintptr_t)vertices & 31u)) {
        errno = EINVAL;
        return -1;
    }

    if(capacity > SIZE_MAX / sizeof(pvr_vertex_t) ||
       capacity * sizeof(pvr_vertex_t) >
       UINTPTR_MAX - (uintptr_t)vertices) {
        errno = ERANGE;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_MEMORY;
    sink->destination.memory.vertices = vertices;
    sink->destination.memory.capacity = capacity;
    return 0;
}

int pvr_geometry_sink_init_current(pvr_geometry_sink_t *sink) {
    if(!sink) {
        errno = EINVAL;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_CURRENT_LIST;
    return 0;
}

int pvr_geometry_sink_init_buffered(pvr_geometry_sink_t *sink,
                                    pvr_list_t list) {
    if(!sink || !polygon_list(list)) {
        errno = EINVAL;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_BUFFERED_LIST;
    sink->destination.list = list;
    return 0;
}

int pvr_geometry_sink_emit(pvr_geometry_sink_t *sink,
                           const pvr_vertex_t *vertices, size_t count) {
    size_t bytes;
    int saved_errno;
    int rv;

    if(!sink) {
        errno = EINVAL;
        return -1;
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               !sink->destination.memory.capacity ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->destination.memory.capacity >
               SIZE_MAX / sizeof(pvr_vertex_t) ||
               sink->destination.memory.capacity * sizeof(pvr_vertex_t) >
               UINTPTR_MAX -
               (uintptr_t)sink->destination.memory.vertices ||
               sink->emitted_vertices >
               sink->destination.memory.capacity) {
                errno = EINVAL;
                return -1;
            }
            break;

        case PVR_GEOMETRY_SINK_CURRENT_LIST:
            break;

        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            if(!polygon_list(sink->destination.list)) {
                errno = EINVAL;
                return -1;
            }
            break;

        default:
            errno = EINVAL;
            return -1;
    }

    /* A zero emission is a no-op after the sink's persistent structure has
       been validated; it does not require an active PVR scene. */
    if(!count)
        return 0;

    if(!vertices || ((uintptr_t)vertices & 31u)) {
        errno = EINVAL;
        return -1;
    }

    if(count > SIZE_MAX / sizeof(pvr_vertex_t) ||
       count > SIZE_MAX - sink->emitted_vertices) {
        errno = ERANGE;
        return -1;
    }

    bytes = count * sizeof(pvr_vertex_t);

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(count > sink->destination.memory.capacity -
                       sink->emitted_vertices) {
                errno = ENOSPC;
                return -1;
            }

            memmove(sink->destination.memory.vertices +
                    sink->emitted_vertices, vertices, bytes);
            break;

        case PVR_GEOMETRY_SINK_CURRENT_LIST:
#ifdef __DREAMCAST__
            /* Current-list sinks may be prepared before a scene. Validate the
               transient scene/list state immediately before submission. */
            if(!pvr_state.scene_active ||
               !polygon_list(pvr_state.list_reg_open)) {
                errno = EPERM;
                return -1;
            }
#endif
            saved_errno = errno;
            errno = 0;
            rv = pvr_prim(vertices, bytes);
            if(rv < 0) {
                if(!errno)
                    errno = EPERM;
                return -1;
            }
            errno = saved_errno;
            break;

        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            saved_errno = errno;
            errno = 0;
            rv = pvr_list_prim(sink->destination.list, vertices, bytes);
            if(rv < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
            errno = saved_errno;
            break;

        default:
            /* Validated before the zero-count fast path. */
            __builtin_unreachable();
    }

    sink->emitted_vertices += count;
    return 0;
}

int pvr_geometry_vertex_sink_init_memory(
    pvr_geometry_vertex_sink_t *sink,
    pvr_geometry_vertex_format_t format,
    void *vertices, size_t capacity) {
    size_t vertex_size = vertex_format_size(format);

    if(!sink || !vertices || !capacity || !vertex_size ||
       ((uintptr_t)vertices & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(capacity > SIZE_MAX / vertex_size ||
       capacity * vertex_size > UINTPTR_MAX - (uintptr_t)vertices) {
        errno = ERANGE;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_MEMORY;
    sink->format = format;
    sink->destination.memory.vertices = vertices;
    sink->destination.memory.capacity = capacity;
    return 0;
}

int pvr_geometry_vertex_sink_init_current(
    pvr_geometry_vertex_sink_t *sink,
    pvr_geometry_vertex_format_t format) {
    if(!sink || !vertex_format_size(format)) {
        errno = EINVAL;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_CURRENT_LIST;
    sink->format = format;
    return 0;
}

int pvr_geometry_vertex_sink_init_buffered(
    pvr_geometry_vertex_sink_t *sink,
    pvr_geometry_vertex_format_t format, pvr_list_t list) {
    if(!sink || !vertex_format_size(format) ||
       (format == PVR_GEOMETRY_VERTEX_MODIFIER ?
        !modifier_list(list) : !polygon_list(list))) {
        errno = EINVAL;
        return -1;
    }

    memset(sink, 0, sizeof(*sink));
    sink->kind = PVR_GEOMETRY_SINK_BUFFERED_LIST;
    sink->format = format;
    sink->destination.list = list;
    return 0;
}

static int vertex_sink_valid(const pvr_geometry_vertex_sink_t *sink,
                             size_t vertex_size) {
    if(!sink || !vertex_size) {
        errno = EINVAL;
        return -1;
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               !sink->destination.memory.capacity ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->destination.memory.capacity > SIZE_MAX / vertex_size ||
               sink->destination.memory.capacity * vertex_size >
               UINTPTR_MAX -
               (uintptr_t)sink->destination.memory.vertices ||
               sink->emitted_vertices > sink->destination.memory.capacity) {
                errno = EINVAL;
                return -1;
            }
            break;
        case PVR_GEOMETRY_SINK_CURRENT_LIST:
            break;
        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            if(sink->format == PVR_GEOMETRY_VERTEX_MODIFIER ?
               !modifier_list(sink->destination.list) :
               !polygon_list(sink->destination.list)) {
                errno = EINVAL;
                return -1;
            }
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    return 0;
}

int pvr_geometry_vertex_sink_emit(
    pvr_geometry_vertex_sink_t *sink,
    const void *vertices, size_t count) {
    size_t vertex_size = sink ? vertex_format_size(sink->format) : 0;
    size_t bytes;
    size_t i;
    int saved_errno;
    int rv;

    if(vertex_sink_valid(sink, vertex_size) < 0)
        return -1;
    if(!count)
        return 0;
    if(!vertices || ((uintptr_t)vertices & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(count > SIZE_MAX / vertex_size ||
       count > SIZE_MAX - sink->emitted_vertices) {
        errno = ERANGE;
        return -1;
    }

    bytes = count * vertex_size;
    if(bytes > UINTPTR_MAX - (uintptr_t)vertices) {
        errno = ERANGE;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       count > sink->destination.memory.capacity - sink->emitted_vertices) {
        errno = ENOSPC;
        return -1;
    }

    /* Validate every packet before making a list call or memory write. A
       two-block textured vertex still carries its command in the first word. */
    for(i = 0; i < count; ++i) {
        uint32_t command;

        memcpy(&command, (const uint8_t *)vertices + i * vertex_size,
               sizeof(command));
        if(command != PVR_CMD_VERTEX && command != PVR_CMD_VERTEX_EOL) {
            errno = EILSEQ;
            return -1;
        }
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            memmove((uint8_t *)sink->destination.memory.vertices +
                    sink->emitted_vertices * vertex_size, vertices, bytes);
            break;
        case PVR_GEOMETRY_SINK_CURRENT_LIST:
#ifdef __DREAMCAST__
            if(!pvr_state.scene_active ||
               (sink->format == PVR_GEOMETRY_VERTEX_MODIFIER ?
                !modifier_list(pvr_state.list_reg_open) :
                !polygon_list(pvr_state.list_reg_open))) {
                errno = EPERM;
                return -1;
            }
#endif
            saved_errno = errno;
            errno = 0;
            rv = pvr_prim(vertices, bytes);
            if(rv < 0) {
                if(!errno)
                    errno = EPERM;
                return -1;
            }
            errno = saved_errno;
            break;
        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            saved_errno = errno;
            errno = 0;
            rv = pvr_list_prim(sink->destination.list, vertices, bytes);
            if(rv < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
            errno = saved_errno;
            break;
        default:
            __builtin_unreachable();
    }

    sink->emitted_vertices += count;
    return 0;
}
