/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_FRUSTUM_INTERNAL_H
#define PVR_FRUSTUM_INTERNAL_H

#include <dc/pvr_frustum.h>

/* Private admitted-wire cache, keyed by strip-reference index. Only original
   homogeneous positions are retained, never segment intersections or colors.
   Clear before a strip and after any callback that may change its positions,
   matrix or FP modes. No cache state escapes the synchronous draw. */
typedef struct pvr_frustum_segment_cache {
    struct { float x, y, w; } positions[3];
    size_t indices[3];
} pvr_frustum_segment_cache_t;

static inline void pvr_frustum_segment_cache_clear(pvr_frustum_segment_cache_t *cache) {
    for(size_t i = 0; i < 3u; ++i)
        cache->indices[i] = SIZE_MAX;
}

/* Same checked segment behavior, with the additional internal precondition
   that equal reference indices have unchanged source positions and transform.
   Cache and indices are valid, disjoint from input/output and non-null.
   Flags/attributes are checked and assembled anew even on a position hit. */
int pvr_frustum_clip_segment_cached(
    pvr_vertex_t output[2], const pvr_vertex_t input[2],
    const pvr_frustum_t *frustum, uint32_t attributes,
    pvr_frustum_segment_result_t *result,
    pvr_frustum_segment_cache_t *cache, const size_t indices[2]);

#endif
