/* Internal compact-model render helpers shared with the draw-cache builder. */

#ifndef __PVR_CHUNK_RENDER_INTERNAL_H
#define __PVR_CHUNK_RENDER_INTERNAL_H

#include <dc/pvr_chunk_render.h>

int pvr_chunk_render_validate_state_record(
    const pvr_chunk_record_t *record);

int pvr_chunk_render_vertex_attributes_get(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes);

void pvr_chunk_render_update_state(pvr_chunk_render_state_t *state,
                                   const pvr_chunk_record_t *record);

#endif /* __PVR_CHUNK_RENDER_INTERNAL_H */
