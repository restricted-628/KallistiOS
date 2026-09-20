/* Count requested endpoint projections in host tests without a production
   instrumentation hook or a replacement for the actual projection math. */
#include <dc/pvr_geometry.h>

size_t wire_projected_vertices;

static int wire_counted_project(pvr_vertex_t *output, size_t capacity,
    const pvr_geometry_stream_t *stream, const matrix_t *matrix,
    pvr_geometry_result_t *result) {
    if(stream)
        wire_projected_vertices += stream->vertex_count;
    return pvr_geometry_project(output, capacity, stream, matrix, result);
}

#define pvr_geometry_project wire_counted_project
#include "../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_wire.c"
#undef pvr_geometry_project
