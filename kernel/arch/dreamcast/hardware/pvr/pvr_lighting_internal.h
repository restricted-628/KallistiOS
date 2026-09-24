/* KallistiOS ##version##

   pvr_lighting_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __PVR_LIGHTING_INTERNAL_H
#define __PVR_LIGHTING_INTERNAL_H

#include <dc/pvr_lighting.h>

/* Internal hot path for an immutable context already admitted through the
   public checked function. Sample, range, overlap, and output checks remain
   active. */
int pvr_lighting_apply_extended_admitted(
    pvr_lighting_output_t *output, size_t output_capacity,
    const pvr_lighting_extended_stream_t *stream,
    const pvr_lighting_extended_context_t *context,
    pvr_lighting_result_t *result);

#endif /* __PVR_LIGHTING_INTERNAL_H */
