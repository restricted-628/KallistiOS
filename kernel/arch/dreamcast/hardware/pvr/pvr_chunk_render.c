/* KallistiOS ##version##

   pvr_chunk_render.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_render.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct render_requirements {
    size_t records;
    size_t strips;
    size_t vertices;
    size_t maximum_strip_vertices;
} render_requirements_t;

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
        record->type >= PVR_CHUNK_MATERIAL_BUMP) ||
       (record->record_class == PVR_CHUNK_RECORD_STRIP &&
        record->type >= PVR_CHUNK_STRIP_TWO_VOLUME)) {
        errno = ENOTSUP;
        return -1;
    }

    return 0;
}

static int validate_state_record(const pvr_chunk_record_t *record) {
    if(unsupported_record(record) < 0)
        return -1;

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

        if(record->flags & UINT8_C(0xc0)) {
            errno = EILSEQ;
            return -1;
        }

        switch(record->type) {
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

static int preflight_strip(const pvr_chunk_model_view_t *view,
                           const pvr_chunk_strip_view_t *strip,
                           pvr_chunk_render_prepare_vertex_t prepare_vertex,
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
           pvr_chunk_model_vertex_attributes_get(view,
                                                  strip_attributes.index,
                                                  &vertex_attributes) < 0)
            return -1;

        if(!prepare_vertex &&
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
    size_t workspace_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t matrix_bytes;
    size_t output_bytes = 0;
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
           validate_state_record(&record) < 0)
            return -1;

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                             &strip)) > 0) {
                if(preflight_strip(view, &strip, prepare_vertex,
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
                 &matrix_start, &matrix_bytes) < 0)
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
       ranges_overlap(output_start, output_bytes,
                      vertex_start, vertex_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      polygon_start, polygon_bytes) ||
       ranges_overlap(output_start, output_bytes,
                      matrix_start, matrix_bytes)) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static uint32_t payload_u32(const uint16_t *payload) {
    return (uint32_t)payload[0] | ((uint32_t)payload[1] << 16);
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
    int diffuse = record->type == PVR_CHUNK_MATERIAL_DIFFUSE ||
                  record->type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT ||
                  record->type == PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR ||
                  record->type ==
                      PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;
    int ambient = record->type == PVR_CHUNK_MATERIAL_AMBIENT ||
                  record->type == PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT ||
                  record->type == PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR ||
                  record->type ==
                      PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;
    int specular = record->type == PVR_CHUNK_MATERIAL_SPECULAR ||
                   record->type == PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR ||
                   record->type == PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR ||
                   record->type ==
                       PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;

    for(i = 0; i < value_count; ++i)
        values[i] = payload_u32(payload + i * 2u);

    update_blend(state, record->flags);
    if(diffuse) {
        state->diffuse_argb = values[value++];
        state->present |= PVR_CHUNK_RENDER_DIFFUSE;
    }
    if(ambient) {
        state->ambient_argb = UINT32_C(0xff000000) |
                              (values[value++] & UINT32_C(0x00ffffff));
        state->present |= PVR_CHUNK_RENDER_AMBIENT;
    }
    if(specular) {
        uint32_t encoded = values[value];

        state->specular_exponent = (uint8_t)(encoded >> 24);
        state->specular_argb = UINT32_C(0xff000000) |
                               (encoded & UINT32_C(0x00ffffff));
        state->present |= PVR_CHUNK_RENDER_SPECULAR |
                          PVR_CHUNK_RENDER_SPECULAR_EXPONENT;
    }
}

static void update_state(pvr_chunk_render_state_t *state,
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

        state->texture.identifier = encoded & UINT16_C(0x1fff);
        state->texture.filter = (uint8_t)(encoded >> 14);
        state->texture.supersample = (encoded & UINT16_C(0x2000)) != 0;
        state->texture.uv_flip = record->flags >> 6;
        state->texture.uv_clamp = (record->flags >> 4) & 3u;
        state->texture.mipmap_adjust = record->flags & 15u;
        state->present |= PVR_CHUNK_RENDER_TEXTURE;
    }
    else if(record->record_class == PVR_CHUNK_RECORD_MATERIAL)
        update_material(state, record);
}

static int assemble_strip(const pvr_chunk_model_view_t *view,
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
           pvr_chunk_model_vertex_attributes_get(view,
                                                  strip_attributes.index,
                                                  &vertex_attributes) < 0)
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

int pvr_chunk_model_emit(
    const pvr_chunk_model_view_t *view,
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
    if(preflight(view, object_to_screen, sink, workspace, workspace_count,
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
        update_state(&state, &record);

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
                if(assemble_strip(view, &state, &strip, workspace,
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
