/* KallistiOS ##version##

   pvr_chunk_render.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_render.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "pvr_chunk_render_internal.h"

#ifdef __DREAMCAST__
#include "pvr_internal.h"
#endif

typedef struct render_requirements {
    size_t records;
    size_t strips;
    size_t vertices;
    size_t maximum_strip_vertices;
} render_requirements_t;

_Static_assert(sizeof(pvr_chunk_two_volume_vertex_t) ==
               sizeof(pvr_vertex_tpcm_t),
               "two-volume workspace entries must hold the largest packet");
_Static_assert(_Alignof(pvr_chunk_two_volume_vertex_t) == 32,
               "two-volume workspace entries must retain TA alignment");

static int checked_add(size_t *value, size_t addend) {
    if(addend > SIZE_MAX - *value) {
        errno = ERANGE;
        return -1;
    }

    *value += addend;
    return 0;
}

static int matrix_valid(const matrix_t *matrix) {
    size_t column;
    size_t row;

    if(!matrix || ((uintptr_t)matrix & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            if(!isfinite((*matrix)[column][row])) {
                errno = EDOM;
                return -1;
            }
        }
    }

    return 0;
}

static int range_get(const void *pointer, size_t count, size_t element_size,
                     uintptr_t *start, size_t *bytes) {
    uintptr_t address = (uintptr_t)pointer;

    if(count > SIZE_MAX / element_size) {
        errno = ERANGE;
        return -1;
    }

    *start = address;
    *bytes = count * element_size;
    if(*bytes > UINTPTR_MAX - address) {
        errno = ERANGE;
        return -1;
    }

    return 0;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs_size && rhs_size && lhs < rhs + rhs_size &&
           rhs < lhs + lhs_size;
}

static int sink_valid(const pvr_geometry_sink_t *sink) {
    if(!sink) {
        errno = EINVAL;
        return -1;
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               !sink->destination.memory.capacity ||
               sink->emitted_vertices > sink->destination.memory.capacity ||
               sink->destination.memory.capacity >
               SIZE_MAX / sizeof(pvr_vertex_t) ||
               sink->destination.memory.capacity * sizeof(pvr_vertex_t) >
               UINTPTR_MAX -
               (uintptr_t)sink->destination.memory.vertices) {
                errno = EINVAL;
                return -1;
            }
            break;
        case PVR_GEOMETRY_SINK_CURRENT_LIST:
            break;
        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            if(sink->destination.list != PVR_LIST_OP_POLY &&
               sink->destination.list != PVR_LIST_TR_POLY &&
               sink->destination.list != PVR_LIST_PT_POLY) {
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

static int unsupported_record(const pvr_chunk_record_t *record) {
    if(record->record_class == PVR_CHUNK_RECORD_VOLUME ||
       (record->record_class == PVR_CHUNK_RECORD_BITS &&
        (record->type == PVR_CHUNK_CONTROL_CACHE_POLYGONS ||
         record->type == PVR_CHUNK_CONTROL_DRAW_CACHED_POLYGONS)) ||
       (record->record_class == PVR_CHUNK_RECORD_TEXTURE &&
        record->type == PVR_CHUNK_TEXTURE_TWO_VOLUME) ||
       (record->record_class == PVR_CHUNK_RECORD_MATERIAL &&
        record->type >= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME) ||
       (record->record_class == PVR_CHUNK_RECORD_STRIP &&
        record->type >= PVR_CHUNK_STRIP_TWO_VOLUME)) {
        errno = ENOTSUP;
        return -1;
    }

    return 0;
}

static int validate_record_fields(const pvr_chunk_record_t *record) {
    if(record->record_class == PVR_CHUNK_RECORD_BITS) {
        if((record->type == PVR_CHUNK_CONTROL_BLEND &&
            (record->flags & UINT8_C(0xc0))) ||
           (record->type == PVR_CHUNK_CONTROL_MIPMAP_ADJUST &&
            (record->flags & UINT8_C(0xf0))) ||
           (record->type == PVR_CHUNK_CONTROL_SPECULAR_EXPONENT &&
            record->flags > 16u)) {
            errno = EILSEQ;
            return -1;
        }
    }
    else if(record->record_class == PVR_CHUNK_RECORD_MATERIAL) {
        const uint16_t *payload = record->payload;
        size_t specular_offset = SIZE_MAX;
        uint8_t type = record->type;

        if(record->flags & UINT8_C(0xc0)) {
            errno = EILSEQ;
            return -1;
        }

        if(type >= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME)
            type -= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME -
                    PVR_CHUNK_MATERIAL_DIFFUSE;

        switch(type) {
            case PVR_CHUNK_MATERIAL_SPECULAR:
                specular_offset = 0u;
                break;
            case PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR:
            case PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR:
                specular_offset = 2u;
                break;
            case PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR:
                specular_offset = 4u;
                break;
            default:
                break;
        }

        if(specular_offset != SIZE_MAX &&
           (payload[specular_offset + 1u] >> 8) > 16u) {
            errno = EILSEQ;
            return -1;
        }
    }
    else if(record->record_class == PVR_CHUNK_RECORD_STRIP &&
            (record->flags & UINT8_C(0x80))) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

int pvr_chunk_render_validate_state_record(
    const pvr_chunk_record_t *record) {
    if(unsupported_record(record) < 0)
        return -1;
    return validate_record_fields(record);
}

static int unsupported_two_volume_record(
    const pvr_chunk_record_t *record) {
    if(record->record_class == PVR_CHUNK_RECORD_VOLUME ||
       (record->record_class == PVR_CHUNK_RECORD_BITS &&
        (record->type == PVR_CHUNK_CONTROL_CACHE_POLYGONS ||
         record->type == PVR_CHUNK_CONTROL_DRAW_CACHED_POLYGONS)) ||
       (record->record_class == PVR_CHUNK_RECORD_MATERIAL &&
        record->type == PVR_CHUNK_MATERIAL_BUMP) ||
       (record->record_class == PVR_CHUNK_RECORD_STRIP &&
        record->type < PVR_CHUNK_STRIP_TWO_VOLUME)) {
        errno = ENOTSUP;
        return -1;
    }

    return 0;
}

int pvr_chunk_render_validate_two_volume_state_record(
    const pvr_chunk_record_t *record) {
    if(unsupported_two_volume_record(record) < 0)
        return -1;
    return validate_record_fields(record);
}

int pvr_chunk_render_vertex_attributes_get(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes) {
    if(plan)
        return pvr_chunk_model_plan_vertex_attributes_get(plan, index,
                                                          attributes);
    return pvr_chunk_model_vertex_attributes_get(view, index, attributes);
}

static int model_plan_ranges_get(
    const pvr_chunk_model_plan_t *plan,
    uintptr_t *plan_start, size_t *plan_bytes,
    uintptr_t *index_start, size_t *index_bytes) {
    *plan_start = 0;
    *plan_bytes = 0;
    *index_start = 0;
    *index_bytes = 0;
    if(!plan)
        return 0;

    if(range_get(plan, 1u, sizeof(*plan), plan_start, plan_bytes) < 0 ||
       range_get(plan->vertex_index, plan->vertex_index_count,
                 sizeof(*plan->vertex_index), index_start, index_bytes) < 0)
        return -1;
    return 0;
}

static int preflight_strip(const pvr_chunk_model_view_t *view,
                           const pvr_chunk_model_plan_t *plan,
                           const pvr_chunk_strip_view_t *strip,
                           int has_prepare_vertex,
                           render_requirements_t *requirements) {
    size_t vertex_index;

    if(checked_add(&requirements->strips, 1u) < 0 ||
       checked_add(&requirements->vertices, strip->vertex_count) < 0)
        return -1;

    if(strip->vertex_count > requirements->maximum_strip_vertices)
        requirements->maximum_strip_vertices = strip->vertex_count;

    for(vertex_index = 0; vertex_index < strip->vertex_count; ++vertex_index) {
        pvr_chunk_strip_attributes_t strip_attributes;
        pvr_chunk_vertex_attributes_t vertex_attributes;

        if(pvr_chunk_strip_attributes_get(strip, vertex_index,
                                          &strip_attributes) < 0 ||
           pvr_chunk_render_vertex_attributes_get(
               view, plan, strip_attributes.index, &vertex_attributes) < 0)
            return -1;

        if(!has_prepare_vertex &&
           ((vertex_attributes.present &
             (PVR_CHUNK_VERTEX_ATTR_DIFFUSE_INTENSITY |
              PVR_CHUNK_VERTEX_ATTR_SPECULAR_INTENSITY)) ||
            vertex_attributes.position.w != 1.0f)) {
            errno = ENOTSUP;
            return -1;
        }
    }

    return 0;
}

static int preflight(const pvr_chunk_model_view_t *view,
                     const pvr_chunk_model_plan_t *plan,
                     const matrix_t *object_to_screen,
                     const pvr_geometry_sink_t *sink,
                     const pvr_vertex_t *workspace, size_t workspace_count,
                     pvr_chunk_render_begin_strip_t begin_strip,
                     pvr_chunk_render_prepare_vertex_t prepare_vertex,
                     render_requirements_t *requirements) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    uintptr_t workspace_start;
    uintptr_t vertex_start;
    uintptr_t polygon_start;
    uintptr_t matrix_start;
    uintptr_t output_start = 0;
    uintptr_t plan_start;
    uintptr_t index_start;
    size_t workspace_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t matrix_bytes;
    size_t output_bytes = 0;
    size_t plan_bytes;
    size_t index_bytes;
    int bump_basis_active = 0;
    int rv;

    memset(requirements, 0, sizeof(*requirements));
    if(!view || !workspace || ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(matrix_valid(object_to_screen) < 0 || sink_valid(sink) < 0)
        return -1;
    if(sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(checked_add(&requirements->records, 1u) < 0 ||
           pvr_chunk_render_validate_state_record(&record) < 0)
            return -1;

        if(record.record_class == PVR_CHUNK_RECORD_MATERIAL &&
           record.type == PVR_CHUNK_MATERIAL_BUMP)
            bump_basis_active = 1;

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(bump_basis_active && !prepare_vertex) {
                errno = ENOTSUP;
                return -1;
            }

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                             &strip)) > 0) {
                if(preflight_strip(view, plan, &strip,
                                   prepare_vertex != NULL,
                                   requirements) < 0)
                    return -1;
            }
            if(strip_rv < 0)
                return -1;
        }
    }

    if(rv < 0)
        return -1;
    if(workspace_count < requirements->maximum_strip_vertices) {
        errno = ENOSPC;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       requirements->vertices > sink->destination.memory.capacity -
                                sink->emitted_vertices) {
        errno = ENOSPC;
        return -1;
    }

    /* Assembly precedes projection, so reject every overlap that could let a
       workspace write corrupt later stream traversal, the transform, or an
       already-emitted memory destination before the geometry layer sees it. */
    if(range_get(workspace, requirements->maximum_strip_vertices,
                 sizeof(*workspace), &workspace_start, &workspace_bytes) < 0 ||
       range_get(view->model.vertex_words, view->model.vertex_word_count,
                 sizeof(*view->model.vertex_words), &vertex_start,
                 &vertex_bytes) < 0 ||
       range_get(view->model.polygon_words, view->model.polygon_word_count,
                 sizeof(*view->model.polygon_words), &polygon_start,
                 &polygon_bytes) < 0 ||
       range_get(object_to_screen, 1u, sizeof(*object_to_screen),
                 &matrix_start, &matrix_bytes) < 0 ||
       model_plan_ranges_get(plan, &plan_start, &plan_bytes,
                             &index_start, &index_bytes) < 0)
        return -1;

    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       range_get(sink->destination.memory.vertices,
                 sink->destination.memory.capacity,
                 sizeof(*sink->destination.memory.vertices), &output_start,
                 &output_bytes) < 0)
        return -1;

    if(ranges_overlap(workspace_start, workspace_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      output_start, output_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      index_start, index_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      index_start, index_bytes)) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static uint32_t payload_u32(const uint16_t *payload) {
    return (uint32_t)payload[0] | ((uint32_t)payload[1] << 16);
}

static float signed_normal16(uint16_t value) {
    int32_t component = value & UINT16_C(0x8000) ?
                        (int32_t)value - INT32_C(0x10000) :
                        (int32_t)value;

    /* Keep the extra negative two's-complement code inside the normalized
       domain, matching the model vertex normal decoder. */
    if(component == INT16_MIN)
        return -1.0f;
    return (float)component / (float)INT16_MAX;
}

static void update_blend(pvr_chunk_render_state_t *state, uint8_t flags) {
    state->blend_source = (pvr_blend_mode_t)((flags >> 3) & 7u);
    state->blend_destination = (pvr_blend_mode_t)(flags & 7u);
    state->present |= PVR_CHUNK_RENDER_BLEND;
}

static void update_material(pvr_chunk_render_state_t *state,
                            const pvr_chunk_record_t *record) {
    const uint16_t *payload = record->payload;
    uint32_t values[3] = { 0, 0, 0 };
    size_t value_count = record->payload_word_count / 2u;
    size_t i;
    size_t value = 0;
    uint8_t type = record->type;
    int secondary = type >= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME;
    int diffuse;
    int ambient;
    int specular;
    uint32_t *present;
    uint8_t *exponent;
    uint32_t *diffuse_argb;
    uint32_t *ambient_argb;
    uint32_t *specular_argb;

    if(record->type == PVR_CHUNK_MATERIAL_BUMP) {
        update_blend(state, record->flags);
        state->bump_direction.x = signed_normal16(payload[0]);
        state->bump_direction.y = signed_normal16(payload[1]);
        state->bump_direction.z = signed_normal16(payload[2]);
        state->bump_direction.w = 0.0f;
        state->bump_up.x = signed_normal16(payload[3]);
        state->bump_up.y = signed_normal16(payload[4]);
        state->bump_up.z = signed_normal16(payload[5]);
        state->bump_up.w = 0.0f;
        state->present |= PVR_CHUNK_RENDER_BUMP_BASIS;
        return;
    }

    if(secondary)
        type -= PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME -
                PVR_CHUNK_MATERIAL_DIFFUSE;

    diffuse = type == PVR_CHUNK_MATERIAL_DIFFUSE ||
              type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT ||
              type == PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR ||
              type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;
    ambient = type == PVR_CHUNK_MATERIAL_AMBIENT ||
              type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT ||
              type == PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR ||
              type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;
    specular = type == PVR_CHUNK_MATERIAL_SPECULAR ||
               type == PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR ||
               type == PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR ||
               type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;

    if(secondary) {
        present = &state->secondary_present;
        exponent = &state->secondary_specular_exponent;
        diffuse_argb = &state->secondary_diffuse_argb;
        ambient_argb = &state->secondary_ambient_argb;
        specular_argb = &state->secondary_specular_argb;
    }
    else {
        present = &state->present;
        exponent = &state->specular_exponent;
        diffuse_argb = &state->diffuse_argb;
        ambient_argb = &state->ambient_argb;
        specular_argb = &state->specular_argb;
    }

    for(i = 0; i < value_count; ++i)
        values[i] = payload_u32(payload + i * 2u);

    update_blend(state, record->flags);
    if(diffuse) {
        *diffuse_argb = values[value++];
        *present |= PVR_CHUNK_RENDER_DIFFUSE;
    }
    if(ambient) {
        *ambient_argb = UINT32_C(0xff000000) |
                        (values[value++] & UINT32_C(0x00ffffff));
        *present |= PVR_CHUNK_RENDER_AMBIENT;
    }
    if(specular) {
        uint32_t encoded = values[value];

        *exponent = (uint8_t)(encoded >> 24);
        *specular_argb = UINT32_C(0xff000000) |
                         (encoded & UINT32_C(0x00ffffff));
        *present |= PVR_CHUNK_RENDER_SPECULAR |
                    PVR_CHUNK_RENDER_SPECULAR_EXPONENT;
    }
}

void pvr_chunk_render_update_state(pvr_chunk_render_state_t *state,
                                   const pvr_chunk_record_t *record) {
    if(record->record_class == PVR_CHUNK_RECORD_BITS) {
        switch(record->type) {
            case PVR_CHUNK_CONTROL_BLEND:
                update_blend(state, record->flags);
                break;
            case PVR_CHUNK_CONTROL_MIPMAP_ADJUST:
                state->mipmap_adjust = record->flags;
                state->present |= PVR_CHUNK_RENDER_MIPMAP_ADJUST;
                break;
            case PVR_CHUNK_CONTROL_SPECULAR_EXPONENT:
                state->specular_exponent = record->flags;
                state->present |= PVR_CHUNK_RENDER_SPECULAR_EXPONENT;
                break;
            default:
                break;
        }
    }
    else if(record->record_class == PVR_CHUNK_RECORD_TEXTURE) {
        uint16_t encoded = *(const uint16_t *)record->payload;
        pvr_chunk_texture_state_t *texture;
        uint32_t *present;

        if(record->type == PVR_CHUNK_TEXTURE_TWO_VOLUME) {
            texture = &state->secondary_texture;
            present = &state->secondary_present;
        }
        else {
            texture = &state->texture;
            present = &state->present;
        }

        texture->identifier = encoded & UINT16_C(0x1fff);
        texture->filter = (uint8_t)(encoded >> 14);
        texture->supersample = (encoded & UINT16_C(0x2000)) != 0;
        texture->uv_flip = record->flags >> 6;
        texture->uv_clamp = (record->flags >> 4) & 3u;
        texture->mipmap_adjust = record->flags & 15u;
        *present |= PVR_CHUNK_RENDER_TEXTURE;
    }
    else if(record->record_class == PVR_CHUNK_RECORD_MATERIAL)
        update_material(state, record);
}

static int assemble_strip(const pvr_chunk_model_view_t *view,
                          const pvr_chunk_model_plan_t *plan,
                          const pvr_chunk_render_state_t *state,
                          const pvr_chunk_strip_view_t *strip,
                          pvr_vertex_t *workspace,
                          pvr_chunk_render_prepare_vertex_t prepare_vertex,
                          void *data) {
    size_t destination_index;

    for(destination_index = 0; destination_index < strip->vertex_count;
        ++destination_index) {
        size_t source_index = destination_index;
        pvr_chunk_strip_attributes_t strip_attributes;
        pvr_chunk_vertex_attributes_t vertex_attributes;
        pvr_vertex_t vertex;

        if(strip->reversed && destination_index < 2u)
            source_index = 1u - destination_index;
        if(pvr_chunk_strip_attributes_get(strip, source_index,
                                          &strip_attributes) < 0 ||
           pvr_chunk_render_vertex_attributes_get(
               view, plan, strip_attributes.index, &vertex_attributes) < 0)
            return -1;

        memset(&vertex, 0, sizeof(vertex));
        vertex.flags = destination_index + 1u == strip->vertex_count ?
                       PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vertex.x = vertex_attributes.position.x;
        vertex.y = vertex_attributes.position.y;
        vertex.z = vertex_attributes.position.z;
        if(strip_attributes.present & PVR_CHUNK_STRIP_ATTR_UV0) {
            vertex.u = strip_attributes.uv[0][0];
            vertex.v = strip_attributes.uv[0][1];
        }
        if(strip_attributes.present & PVR_CHUNK_STRIP_ATTR_COLOR)
            vertex.argb = strip_attributes.argb;
        else if(vertex_attributes.present &
                PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR)
            vertex.argb = vertex_attributes.diffuse_argb;
        else if(state->present & PVR_CHUNK_RENDER_DIFFUSE)
            vertex.argb = state->diffuse_argb;
        else
            vertex.argb = UINT32_C(0xffffffff);

        if(vertex_attributes.present & PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR)
            vertex.oargb = vertex_attributes.specular_argb;
        else if(state->present & PVR_CHUNK_RENDER_SPECULAR)
            vertex.oargb = state->specular_argb;

        if(prepare_vertex) {
            errno = 0;
            if(prepare_vertex(state, &vertex_attributes, &strip_attributes,
                              &vertex, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }

        vertex.flags = destination_index + 1u == strip->vertex_count ?
                       PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        workspace[destination_index] = vertex;
    }

    return 0;
}

static int model_emit(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    pvr_chunk_render_result_t progress = { 0, 0, 0 };
    render_requirements_t requirements;
    pvr_chunk_render_state_t state;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    if(result)
        *result = progress;
    if(preflight(view, plan, object_to_screen, sink, workspace,
                 workspace_count,
                 begin_strip, prepare_vertex, &requirements) < 0)
        return -1;

    memset(&state, 0, sizeof(state));
    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        ++progress.consumed_records;
        pvr_chunk_render_update_state(&state, &record);

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                goto fail;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                             &strip)) > 0) {
                pvr_geometry_stream_t geometry;

                state.strip_flags = strip.flags;
                if(assemble_strip(view, plan, &state, &strip, workspace,
                                  prepare_vertex, data) < 0)
                    goto fail;

                geometry.vertices = workspace;
                geometry.vertex_count = strip.vertex_count;
                geometry.stride = sizeof(pvr_vertex_t);
                if(pvr_geometry_project(workspace, workspace_count, &geometry,
                                        object_to_screen, NULL) < 0)
                    goto fail;
                if(begin_strip) {
                    errno = 0;
                    if(begin_strip(&state, &strip, data) < 0) {
                        if(!errno)
                            errno = EIO;
                        goto fail;
                    }
                }
                if(pvr_geometry_sink_emit(sink, workspace,
                                          strip.vertex_count) < 0)
                    goto fail;

                ++progress.emitted_strips;
                progress.emitted_vertices += strip.vertex_count;
            }
            if(strip_rv < 0)
                goto fail;
        }
    }

    if(rv < 0)
        goto fail;
    if(result)
        *result = progress;
    return 0;

fail:
    if(!errno)
        errno = EIO;
    if(result)
        *result = progress;
    return -1;
}

int pvr_chunk_model_emit(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    return model_emit(view, NULL, object_to_screen, sink, workspace,
                      workspace_count, begin_strip, prepare_vertex, data,
                      result);
}

int pvr_chunk_model_emit_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    if(!plan) {
        errno = EINVAL;
        return -1;
    }
    return model_emit(&plan->view, plan, object_to_screen, sink, workspace,
                      workspace_count, begin_strip, prepare_vertex, data,
                      result);
}

size_t pvr_chunk_render_two_volume_format_size(
    pvr_geometry_vertex_format_t format) {
    switch(format) {
        case PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR:
            return sizeof(pvr_vertex_pcm_t);
        case PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED:
            return sizeof(pvr_vertex_tpcm_t);
        default:
            return 0;
    }
}

static int two_volume_sink_valid(
    const pvr_geometry_vertex_sink_t *sink, size_t vertex_size) {
    if(!sink || !vertex_size) {
        errno = EINVAL;
        return -1;
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               !sink->destination.memory.capacity ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->emitted_vertices > sink->destination.memory.capacity ||
               sink->destination.memory.capacity > SIZE_MAX / vertex_size ||
               sink->destination.memory.capacity * vertex_size >
               UINTPTR_MAX -
               (uintptr_t)sink->destination.memory.vertices) {
                errno = EINVAL;
                return -1;
            }
            break;
        case PVR_GEOMETRY_SINK_CURRENT_LIST:
            break;
        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            if(sink->destination.list != PVR_LIST_OP_POLY &&
               sink->destination.list != PVR_LIST_TR_POLY &&
               sink->destination.list != PVR_LIST_PT_POLY) {
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

pvr_geometry_vertex_format_t pvr_chunk_render_two_volume_strip_format(
    uint8_t type) {
    return type == PVR_CHUNK_STRIP_TWO_VOLUME ?
           PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR :
           PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED;
}

static int preflight_two_volume(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_geometry_vertex_sink_t *sink,
    const pvr_chunk_two_volume_vertex_t *workspace,
    size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    render_requirements_t *requirements) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    uintptr_t workspace_start;
    uintptr_t vertex_start;
    uintptr_t polygon_start;
    uintptr_t matrix_start;
    uintptr_t output_start = 0;
    uintptr_t plan_start;
    uintptr_t index_start;
    size_t workspace_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t matrix_bytes;
    size_t output_bytes = 0;
    size_t plan_bytes;
    size_t index_bytes;
    size_t vertex_size = sink ?
        pvr_chunk_render_two_volume_format_size(sink->format) : 0;
    int rv;

    memset(requirements, 0, sizeof(*requirements));
    if(!view || !workspace || ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(matrix_valid(object_to_screen) < 0 ||
       two_volume_sink_valid(sink, vertex_size) < 0)
        return -1;
    if(sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(checked_add(&requirements->records, 1u) < 0 ||
           pvr_chunk_render_validate_two_volume_state_record(&record) < 0)
            return -1;

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_render_two_volume_strip_format(record.type) !=
               sink->format) {
                errno = EINVAL;
                return -1;
            }
            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                             &strip)) > 0) {
                if(preflight_strip(view, plan, &strip,
                                   prepare_vertex != NULL,
                                   requirements) < 0)
                    return -1;
            }
            if(strip_rv < 0)
                return -1;
        }
    }

    if(rv < 0)
        return -1;
    if(workspace_count < requirements->maximum_strip_vertices) {
        errno = ENOSPC;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       requirements->vertices > sink->destination.memory.capacity -
                                sink->emitted_vertices) {
        errno = ENOSPC;
        return -1;
    }

    if(range_get(workspace, requirements->maximum_strip_vertices,
                 sizeof(*workspace), &workspace_start, &workspace_bytes) < 0 ||
       range_get(view->model.vertex_words, view->model.vertex_word_count,
                 sizeof(*view->model.vertex_words), &vertex_start,
                 &vertex_bytes) < 0 ||
       range_get(view->model.polygon_words, view->model.polygon_word_count,
                 sizeof(*view->model.polygon_words), &polygon_start,
                 &polygon_bytes) < 0 ||
       range_get(object_to_screen, 1u, sizeof(*object_to_screen),
                 &matrix_start, &matrix_bytes) < 0 ||
       model_plan_ranges_get(plan, &plan_start, &plan_bytes,
                             &index_start, &index_bytes) < 0)
        return -1;

    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       range_get(sink->destination.memory.vertices,
                 sink->destination.memory.capacity, vertex_size,
                 &output_start, &output_bytes) < 0)
        return -1;

    if(ranges_overlap(workspace_start, workspace_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      output_start, output_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      index_start, index_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      index_start, index_bytes)) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static uint32_t vertex_diffuse(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes, int secondary) {
    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR)
        return attributes->diffuse_argb;
    if(secondary &&
       (state->secondary_present & PVR_CHUNK_RENDER_DIFFUSE))
        return state->secondary_diffuse_argb;
    if(!secondary && (state->present & PVR_CHUNK_RENDER_DIFFUSE))
        return state->diffuse_argb;
    return UINT32_C(0xffffffff);
}

static uint32_t vertex_specular(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes, int secondary) {
    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR)
        return attributes->specular_argb;
    if(secondary &&
       (state->secondary_present & PVR_CHUNK_RENDER_SPECULAR))
        return state->secondary_specular_argb;
    if(!secondary && (state->present & PVR_CHUNK_RENDER_SPECULAR))
        return state->specular_argb;
    return 0;
}

static int assemble_two_volume_strip(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_strip_view_t *strip,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *workspace,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data) {
    size_t vertex_size = pvr_chunk_render_two_volume_format_size(format);
    size_t destination_index;

    for(destination_index = 0; destination_index < strip->vertex_count;
        ++destination_index) {
        size_t source_index = destination_index;
        pvr_chunk_strip_attributes_t strip_attributes;
        pvr_chunk_vertex_attributes_t vertex_attributes;
        pvr_chunk_two_volume_vertex_t vertex;
        uint32_t command = destination_index + 1u == strip->vertex_count ?
                           PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

        if(strip->reversed && destination_index < 2u)
            source_index = 1u - destination_index;
        if(pvr_chunk_strip_attributes_get(strip, source_index,
                                          &strip_attributes) < 0 ||
           pvr_chunk_render_vertex_attributes_get(
               view, plan, strip_attributes.index, &vertex_attributes) < 0)
            return -1;

        memset(&vertex, 0, sizeof(vertex));
        if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
            vertex.color.flags = command;
            vertex.color.x = vertex_attributes.position.x;
            vertex.color.y = vertex_attributes.position.y;
            vertex.color.z = vertex_attributes.position.z;
            vertex.color.argb0 = vertex_diffuse(state, &vertex_attributes, 0);
            vertex.color.argb1 = vertex_diffuse(state, &vertex_attributes, 1);
        }
        else {
            vertex.textured.flags = command;
            vertex.textured.x = vertex_attributes.position.x;
            vertex.textured.y = vertex_attributes.position.y;
            vertex.textured.z = vertex_attributes.position.z;
            vertex.textured.u0 = strip_attributes.uv[0][0];
            vertex.textured.v0 = strip_attributes.uv[0][1];
            vertex.textured.argb0 = vertex_diffuse(state, &vertex_attributes,
                                                   0);
            vertex.textured.oargb0 = vertex_specular(state,
                                                     &vertex_attributes, 0);
            vertex.textured.u1 = strip_attributes.uv[1][0];
            vertex.textured.v1 = strip_attributes.uv[1][1];
            vertex.textured.argb1 = vertex_diffuse(state, &vertex_attributes,
                                                   1);
            vertex.textured.oargb1 = vertex_specular(state,
                                                     &vertex_attributes, 1);
        }

        if(prepare_vertex) {
            errno = 0;
            if(prepare_vertex(state, &vertex_attributes, &strip_attributes,
                              format, &vertex, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }

        memcpy(&vertex, &command, sizeof(command));
        memcpy((uint8_t *)workspace + destination_index * vertex_size,
               &vertex, vertex_size);
    }

    return 0;
}

static int model_emit_two_volume(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    pvr_chunk_render_result_t progress = { 0, 0, 0 };
    render_requirements_t requirements;
    pvr_chunk_render_state_t state;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t vertex_size;
    int rv;

    if(result)
        *result = progress;
    if(preflight_two_volume(view, plan, object_to_screen, sink, workspace,
                            workspace_count, begin_strip, prepare_vertex,
                            &requirements) < 0)
        return -1;

    vertex_size = pvr_chunk_render_two_volume_format_size(sink->format);
    memset(&state, 0, sizeof(state));
    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        ++progress.consumed_records;
        pvr_chunk_render_update_state(&state, &record);

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                goto fail;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                             &strip)) > 0) {
                pvr_geometry_vertex_stream_t geometry;

                state.strip_flags = strip.flags;
                if(assemble_two_volume_strip(view, plan, &state, &strip,
                                             sink->format, workspace,
                                             prepare_vertex, data) < 0)
                    goto fail;

                geometry.vertices = workspace;
                geometry.vertex_count = strip.vertex_count;
                geometry.stride = vertex_size;
                geometry.format = sink->format;
                if(pvr_geometry_project_vertices(workspace, workspace_count,
                                                 &geometry,
                                                 object_to_screen, NULL) < 0)
                    goto fail;
                if(begin_strip) {
                    errno = 0;
                    if(begin_strip(&state, &strip, data) < 0) {
                        if(!errno)
                            errno = EIO;
                        goto fail;
                    }
                }
                if(pvr_geometry_vertex_sink_emit(sink, workspace,
                                                 strip.vertex_count) < 0)
                    goto fail;

                ++progress.emitted_strips;
                progress.emitted_vertices += strip.vertex_count;
            }
            if(strip_rv < 0)
                goto fail;
        }
    }

    if(rv < 0)
        goto fail;
    if(result)
        *result = progress;
    return 0;

fail:
    if(!errno)
        errno = EIO;
    if(result)
        *result = progress;
    return -1;
}

int pvr_chunk_model_emit_two_volume(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    return model_emit_two_volume(view, NULL, object_to_screen, sink,
                                 workspace, workspace_count, begin_strip,
                                 prepare_vertex, data, result);
}

int pvr_chunk_model_emit_two_volume_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_render_begin_strip_t begin_strip,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_render_result_t *result) {
    if(!plan) {
        errno = EINVAL;
        return -1;
    }
    return model_emit_two_volume(&plan->view, plan, object_to_screen, sink,
                                 workspace, workspace_count, begin_strip,
                                 prepare_vertex, data, result);
}

static int modifier_sink_valid(const pvr_geometry_vertex_sink_t *sink) {
    if(!sink || sink->format != PVR_GEOMETRY_VERTEX_MODIFIER) {
        errno = EINVAL;
        return -1;
    }

    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               !sink->destination.memory.capacity ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->emitted_vertices > sink->destination.memory.capacity ||
               sink->destination.memory.capacity >
               SIZE_MAX / sizeof(pvr_modifier_vol_t) ||
               sink->destination.memory.capacity *
               sizeof(pvr_modifier_vol_t) >
               UINTPTR_MAX -
               (uintptr_t)sink->destination.memory.vertices) {
                errno = EINVAL;
                return -1;
            }
            break;
        case PVR_GEOMETRY_SINK_CURRENT_LIST:
            break;
        case PVR_GEOMETRY_SINK_BUFFERED_LIST:
            if(sink->destination.list != PVR_LIST_OP_MOD &&
               sink->destination.list != PVR_LIST_TR_MOD) {
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

static int modifier_config_valid(
    const pvr_chunk_modifier_config_t *config,
    const pvr_geometry_vertex_sink_t *sink) {
    if(!config ||
       (config->list != PVR_LIST_OP_MOD &&
        config->list != PVR_LIST_TR_MOD) ||
       config->culling > PVR_CULLING_CW ||
       (config->final_mode != PVR_MODIFIER_INCLUDE_LAST_POLY &&
        config->final_mode != PVR_MODIFIER_EXCLUDE_LAST_POLY) ||
       (sink->kind == PVR_GEOMETRY_SINK_BUFFERED_LIST &&
        sink->destination.list != config->list)) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int volume_triangle_count(const pvr_chunk_record_t *record,
                                 size_t *count) {
    const uint16_t *payload = record->payload;
    size_t total = 0;

    if(record->type == PVR_CHUNK_VOLUME_TRIANGLES)
        total = payload[0] & UINT16_C(0x3fff);
    else if(record->type == PVR_CHUNK_VOLUME_QUADS) {
        size_t quads = payload[0] & UINT16_C(0x3fff);

        if(quads > SIZE_MAX / 2u) {
            errno = ERANGE;
            return -1;
        }
        total = quads * 2u;
    }
    else {
        pvr_chunk_record_t strip_record = *record;
        pvr_chunk_strip_iterator_t iterator;
        pvr_chunk_strip_view_t strip;
        int rv;

        strip_record.type = PVR_CHUNK_STRIP_INDEX;
        strip_record.record_class = PVR_CHUNK_RECORD_STRIP;
        if(pvr_chunk_strip_iterator_init(&iterator, &strip_record) < 0)
            return -1;
        while((rv = pvr_chunk_strip_iterator_next(&iterator, &strip)) > 0) {
            if(checked_add(&total, strip.vertex_count - 2u) < 0)
                return -1;
        }
        if(rv < 0)
            return -1;
    }

    *count = total;
    return 0;
}

static int modifier_preflight(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    const pvr_geometry_vertex_sink_t *sink,
    const pvr_modifier_vol_t *workspace,
    size_t *triangle_count) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    uintptr_t workspace_start;
    uintptr_t vertex_start;
    uintptr_t polygon_start;
    uintptr_t matrix_start;
    uintptr_t output_start = 0;
    uintptr_t plan_start;
    uintptr_t index_start;
    size_t workspace_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t matrix_bytes;
    size_t output_bytes = 0;
    size_t plan_bytes;
    size_t index_bytes;
    int rv;

    *triangle_count = 0;
    if(!view || !workspace || ((uintptr_t)workspace & 31u) ||
       matrix_valid(object_to_screen) < 0 || modifier_sink_valid(sink) < 0 ||
       modifier_config_valid(config, sink) < 0)
        return -1;
    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        size_t triangles;

        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        if(volume_triangle_count(&record, &triangles) < 0 ||
           checked_add(triangle_count, triangles) < 0)
            return -1;
    }
    if(rv < 0)
        return -1;
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       *triangle_count > sink->destination.memory.capacity -
                         sink->emitted_vertices) {
        errno = ENOSPC;
        return -1;
    }

    if(range_get(workspace, 1u, sizeof(*workspace),
                 &workspace_start, &workspace_bytes) < 0 ||
       range_get(view->model.vertex_words, view->model.vertex_word_count,
                 sizeof(*view->model.vertex_words), &vertex_start,
                 &vertex_bytes) < 0 ||
       range_get(view->model.polygon_words, view->model.polygon_word_count,
                 sizeof(*view->model.polygon_words), &polygon_start,
                 &polygon_bytes) < 0 ||
       range_get(object_to_screen, 1u, sizeof(*object_to_screen),
                 &matrix_start, &matrix_bytes) < 0 ||
       model_plan_ranges_get(plan, &plan_start, &plan_bytes,
                             &index_start, &index_bytes) < 0)
        return -1;
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       range_get(sink->destination.memory.vertices,
                 sink->destination.memory.capacity,
                 sizeof(pvr_modifier_vol_t), &output_start,
                 &output_bytes) < 0)
        return -1;
    if(ranges_overlap(workspace_start, workspace_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      output_start, output_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(workspace_start, workspace_bytes,
                      index_start, index_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      matrix_start, matrix_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      plan_start, plan_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      index_start, index_bytes)) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

typedef struct modifier_packet {
    alignas(32) pvr_mod_hdr_t header;
    pvr_modifier_vol_t triangle;
} modifier_packet_t;

static int publish_modifier(
    pvr_geometry_vertex_sink_t *sink,
    const pvr_chunk_modifier_config_t *config,
    const pvr_modifier_vol_t *triangle, uint32_t mode) {
    modifier_packet_t packet;
    int saved_errno;
    int rv;

    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY)
        return pvr_geometry_vertex_sink_emit(sink, triangle, 1u);

    pvr_mod_compile(&packet.header, config->list, mode, config->culling);
    packet.triangle = *triangle;
#ifdef __DREAMCAST__
    if(!pvr_state.scene_active || pvr_state.list_reg_open != config->list) {
        errno = EPERM;
        return -1;
    }
#endif
    saved_errno = errno;
    errno = 0;
    if(sink->kind == PVR_GEOMETRY_SINK_CURRENT_LIST)
        rv = pvr_prim(&packet, sizeof(packet));
    else
        rv = pvr_list_prim(config->list, &packet, sizeof(packet));
    if(rv < 0) {
        if(!errno)
            errno = sink->kind == PVR_GEOMETRY_SINK_CURRENT_LIST ?
                    EPERM : EIO;
        return -1;
    }
    errno = saved_errno;
    ++sink->emitted_vertices;
    return 0;
}

static int emit_modifier_triangle(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace, const uint16_t indices[3],
    const uint16_t *user_words, size_t user_word_count,
    uint32_t mode, pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data) {
    pvr_chunk_vertex_attributes_t vertices[3];
    pvr_geometry_vertex_stream_t stream;
    size_t i;

    for(i = 0; i < 3u; ++i) {
        if(pvr_chunk_render_vertex_attributes_get(
               view, plan, indices[i], &vertices[i]) < 0)
            return -1;
        if(vertices[i].position.w != 1.0f && !prepare_triangle) {
            errno = ENOTSUP;
            return -1;
        }
    }

    memset(workspace, 0, sizeof(*workspace));
    workspace->flags = PVR_CMD_VERTEX_EOL;
    workspace->ax = vertices[0].position.x;
    workspace->ay = vertices[0].position.y;
    workspace->az = vertices[0].position.z;
    workspace->bx = vertices[1].position.x;
    workspace->by = vertices[1].position.y;
    workspace->bz = vertices[1].position.z;
    workspace->cx = vertices[2].position.x;
    workspace->cy = vertices[2].position.y;
    workspace->cz = vertices[2].position.z;
    if(prepare_triangle) {
        errno = 0;
        if(prepare_triangle(vertices, user_words, user_word_count,
                            workspace, data) < 0) {
            if(!errno)
                errno = EIO;
            return -1;
        }
        workspace->flags = PVR_CMD_VERTEX_EOL;
    }

    stream.vertices = workspace;
    stream.vertex_count = 1u;
    stream.stride = sizeof(*workspace);
    stream.format = PVR_GEOMETRY_VERTEX_MODIFIER;
    if(pvr_geometry_project_vertices(workspace, 1u, &stream,
                                     object_to_screen, NULL) < 0)
        return -1;
    return publish_modifier(sink, config, workspace, mode);
}

static int emit_modifier_record(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace, const pvr_chunk_record_t *record,
    size_t triangle_count,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data) {
    size_t emitted = 0;

    if(record->type == PVR_CHUNK_VOLUME_TRIANGLES ||
       record->type == PVR_CHUNK_VOLUME_QUADS) {
        const uint16_t *payload = record->payload;
        size_t user_count = payload[0] >> 14;
        size_t primitive_count = payload[0] & UINT16_C(0x3fff);
        size_t index_count = record->type == PVR_CHUNK_VOLUME_TRIANGLES ?
                             3u : 4u;
        size_t primitive;

        ++payload;
        for(primitive = 0; primitive < primitive_count; ++primitive) {
            uint16_t first[3] = { payload[0], payload[1], payload[2] };
            uint32_t mode = ++emitted == triangle_count ?
                            config->final_mode : PVR_MODIFIER_OTHER_POLY;

            if(emit_modifier_triangle(view, plan, object_to_screen, config,
                                      sink,
                                      workspace, first,
                                      payload + index_count, user_count, mode,
                                      prepare_triangle, data) < 0)
                return -1;
            if(index_count == 4u) {
                uint16_t second[3] = { payload[2], payload[1], payload[3] };

                mode = ++emitted == triangle_count ?
                       config->final_mode : PVR_MODIFIER_OTHER_POLY;
                if(emit_modifier_triangle(view, plan, object_to_screen,
                                          config, sink, workspace, second,
                                          payload + index_count, user_count,
                                          mode, prepare_triangle, data) < 0)
                    return -1;
            }
            payload += index_count + user_count;
        }
    }
    else {
        pvr_chunk_record_t strip_record = *record;
        pvr_chunk_strip_iterator_t iterator;
        pvr_chunk_strip_view_t strip;
        int rv;

        strip_record.type = PVR_CHUNK_STRIP_INDEX;
        strip_record.record_class = PVR_CHUNK_RECORD_STRIP;
        if(pvr_chunk_strip_iterator_init(&iterator, &strip_record) < 0)
            return -1;
        while((rv = pvr_chunk_strip_iterator_next(&iterator, &strip)) > 0) {
            size_t triangle;

            for(triangle = 0; triangle + 2u < strip.vertex_count;
                ++triangle) {
                pvr_chunk_strip_vertex_view_t a;
                pvr_chunk_strip_vertex_view_t b;
                pvr_chunk_strip_vertex_view_t c;
                uint16_t indices[3];
                uint32_t mode;

                if(pvr_chunk_strip_vertex_get(&strip, triangle, &a) < 0 ||
                   pvr_chunk_strip_vertex_get(&strip, triangle + 1u, &b) < 0 ||
                   pvr_chunk_strip_vertex_get(&strip, triangle + 2u, &c) < 0)
                    return -1;
                indices[0] = a.index;
                indices[1] = b.index;
                indices[2] = c.index;
                if((triangle & 1u) != 0u)
                    indices[0] = b.index, indices[1] = a.index;
                if(strip.reversed) {
                    uint16_t swap = indices[0];

                    indices[0] = indices[1];
                    indices[1] = swap;
                }
                mode = ++emitted == triangle_count ?
                       config->final_mode : PVR_MODIFIER_OTHER_POLY;
                if(emit_modifier_triangle(view, plan, object_to_screen,
                                          config, sink, workspace, indices,
                                          c.triangle_user_words,
                                          c.triangle_user_word_count, mode,
                                          prepare_triangle, data) < 0)
                    return -1;
            }
        }
        if(rv < 0)
            return -1;
    }

    return 0;
}

static int model_emit_modifiers(
    const pvr_chunk_model_view_t *view,
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result) {
    pvr_chunk_modifier_result_t progress = { 0, 0, 0 };
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t triangle_count;
    int rv;

    if(result)
        *result = progress;
    if(modifier_preflight(view, plan, object_to_screen, config, sink, workspace,
                          &triangle_count) < 0)
        return -1;

    if(pvr_chunk_polygon_iterator_init(&iterator, view->model.polygon_words,
                                       view->model.polygon_word_count) < 0)
        return -1;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        size_t record_triangles;

        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        ++progress.consumed_records;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        if(volume_triangle_count(&record, &record_triangles) < 0)
            goto fail;
        if(!record_triangles)
            continue;
        if(emit_modifier_record(view, plan, object_to_screen, config, sink,
                                workspace, &record, record_triangles,
                                prepare_triangle, data) < 0)
            goto fail;
        ++progress.emitted_volumes;
        progress.emitted_triangles += record_triangles;
    }
    if(rv < 0)
        goto fail;
    if(result)
        *result = progress;
    return 0;

fail:
    if(!errno)
        errno = EIO;
    if(result)
        *result = progress;
    return -1;
}

int pvr_chunk_model_emit_modifiers(
    const pvr_chunk_model_view_t *view,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result) {
    return model_emit_modifiers(view, NULL, object_to_screen, config, sink,
                                workspace, prepare_triangle, data, result);
}

int pvr_chunk_model_emit_modifiers_prepared(
    const pvr_chunk_model_plan_t *plan,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink,
    pvr_modifier_vol_t *workspace,
    pvr_chunk_render_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_result_t *result) {
    if(!plan) {
        errno = EINVAL;
        return -1;
    }
    return model_emit_modifiers(&plan->view, plan, object_to_screen, config,
                                sink, workspace, prepare_triangle, data,
                                result);
}
