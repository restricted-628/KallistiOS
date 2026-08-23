/* KallistiOS ##version##

   dc/pvr_geometry.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_geometry.h
    \brief   Checked, caller-owned PVR geometry preparation and output.
    \ingroup pvr_geometry

    This interface prepares canonical pvr_vertex_t streams without taking
    ownership of PVR scenes, polygon headers, materials, textures, or memory.
*/

#ifndef __DC_PVR_GEOMETRY_H
#define __DC_PVR_GEOMETRY_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>

#include <dc/matrix.h>
#include <dc/pvr.h>

/** \addtogroup pvr_geometry
    @{
*/

/** \brief Strided input view over canonical PVR vertices.

    A stride larger than pvr_vertex_t permits positions and attributes to be
    embedded at the start of an application-owned extended vertex structure.
*/
typedef struct pvr_geometry_stream {
    const void *vertices;       /**< First input pvr_vertex_t. */
    size_t vertex_count;        /**< Number of input vertices. */
    size_t stride;              /**< Bytes between vertices. */
} pvr_geometry_stream_t;

/** \brief Progress from one checked projection operation. */
typedef struct pvr_geometry_result {
    size_t consumed_vertices;   /**< Input vertices accepted. */
    size_t produced_vertices;   /**< Valid vertices written to output. */
} pvr_geometry_result_t;

/** \brief Geometry output destination. */
typedef enum pvr_geometry_sink_kind {
    PVR_GEOMETRY_SINK_MEMORY = 0,       /**< Caller-owned memory. */
    PVR_GEOMETRY_SINK_CURRENT_LIST,     /**< Current direct or buffered list. */
    PVR_GEOMETRY_SINK_BUFFERED_LIST     /**< Explicit buffered PVR list. */
} pvr_geometry_sink_kind_t;

/** \brief Allocation-free geometry output sink.

    Initialize this structure with one of the pvr_geometry_sink_init_*()
    functions. Applications retain ownership of the structure and any memory
    destination. The emitted count changes only after a complete successful
    emission.
*/
typedef struct pvr_geometry_sink {
    pvr_geometry_sink_kind_t kind;
    size_t emitted_vertices;
    union {
        struct {
            pvr_vertex_t *vertices;
            size_t capacity;
        } memory;
        pvr_list_t list;
    } destination;
} pvr_geometry_sink_t;

/** \brief Project a strided vertex stream into caller-owned PVR vertices.

    The explicit column-major matrix transforms each XYZ position with W=1.
    Output X and Y are divided by transformed W, while output Z is 1/W, which
    is the depth convention consumed by the PVR. All other pvr_vertex_t fields
    are preserved byte-for-byte.

    Input vertex commands must be PVR_CMD_VERTEX or PVR_CMD_VERTEX_EOL. This
    function does not clip geometry: a vertex at or behind W=0 is rejected.
    On failure, `[0, result->produced_vertices)` is a valid output prefix and
    all later output vertices remain untouched. Exact in-place projection is
    supported when the input stride is sizeof(pvr_vertex_t); other overlapping
    input and output ranges are rejected.

    The output must be 32-byte aligned and have at least vertex_count entries.
    The matrix must satisfy matrix_t's alignment requirement. No global matrix
    state is read or changed.

    \param output          Caller-owned output vertex array.
    \param output_capacity Capacity of output in vertices.
    \param stream          Strided canonical input stream.
    \param matrix          Complete object-to-screen projection matrix.
    \param result          Optional progress destination, initialized to zero
                           before validation.

    \retval 0  All vertices were projected.
    \retval -1 Error, with errno set to EINVAL, EILSEQ, ENOSPC, EDOM, or
               ERANGE.
*/
int pvr_geometry_project(pvr_vertex_t *output, size_t output_capacity,
                         const pvr_geometry_stream_t *stream,
                         const matrix_t *matrix,
                         pvr_geometry_result_t *result);

/** \brief Initialize a caller-owned memory sink.

    \param sink        Sink to initialize.
    \param vertices    32-byte-aligned destination array.
    \param capacity    Capacity in vertices; must be nonzero.

    \retval 0  Success.
    \retval -1 Invalid arguments or address range, with errno set to EINVAL or
               ERANGE.
*/
int pvr_geometry_sink_init_memory(pvr_geometry_sink_t *sink,
                                  pvr_vertex_t *vertices, size_t capacity);

/** \brief Initialize a sink for the currently open PVR list.

    Emission calls pvr_prim(), so the established PVR configuration determines
    whether data goes directly through the store queues or into the current
    list's DMA staging buffer. The active list must be an opaque, translucent,
    or punch-through polygon list; modifier-volume lists use a different vertex
    format. Scene and list ownership remain with the caller.
*/
int pvr_geometry_sink_init_current(pvr_geometry_sink_t *sink);

/** \brief Initialize a sink for an explicit buffered PVR list.

    Emission calls pvr_list_prim(). The caller must own an active DMA scene and
    provide the list's staging allocation through the established PVR API.

    \retval 0  Success.
    \retval -1 Invalid sink or non-polygon list, with errno set to EINVAL.
*/
int pvr_geometry_sink_init_buffered(pvr_geometry_sink_t *sink,
                                    pvr_list_t list);

/** \brief Emit complete canonical vertices to a prepared sink.

    Memory destinations are preflighted and copied with overlap-safe semantics.
    Current and explicit-list destinations retain the all-or-nothing behavior
    of pvr_prim() and pvr_list_prim() for this call. The sink never begins,
    finishes, flushes, or otherwise changes scene ownership.

    \param sink        Initialized output sink.
    \param vertices    32-byte-aligned vertex array.
    \param count       Number of vertices. Zero is a successful no-op.

    \retval 0  Success.
    \retval -1 Error, with errno set appropriately.
*/
int pvr_geometry_sink_emit(pvr_geometry_sink_t *sink,
                           const pvr_vertex_t *vertices, size_t count);

/** @} */

__END_DECLS

#endif /* __DC_PVR_GEOMETRY_H */
