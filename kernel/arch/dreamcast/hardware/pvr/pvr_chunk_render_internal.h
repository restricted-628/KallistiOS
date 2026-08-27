/* Internal compact-model render helpers shared with the draw-cache builder. */

#ifndef __PVR_CHUNK_RENDER_INTERNAL_H
#define __PVR_CHUNK_RENDER_INTERNAL_H

#include <dc/pvr_chunk_render.h>

int pvr_chunk_render_validate_state_record(
    const pvr_chunk_record_t *record);

int pvr_chunk_render_validate_two_volume_state_record(
    const pvr_chunk_record_t *record);

size_t pvr_chunk_render_two_volume_format_size(
    pvr_geometry_vertex_format_t format);

pvr_geometry_vertex_format_t pvr_chunk_render_two_volume_strip_format(
    uint8_t type);

typedef int (*pvr_chunk_render_modifier_visitor_t)(
    const uint16_t indices[3], const uint16_t *user_words,
    size_t user_word_count, uint8_t source_type, int final_in_volume,
    void *data);

int pvr_chunk_render_modifier_triangle_count(
    const pvr_chunk_record_t *record, size_t *count);

int pvr_chunk_render_visit_modifier_record(
    const pvr_chunk_record_t *record,
    pvr_chunk_render_modifier_visitor_t visitor, void *data);

int pvr_chunk_render_modifier_sink_valid(
    const pvr_geometry_vertex_sink_t *sink);

int pvr_chunk_render_modifier_config_valid(
    const pvr_chunk_modifier_config_t *config,
    const pvr_geometry_vertex_sink_t *sink);

int pvr_chunk_render_publish_modifier(
    pvr_geometry_vertex_sink_t *sink,
    const pvr_chunk_modifier_config_t *config,
    const pvr_modifier_vol_t *triangle, uint32_t mode);

int pvr_chunk_render_vertex_attributes_get(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes);

void pvr_chunk_render_update_state(pvr_chunk_render_state_t *state,
                                   const pvr_chunk_record_t *record);

#endif /* __PVR_CHUNK_RENDER_INTERNAL_H */
