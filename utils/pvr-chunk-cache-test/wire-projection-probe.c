/* Count requested endpoint projections in host tests without a production
   instrumentation hook or a replacement for the actual projection math. */
#include <dc/pvr_geometry.h>
#include "../../kernel/arch/dreamcast/hardware/pvr/pvr_frustum_internal.h"

size_t wire_projected_vertices;

static int wire_counted_project(pvr_vertex_t *output, size_t capacity,
    const pvr_geometry_stream_t *stream, const matrix_t *matrix,
    pvr_geometry_result_t *result) {
    if(stream)
        wire_projected_vertices += stream->vertex_count;
    return pvr_geometry_project(output, capacity, stream, matrix, result);
}

static int wire_counted_clip(pvr_vertex_t output[2], const pvr_vertex_t input[2],
    const pvr_frustum_t *frustum, uint32_t attributes,
    pvr_frustum_segment_result_t *result) {
    wire_projected_vertices += 2;
    return pvr_frustum_clip_segment(output, input, frustum, attributes, result);
}

static int wire_counted_clip_cached(pvr_vertex_t output[2], const pvr_vertex_t input[2],
    const pvr_frustum_t *frustum, uint32_t attributes,
    pvr_frustum_segment_result_t *result,
    pvr_frustum_segment_cache_t *cache, const size_t indices[2]) {
    /* Count requested cache misses, not successful transforms on error. Wire
       topology guarantees distinct slots for the two endpoints of an edge. */
    for(size_t i = 0; i < 2; ++i)
        wire_projected_vertices += cache->indices[indices[i] % 3u] != indices[i];
    return pvr_frustum_clip_segment_cached(output, input, frustum, attributes,
                                           result, cache, indices);
}

#define pvr_geometry_project wire_counted_project
#define pvr_frustum_clip_segment wire_counted_clip
#define pvr_frustum_clip_segment_cached wire_counted_clip_cached
#include "../../kernel/arch/dreamcast/hardware/pvr/pvr_chunk_wire.c"
#undef pvr_frustum_clip_segment_cached
#undef pvr_frustum_clip_segment
#undef pvr_geometry_project
