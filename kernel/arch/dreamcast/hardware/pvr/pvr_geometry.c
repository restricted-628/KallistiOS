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

static int polygon_list(pvr_list_t list) {
    return list == PVR_LIST_OP_POLY || list == PVR_LIST_TR_POLY ||
           list == PVR_LIST_PT_POLY;
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

int pvr_geometry_project(pvr_vertex_t *output, size_t output_capacity,
                         const pvr_geometry_stream_t *stream,
                         const matrix_t *matrix,
                         pvr_geometry_result_t *result) {
    pvr_geometry_result_t progress = { 0, 0 };
    uintptr_t input_start;
    uintptr_t output_start;
    size_t input_bytes;
    size_t output_bytes;
    size_t i;
#ifdef __DREAMCAST__
    shz_mat4x4_t saved_xmtrx;
    shz_mat4x4_t transform;
#endif

    if(result)
        *result = progress;

    if(!output || !stream || !matrix || !stream->vertices ||
       ((uintptr_t)output & 31u) ||
       ((uintptr_t)stream->vertices & 3u) || !matrix_aligned(matrix) ||
       stream->stride < sizeof(pvr_vertex_t) || (stream->stride & 3u)) {
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
       (SIZE_MAX - sizeof(pvr_vertex_t)) / stream->stride ||
       stream->vertex_count > SIZE_MAX / sizeof(pvr_vertex_t)) {
        errno = ERANGE;
        return -1;
    }

    input_bytes = (stream->vertex_count - 1u) * stream->stride +
                  sizeof(pvr_vertex_t);
    output_bytes = stream->vertex_count * sizeof(pvr_vertex_t);
    input_start = (uintptr_t)stream->vertices;
    output_start = (uintptr_t)output;

    if(input_bytes > UINTPTR_MAX - input_start ||
       output_bytes > UINTPTR_MAX - output_start) {
        errno = ERANGE;
        return -1;
    }

    /* Forward processing is safe only for exact canonical in-place data.
       Shifted overlap could overwrite an input vertex not yet consumed. */
    if(!(input_start == output_start &&
         stream->stride == sizeof(pvr_vertex_t)) &&
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
        pvr_vertex_t vertex;
        float tx;
        float ty;
        float tw;
        float reciprocal_w;
#ifdef __DREAMCAST__
        shz_vec4_t transformed;
#endif

        /* Work on a complete local TA block so a rejected vertex cannot leave
           partially updated coordinates or attributes in caller storage. */
        memcpy(&vertex, source, sizeof(vertex));

        if(vertex.flags != PVR_CMD_VERTEX &&
           vertex.flags != PVR_CMD_VERTEX_EOL) {
            errno = EILSEQ;
            goto fail;
        }

        if(!isfinite(vertex.x) || !isfinite(vertex.y) ||
           !isfinite(vertex.z)) {
            errno = EDOM;
            goto fail;
        }

#ifdef __DREAMCAST__
        transformed = shz_xmtrx_transform_vec4(
            shz_vec4_init(vertex.x, vertex.y, vertex.z, 1.0f));
        tx = transformed.x;
        ty = transformed.y;
        tw = transformed.w;
#else
        tx = (*matrix)[0][0] * vertex.x +
             (*matrix)[1][0] * vertex.y +
             (*matrix)[2][0] * vertex.z + (*matrix)[3][0];
        ty = (*matrix)[0][1] * vertex.x +
             (*matrix)[1][1] * vertex.y +
             (*matrix)[2][1] * vertex.z + (*matrix)[3][1];
        tw = (*matrix)[0][3] * vertex.x +
             (*matrix)[1][3] * vertex.y +
             (*matrix)[2][3] * vertex.z + (*matrix)[3][3];
#endif

        if(!isfinite(tx) || !isfinite(ty) || !isfinite(tw)) {
            errno = ERANGE;
            goto fail;
        }

        if(tw <= FLT_MIN) {
            errno = EDOM;
            goto fail;
        }

        /* Unlike a conventional retained-mode position, TA depth is 1/W.
           Keeping this conversion here makes every sink consume identical
           canonical pvr_vertex_t data. */
        reciprocal_w = 1.0f / tw;
        vertex.x = tx * reciprocal_w;
        vertex.y = ty * reciprocal_w;
        vertex.z = reciprocal_w;

        if(!isfinite(vertex.x) || !isfinite(vertex.y) ||
           !isfinite(vertex.z)) {
            errno = ERANGE;
            goto fail;
        }

        memcpy(output + i, &vertex, sizeof(vertex));
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
