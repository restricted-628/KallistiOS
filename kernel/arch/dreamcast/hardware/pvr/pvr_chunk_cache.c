/* KallistiOS ##version##

   pvr_chunk_cache.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_cache.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "pvr_chunk_render_internal.h"

static int strip_is_two_volume(uint8_t type) {
    return type == PVR_CHUNK_STRIP_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV8_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV10_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME ||
           type == PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME;
}

static int add_size(size_t left, size_t right, size_t *result) {
    if(left > SIZE_MAX - right) {
        errno = ERANGE;
        return -1;
    }
    *result = left + right;
    return 0;
}

static int multiply_size(size_t count, size_t size, size_t *result) {
    if(count > SIZE_MAX / size) {
        errno = ERANGE;
        return -1;
    }
    *result = count * size;
    return 0;
}

static int align_size(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1u;

    if(value > SIZE_MAX - mask) {
        errno = ERANGE;
        return -1;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

static int accumulate(size_t *value, size_t addend) {
    return add_size(*value, addend, value);
}

static int cache_layout_finish(pvr_chunk_cache_requirements_t *result) {
    size_t cursor;
    size_t bytes;

    if(multiply_size(result->strip_count,
                     sizeof(pvr_chunk_cached_strip_t), &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->vertices_offset) < 0 ||
       multiply_size(result->vertex_count, sizeof(pvr_vertex_t), &bytes) < 0 ||
       add_size(result->vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->deform_vertices_offset) < 0 ||
       multiply_size(result->vertex_count,
                     sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       add_size(result->deform_vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, _Alignof(uint16_t),
                  &result->source_indices_offset) < 0 ||
       multiply_size(result->vertex_count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(result->source_indices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT, &result->bytes) < 0)
        return -1;

    result->alignment = PVR_CHUNK_CACHE_ALIGNMENT;
    return 0;
}

int pvr_chunk_model_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_cache_requirements_t *requirements) {
    pvr_chunk_cache_requirements_t result = { 0 };
    pvr_chunk_model_view_t checked;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!plan || !requirements) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&plan->view.model, &checked) < 0 ||
       pvr_chunk_polygon_iterator_init(&iterator,
           checked.model.polygon_words,
           checked.model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(pvr_chunk_render_validate_state_record(&record) < 0)
            return -1;

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(
                       &strip_iterator, &strip)) > 0) {
                size_t index;

                if(accumulate(&result.strip_count, 1u) < 0 ||
                   accumulate(&result.vertex_count,
                              strip.vertex_count) < 0)
                    return -1;
                if(strip.vertex_count > result.maximum_strip_vertices)
                    result.maximum_strip_vertices = strip.vertex_count;

                for(index = 0; index < strip.vertex_count; ++index) {
                    pvr_chunk_strip_attributes_t strip_attributes;
                    pvr_chunk_vertex_attributes_t vertex_attributes;

                    if(pvr_chunk_strip_attributes_get(
                           &strip, index, &strip_attributes) < 0 ||
                       pvr_chunk_render_vertex_attributes_get(
                           &checked, plan, strip_attributes.index,
                           &vertex_attributes) < 0)
                        return -1;
                }
            }
            if(strip_rv < 0)
                return -1;
        }
    }
    if(rv < 0)
        return -1;
    if(result.strip_count != checked.info.strips ||
       result.vertex_count != checked.info.index_references) {
        errno = EILSEQ;
        return -1;
    }
    if(cache_layout_finish(&result) < 0)
        return -1;

    *requirements = result;
    return 0;
}

static int address_range(const void *pointer, size_t bytes,
                         uintptr_t *start, uintptr_t *end) {
    uintptr_t address = (uintptr_t)pointer;

    if(bytes > UINTPTR_MAX - address) {
        errno = ERANGE;
        return -1;
    }
    *start = address;
    *end = address + bytes;
    return 0;
}

static int ranges_overlap(uintptr_t left_start, uintptr_t left_end,
                          uintptr_t right_start, uintptr_t right_end) {
    return left_start < right_end && right_start < left_end;
}

static int reject_storage_overlap(const pvr_chunk_model_plan_t *plan,
                                  const void *storage, size_t storage_bytes,
                                  const void *cache, size_t cache_bytes) {
    uintptr_t storage_start;
    uintptr_t storage_end;
    uintptr_t other_start;
    uintptr_t other_end;
    size_t bytes;

    if(!storage_bytes)
        return 0;
    if(address_range(storage, storage_bytes,
                     &storage_start, &storage_end) < 0)
        return -1;

#define REJECT_RANGE(pointer, count, type) do {                              \
    if(multiply_size((count), sizeof(type), &bytes) < 0 ||                    \
       address_range((pointer), bytes, &other_start, &other_end) < 0)         \
        return -1;                                                            \
    if(bytes && ranges_overlap(storage_start, storage_end,                    \
                               other_start, other_end)) {                     \
        errno = EINVAL;                                                       \
        return -1;                                                            \
    }                                                                         \
} while(0)

    REJECT_RANGE(plan->vertex_index, plan->vertex_index_count,
                 pvr_chunk_vertex_index_entry_t);
    REJECT_RANGE(plan->view.model.vertex_words,
                 plan->view.model.vertex_word_count, uint32_t);
    REJECT_RANGE(plan->view.model.polygon_words,
                 plan->view.model.polygon_word_count, uint16_t);
    REJECT_RANGE(plan, 1u, pvr_chunk_model_plan_t);
    if(address_range(cache, cache_bytes, &other_start, &other_end) < 0)
        return -1;
    if(cache_bytes && ranges_overlap(storage_start, storage_end,
                                     other_start, other_end)) {
        errno = EINVAL;
        return -1;
    }

#undef REJECT_RANGE
    return 0;
}

static void base_deformation(
    const pvr_chunk_vertex_attributes_t *attributes,
    pvr_deform_vertex_t *deformation) {
    deformation->position = attributes->position;
    deformation->position.w = 1.0f;
    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_NORMAL)
        deformation->normal = attributes->normal;
    else {
        deformation->normal.x = 0.0f;
        deformation->normal.y = 0.0f;
        deformation->normal.z = 1.0f;
        deformation->normal.w = 0.0f;
    }
}

static void base_vertex(const pvr_chunk_render_state_t *state,
                        const pvr_chunk_vertex_attributes_t *attributes,
                        const pvr_chunk_strip_attributes_t *strip_attributes,
                        uint32_t command, pvr_vertex_t *vertex) {
    memset(vertex, 0, sizeof(*vertex));
    vertex->flags = command;
    vertex->x = attributes->position.x;
    vertex->y = attributes->position.y;
    vertex->z = attributes->position.z;
    if(strip_attributes->present & PVR_CHUNK_STRIP_ATTR_UV0) {
        vertex->u = strip_attributes->uv[0][0];
        vertex->v = strip_attributes->uv[0][1];
    }
    if(strip_attributes->present & PVR_CHUNK_STRIP_ATTR_COLOR)
        vertex->argb = strip_attributes->argb;
    else if(attributes->present & PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR)
        vertex->argb = attributes->diffuse_argb;
    else if(state->present & PVR_CHUNK_RENDER_DIFFUSE)
        vertex->argb = state->diffuse_argb;
    else
        vertex->argb = UINT32_C(0xffffffff);

    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR)
        vertex->oargb = attributes->specular_argb;
    else if(state->present & PVR_CHUNK_RENDER_SPECULAR)
        vertex->oargb = state->specular_argb;
}

static void cached_strip_bound_include(
    pvr_chunk_cached_strip_t *strip, const point_t *position,
    size_t vertex_index) {
    if(!vertex_index) {
        strip->minimum = *position;
        strip->maximum = *position;
        strip->minimum.w = 1.0f;
        strip->maximum.w = 1.0f;
        return;
    }

    if(position->x < strip->minimum.x)
        strip->minimum.x = position->x;
    if(position->y < strip->minimum.y)
        strip->minimum.y = position->y;
    if(position->z < strip->minimum.z)
        strip->minimum.z = position->z;
    if(position->x > strip->maximum.x)
        strip->maximum.x = position->x;
    if(position->y > strip->maximum.y)
        strip->maximum.y = position->y;
    if(position->z > strip->maximum.z)
        strip->maximum.z = position->z;
}

static int finite_deformation(const pvr_deform_vertex_t *vertex);

int pvr_chunk_model_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_vertex_t prepare_vertex, void *data,
    pvr_chunk_model_cache_t *cache) {
    pvr_chunk_cache_requirements_t requirements;
    pvr_chunk_model_cache_t prepared = { 0 };
    pvr_chunk_cached_strip_t *strips;
    pvr_vertex_t *vertices;
    pvr_deform_vertex_t *deform_vertices;
    uint16_t *source_indices;
    pvr_chunk_render_state_t state;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t strip_index = 0;
    size_t vertex_index = 0;
    int rv;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!plan || !cache || pvr_chunk_model_cache_query(
           plan, &requirements) < 0)
        return -1;
    if(requirements.bytes &&
       (!storage || ((uintptr_t)storage &
                     (PVR_CHUNK_CACHE_ALIGNMENT - 1u)))) {
        errno = EINVAL;
        return -1;
    }
    if(storage_bytes < requirements.bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(reject_storage_overlap(plan, storage, requirements.bytes,
                              cache, sizeof(*cache)) < 0)
        return -1;

    strips = storage;
    vertices = requirements.vertex_count ?
        (pvr_vertex_t *)((uint8_t *)storage + requirements.vertices_offset) :
        NULL;
    deform_vertices = requirements.vertex_count ?
        (pvr_deform_vertex_t *)((uint8_t *)storage +
                               requirements.deform_vertices_offset) : NULL;
    source_indices = requirements.vertex_count ?
        (uint16_t *)((uint8_t *)storage +
                    requirements.source_indices_offset) : NULL;

    memset(&state, 0, sizeof(state));
    if(pvr_chunk_polygon_iterator_init(&iterator,
           plan->view.model.polygon_words,
           plan->view.model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        pvr_chunk_render_update_state(&state, &record);

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(
                       &strip_iterator, &strip)) > 0) {
                pvr_chunk_cached_strip_t *cached = strips + strip_index;
                size_t destination_index;

                memset(cached, 0, sizeof(*cached));
                cached->state = state;
                cached->state.strip_flags = strip.flags;
                cached->first_vertex = vertex_index;
                cached->vertex_count = strip.vertex_count;
                cached->source_type = strip.type;
                cached->source_flags = strip.flags;

                for(destination_index = 0;
                    destination_index < strip.vertex_count;
                    ++destination_index, ++vertex_index) {
                    size_t source_index = destination_index;
                    pvr_chunk_strip_attributes_t strip_attributes;
                    pvr_chunk_vertex_attributes_t vertex_attributes;
                    uint32_t command =
                        destination_index + 1u == strip.vertex_count ?
                        PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

                    if(strip.reversed && destination_index < 2u)
                        source_index = 1u - destination_index;
                    if(pvr_chunk_strip_attributes_get(
                           &strip, source_index, &strip_attributes) < 0 ||
                       pvr_chunk_render_vertex_attributes_get(
                           &plan->view, plan, strip_attributes.index,
                           &vertex_attributes) < 0)
                        return -1;
                    if(!prepare_vertex &&
                       ((vertex_attributes.present &
                         (PVR_CHUNK_VERTEX_ATTR_DIFFUSE_INTENSITY |
                          PVR_CHUNK_VERTEX_ATTR_SPECULAR_INTENSITY)) ||
                        vertex_attributes.position.w != 1.0f ||
                        (state.present & PVR_CHUNK_RENDER_BUMP_BASIS))) {
                        errno = ENOTSUP;
                        return -1;
                    }

                    base_vertex(&state, &vertex_attributes,
                                &strip_attributes, command,
                                vertices + vertex_index);
                    base_deformation(&vertex_attributes,
                                     deform_vertices + vertex_index);
                    source_indices[vertex_index] = strip_attributes.index;
                    if(prepare_vertex) {
                        errno = 0;
                        if(prepare_vertex(&cached->state, &vertex_attributes,
                                          &strip_attributes,
                                          vertices + vertex_index,
                                          data) < 0) {
                            if(!errno)
                                errno = EIO;
                            return -1;
                        }
                        vertices[vertex_index].flags = command;
                        deform_vertices[vertex_index].position.x =
                            vertices[vertex_index].x;
                        deform_vertices[vertex_index].position.y =
                            vertices[vertex_index].y;
                        deform_vertices[vertex_index].position.z =
                            vertices[vertex_index].z;
                    }
                    if(finite_deformation(
                           deform_vertices + vertex_index) < 0)
                        return -1;
                    cached_strip_bound_include(
                        cached, &deform_vertices[vertex_index].position,
                        destination_index);
                }
                ++strip_index;
            }
            if(strip_rv < 0)
                return -1;
        }
    }
    if(rv < 0 || strip_index != requirements.strip_count ||
       vertex_index != requirements.vertex_count) {
        if(rv >= 0)
            errno = EILSEQ;
        return -1;
    }

    prepared.version = PVR_CHUNK_CACHE_VERSION;
    prepared.storage = storage;
    prepared.storage_bytes = requirements.bytes;
    prepared.strips = strips;
    prepared.strip_count = requirements.strip_count;
    prepared.vertices = vertices;
    prepared.deform_vertices = deform_vertices;
    prepared.source_indices = source_indices;
    prepared.vertex_count = requirements.vertex_count;
    prepared.maximum_strip_vertices = requirements.maximum_strip_vertices;
    memcpy(prepared.center, plan->view.model.center,
           sizeof(prepared.center));
    prepared.radius = plan->view.model.radius;
    *cache = prepared;
    return 0;
}

static int finite_deformation(const pvr_deform_vertex_t *vertex) {
    const float *values = (const float *)vertex;
    size_t index;

    for(index = 0; index < sizeof(*vertex) / sizeof(*values); ++index) {
        if(!isfinite(values[index])) {
            errno = EDOM;
            return -1;
        }
    }
    if(vertex->position.w != 1.0f || vertex->normal.w != 0.0f) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int matrix_valid(const matrix_t *matrix) {
    const float *values = (const float *)matrix;
    size_t index;

    if(!matrix || ((uintptr_t)matrix & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < 16u; ++index) {
        if(!isfinite(values[index])) {
            errno = EDOM;
            return -1;
        }
    }
    return 0;
}

static int sink_valid(const pvr_geometry_sink_t *sink,
                      size_t required_vertices) {
    if(!sink || sink->emitted_vertices > SIZE_MAX - required_vertices) {
        errno = EINVAL;
        return -1;
    }
    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->emitted_vertices > sink->destination.memory.capacity) {
                errno = EINVAL;
                return -1;
            }
            if(required_vertices > sink->destination.memory.capacity -
                                   sink->emitted_vertices) {
                errno = ENOSPC;
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

static int cached_strip_bound_values_valid(
    const pvr_chunk_cached_strip_t *strip) {
    const float *minimum = (const float *)&strip->minimum;
    const float *maximum = (const float *)&strip->maximum;
    size_t component;

    /* Build includes every retained reference vertex before publishing the
       immutable cache. Rechecking extrema here keeps emission O(strips)
       instead of defeating the culling path with an O(vertices) scan. */
    for(component = 0; component < 4u; ++component) {
        if(!isfinite(minimum[component]) ||
           !isfinite(maximum[component])) {
            errno = EILSEQ;
            return -1;
        }
    }
    if(strip->minimum.w != 1.0f || strip->maximum.w != 1.0f ||
       strip->minimum.x > strip->maximum.x ||
       strip->minimum.y > strip->maximum.y ||
       strip->minimum.z > strip->maximum.z) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_cached_strip_classify(
    const pvr_chunk_cached_strip_t *strip, const pvr_frustum_t *frustum,
    pvr_frustum_classification_t *result) {
    if(!strip || !frustum || !result) {
        errno = EINVAL;
        return -1;
    }
    if(cached_strip_bound_values_valid(strip) < 0)
        return -1;
    return pvr_frustum_classify_aabb(frustum, &strip->minimum,
                                     &strip->maximum, result);
}

static int cache_valid(const pvr_chunk_model_cache_t *cache) {
    pvr_chunk_cache_requirements_t layout = { 0 };
    uintptr_t storage_start;
    uintptr_t storage_end;
    uintptr_t range_start;
    uintptr_t range_end;
    size_t bytes;
    size_t vertex_cursor = 0;
    size_t maximum = 0;
    size_t index;

    if(!cache || cache->version != PVR_CHUNK_CACHE_VERSION ||
       (cache->storage_bytes &&
        (!cache->storage || ((uintptr_t)cache->storage & 31u))) ||
       !isfinite(cache->center[0]) || !isfinite(cache->center[1]) ||
       !isfinite(cache->center[2]) || !isfinite(cache->radius) ||
       cache->radius < 0.0f) {
        errno = EINVAL;
        return -1;
    }
    layout.strip_count = cache->strip_count;
    layout.vertex_count = cache->vertex_count;
    layout.maximum_strip_vertices = cache->maximum_strip_vertices;
    if(cache_layout_finish(&layout) < 0)
        return -1;
    if(cache->storage_bytes != layout.bytes ||
       (layout.strip_count &&
        cache->strips != (const pvr_chunk_cached_strip_t *)cache->storage) ||
       (layout.vertex_count &&
        (cache->vertices != (const pvr_vertex_t *)
             ((const uint8_t *)cache->storage + layout.vertices_offset) ||
         cache->deform_vertices != (const pvr_deform_vertex_t *)
             ((const uint8_t *)cache->storage +
              layout.deform_vertices_offset) ||
         cache->source_indices != (const uint16_t *)
             ((const uint8_t *)cache->storage +
              layout.source_indices_offset)))) {
        errno = EILSEQ;
        return -1;
    }
    if(address_range(cache->storage, cache->storage_bytes,
                     &storage_start, &storage_end) < 0)
        return -1;

#define REQUIRE_STORED(pointer, count, type, alignment) do {                  \
    if((count) && (!(pointer) ||                                               \
       ((uintptr_t)(pointer) & ((alignment) - 1u)))) {                         \
        errno = EILSEQ;                                                        \
        return -1;                                                             \
    }                                                                          \
    if(multiply_size((count), sizeof(type), &bytes) < 0 ||                     \
       address_range((pointer), bytes, &range_start, &range_end) < 0)          \
        return -1;                                                             \
    if(bytes && (range_start < storage_start || range_end > storage_end)) {    \
        errno = EILSEQ;                                                        \
        return -1;                                                             \
    }                                                                          \
} while(0)

    REQUIRE_STORED(cache->strips, cache->strip_count,
                   pvr_chunk_cached_strip_t,
                   _Alignof(pvr_chunk_cached_strip_t));
    REQUIRE_STORED(cache->vertices, cache->vertex_count,
                   pvr_vertex_t, 32u);
    REQUIRE_STORED(cache->deform_vertices, cache->vertex_count,
                   pvr_deform_vertex_t, 32u);
    REQUIRE_STORED(cache->source_indices, cache->vertex_count,
                   uint16_t, _Alignof(uint16_t));

#undef REQUIRE_STORED

    for(index = 0; index < cache->strip_count; ++index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + index;

        if(strip->reserved || strip->first_vertex != vertex_cursor ||
           strip->vertex_count < 3u ||
           strip->source_type < PVR_CHUNK_STRIP_INDEX ||
           strip->source_type > PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME ||
           strip_is_two_volume(strip->source_type) ||
           strip->source_flags & UINT8_C(0x80) ||
           strip->state.strip_flags != strip->source_flags ||
           accumulate(&vertex_cursor, strip->vertex_count) < 0 ||
           vertex_cursor > cache->vertex_count) {
            if(!errno)
                errno = EILSEQ;
            return -1;
        }
        if(cached_strip_bound_values_valid(strip) < 0)
            return -1;
        if(strip->vertex_count > maximum)
            maximum = strip->vertex_count;
    }
    if(vertex_cursor != cache->vertex_count ||
       maximum != cache->maximum_strip_vertices) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_cache_validate(const pvr_chunk_model_cache_t *cache) {
    return cache_valid(cache);
}

static int emit_preflight(const pvr_chunk_model_cache_t *cache,
                          const matrix_t *matrix,
                          const pvr_geometry_sink_t *sink,
                          const pvr_vertex_t *workspace,
                          size_t workspace_count,
                          pvr_chunk_cache_begin_strip_t begin_strip) {
    uintptr_t cache_start;
    uintptr_t cache_end;
    uintptr_t descriptor_start;
    uintptr_t descriptor_end;
    uintptr_t workspace_start;
    uintptr_t workspace_end;
    uintptr_t matrix_start;
    uintptr_t matrix_end;
    uintptr_t output_start = 0;
    uintptr_t output_end = 0;
    size_t bytes;

    if(cache_valid(cache) < 0 || matrix_valid(matrix) < 0 ||
       sink_valid(sink, cache->vertex_count) < 0)
        return -1;
    if(cache->vertex_count &&
       (!workspace || ((uintptr_t)workspace & 31u))) {
        errno = EINVAL;
        return -1;
    }
    if(workspace_count < cache->maximum_strip_vertices) {
        errno = ENOSPC;
        return -1;
    }
    if(sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip) {
        errno = EINVAL;
        return -1;
    }
    if(address_range(cache->storage, cache->storage_bytes,
                     &cache_start, &cache_end) < 0 ||
       address_range(cache, sizeof(*cache),
                     &descriptor_start, &descriptor_end) < 0 ||
       multiply_size(workspace_count, sizeof(*workspace), &bytes) < 0 ||
       address_range(workspace, bytes,
                     &workspace_start, &workspace_end) < 0 ||
       address_range(matrix, sizeof(*matrix),
                     &matrix_start, &matrix_end) < 0)
        return -1;
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY) {
        if(multiply_size(sink->destination.memory.capacity,
                         sizeof(pvr_vertex_t), &bytes) < 0 ||
           address_range(sink->destination.memory.vertices, bytes,
                         &output_start, &output_end) < 0)
            return -1;
    }
    if(ranges_overlap(cache_start, cache_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(cache_start, cache_end, matrix_start, matrix_end) ||
       ranges_overlap(cache_start, cache_end, output_start, output_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      output_start, output_end)) {
        errno = EINVAL;
        return -1;
    }
    if(ranges_overlap(descriptor_start, descriptor_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      output_start, output_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(output_start, output_end,
                      matrix_start, matrix_end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_cache_emit_filtered(
    const pvr_chunk_model_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result) {
    pvr_chunk_cache_result_t progress = { 0 };
    size_t strip_index;

    if(result)
        *result = progress;
    if(emit_preflight(cache, object_to_screen, sink, workspace,
                      workspace_count, begin_strip) < 0)
        return -1;

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_geometry_stream_t stream;
        size_t index;

        if(filter_strip) {
            int filter_result;

            errno = 0;
            filter_result = filter_strip(strip, data);
            if(filter_result < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
            if(!filter_result) {
                ++progress.skipped_strips;
                progress.skipped_vertices += strip->vertex_count;
                continue;
            }
        }

        for(index = 0; index < strip->vertex_count; ++index) {
            size_t cached_index = strip->first_vertex + index;
            uint16_t source_index = cache->source_indices[cached_index];
            pvr_deform_vertex_t deformation =
                cache->deform_vertices[cached_index];
            uint32_t command = index + 1u == strip->vertex_count ?
                               PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

            if(resolve_vertex) {
                errno = 0;
                if(resolve_vertex(source_index, &deformation, data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            if(finite_deformation(&deformation) < 0)
                goto fail;

            workspace[index] = cache->vertices[cached_index];
            workspace[index].x = deformation.position.x;
            workspace[index].y = deformation.position.y;
            workspace[index].z = deformation.position.z;
            if(prepare_vertex) {
                errno = 0;
                if(prepare_vertex(&strip->state, source_index,
                                  &deformation, workspace + index,
                                  data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            workspace[index].flags = command;
        }

        stream.vertices = workspace;
        stream.vertex_count = strip->vertex_count;
        stream.stride = sizeof(*workspace);
        if(pvr_geometry_project(workspace, workspace_count, &stream,
                                object_to_screen, NULL) < 0)
            goto fail;
        if(begin_strip) {
            errno = 0;
            if(begin_strip(strip, data) < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
        }
        if(pvr_geometry_sink_emit(sink, workspace,
                                  strip->vertex_count) < 0)
            goto fail;

        ++progress.emitted_strips;
        progress.emitted_vertices += strip->vertex_count;
    }

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

int pvr_chunk_model_cache_emit(
    const pvr_chunk_model_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_sink_t *sink,
    pvr_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result) {
    return pvr_chunk_model_cache_emit_filtered(
        cache, object_to_screen, sink, workspace, workspace_count, NULL,
        begin_strip, resolve_vertex, prepare_vertex, data, result);
}

static int two_volume_cache_layout_finish(
    pvr_chunk_two_volume_cache_requirements_t *result) {
    size_t cursor;
    size_t bytes;

    result->vertex_size = pvr_chunk_render_two_volume_format_size(
        result->format);
    if(!result->vertex_size) {
        errno = EINVAL;
        return -1;
    }
    if(multiply_size(result->strip_count,
                     sizeof(pvr_chunk_cached_strip_t), &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->vertices_offset) < 0 ||
       multiply_size(result->vertex_count, result->vertex_size, &bytes) < 0 ||
       add_size(result->vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->deform_vertices_offset) < 0 ||
       multiply_size(result->vertex_count,
                     sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       add_size(result->deform_vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, _Alignof(uint16_t),
                  &result->source_indices_offset) < 0 ||
       multiply_size(result->vertex_count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(result->source_indices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT, &result->bytes) < 0)
        return -1;

    result->alignment = PVR_CHUNK_CACHE_ALIGNMENT;
    return 0;
}

int pvr_chunk_model_two_volume_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_two_volume_cache_requirements_t *requirements) {
    pvr_chunk_two_volume_cache_requirements_t result = { 0 };
    pvr_chunk_model_view_t checked;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!plan || !requirements) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&plan->view.model, &checked) < 0 ||
       pvr_chunk_polygon_iterator_init(&iterator,
           checked.model.polygon_words,
           checked.model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(pvr_chunk_render_validate_two_volume_state_record(&record) < 0)
            return -1;

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            pvr_geometry_vertex_format_t format =
                pvr_chunk_render_two_volume_strip_format(record.type);
            int strip_rv;

            if(result.format && result.format != format) {
                errno = EINVAL;
                return -1;
            }
            result.format = format;
            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(
                       &strip_iterator, &strip)) > 0) {
                size_t index;

                if(accumulate(&result.strip_count, 1u) < 0 ||
                   accumulate(&result.vertex_count,
                              strip.vertex_count) < 0)
                    return -1;
                if(strip.vertex_count > result.maximum_strip_vertices)
                    result.maximum_strip_vertices = strip.vertex_count;

                for(index = 0; index < strip.vertex_count; ++index) {
                    pvr_chunk_strip_attributes_t strip_attributes;
                    pvr_chunk_vertex_attributes_t vertex_attributes;

                    if(pvr_chunk_strip_attributes_get(
                           &strip, index, &strip_attributes) < 0 ||
                       pvr_chunk_render_vertex_attributes_get(
                           &checked, plan, strip_attributes.index,
                           &vertex_attributes) < 0)
                        return -1;
                }
            }
            if(strip_rv < 0)
                return -1;
        }
    }
    if(rv < 0)
        return -1;
    if(!result.strip_count ||
       result.strip_count != checked.info.strips ||
       result.vertex_count != checked.info.index_references) {
        errno = EILSEQ;
        return -1;
    }
    if(two_volume_cache_layout_finish(&result) < 0)
        return -1;

    *requirements = result;
    return 0;
}

static uint32_t two_volume_diffuse(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes, int secondary) {
    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR)
        return attributes->diffuse_argb;
    if(secondary && (state->secondary_present & PVR_CHUNK_RENDER_DIFFUSE))
        return state->secondary_diffuse_argb;
    if(!secondary && (state->present & PVR_CHUNK_RENDER_DIFFUSE))
        return state->diffuse_argb;
    return UINT32_C(0xffffffff);
}

static uint32_t two_volume_specular(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes, int secondary) {
    if(attributes->present & PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR)
        return attributes->specular_argb;
    if(secondary && (state->secondary_present & PVR_CHUNK_RENDER_SPECULAR))
        return state->secondary_specular_argb;
    if(!secondary && (state->present & PVR_CHUNK_RENDER_SPECULAR))
        return state->specular_argb;
    return 0;
}

static void two_volume_base_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_geometry_vertex_format_t format, uint32_t command,
    pvr_chunk_two_volume_vertex_t *vertex) {
    memset(vertex, 0, sizeof(*vertex));
    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
        vertex->color.flags = command;
        vertex->color.x = attributes->position.x;
        vertex->color.y = attributes->position.y;
        vertex->color.z = attributes->position.z;
        vertex->color.argb0 = two_volume_diffuse(state, attributes, 0);
        vertex->color.argb1 = two_volume_diffuse(state, attributes, 1);
    }
    else {
        vertex->textured.flags = command;
        vertex->textured.x = attributes->position.x;
        vertex->textured.y = attributes->position.y;
        vertex->textured.z = attributes->position.z;
        vertex->textured.u0 = strip_attributes->uv[0][0];
        vertex->textured.v0 = strip_attributes->uv[0][1];
        vertex->textured.argb0 = two_volume_diffuse(state, attributes, 0);
        vertex->textured.oargb0 = two_volume_specular(state, attributes, 0);
        vertex->textured.u1 = strip_attributes->uv[1][0];
        vertex->textured.v1 = strip_attributes->uv[1][1];
        vertex->textured.argb1 = two_volume_diffuse(state, attributes, 1);
        vertex->textured.oargb1 = two_volume_specular(state, attributes, 1);
    }
}

int pvr_chunk_model_two_volume_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_two_volume_vertex_t prepare_vertex, void *data,
    pvr_chunk_two_volume_cache_t *cache) {
    pvr_chunk_two_volume_cache_requirements_t requirements;
    pvr_chunk_two_volume_cache_t prepared = { 0 };
    pvr_chunk_cached_strip_t *strips;
    uint8_t *vertices;
    pvr_deform_vertex_t *deform_vertices;
    uint16_t *source_indices;
    pvr_chunk_render_state_t state;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t strip_index = 0;
    size_t vertex_index = 0;
    int rv;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!plan || !cache || pvr_chunk_model_two_volume_cache_query(
           plan, &requirements) < 0)
        return -1;
    if(!storage || ((uintptr_t)storage &
                    (PVR_CHUNK_CACHE_ALIGNMENT - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(storage_bytes < requirements.bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(reject_storage_overlap(plan, storage, requirements.bytes,
                              cache, sizeof(*cache)) < 0)
        return -1;

    strips = storage;
    vertices = (uint8_t *)storage + requirements.vertices_offset;
    deform_vertices = (pvr_deform_vertex_t *)((uint8_t *)storage +
                                              requirements.
                                              deform_vertices_offset);
    source_indices = (uint16_t *)((uint8_t *)storage +
                                  requirements.source_indices_offset);

    memset(&state, 0, sizeof(state));
    if(pvr_chunk_polygon_iterator_init(&iterator,
           plan->view.model.polygon_words,
           plan->view.model.polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        pvr_chunk_render_update_state(&state, &record);

        if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(
                       &strip_iterator, &strip)) > 0) {
                pvr_chunk_cached_strip_t *cached = strips + strip_index;
                size_t destination_index;

                memset(cached, 0, sizeof(*cached));
                cached->state = state;
                cached->state.strip_flags = strip.flags;
                cached->first_vertex = vertex_index;
                cached->vertex_count = strip.vertex_count;
                cached->source_type = strip.type;
                cached->source_flags = strip.flags;

                for(destination_index = 0;
                    destination_index < strip.vertex_count;
                    ++destination_index, ++vertex_index) {
                    size_t reference_index = destination_index;
                    pvr_chunk_strip_attributes_t strip_attributes;
                    pvr_chunk_vertex_attributes_t vertex_attributes;
                    pvr_chunk_two_volume_vertex_t vertex;
                    uint32_t command =
                        destination_index + 1u == strip.vertex_count ?
                        PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

                    if(strip.reversed && destination_index < 2u)
                        reference_index = 1u - destination_index;
                    if(pvr_chunk_strip_attributes_get(
                           &strip, reference_index, &strip_attributes) < 0 ||
                       pvr_chunk_render_vertex_attributes_get(
                           &plan->view, plan, strip_attributes.index,
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

                    two_volume_base_vertex(&cached->state,
                                           &vertex_attributes,
                                           &strip_attributes,
                                           requirements.format, command,
                                           &vertex);
                    base_deformation(&vertex_attributes,
                                     deform_vertices + vertex_index);
                    source_indices[vertex_index] = strip_attributes.index;
                    if(prepare_vertex) {
                        errno = 0;
                        if(prepare_vertex(&cached->state, &vertex_attributes,
                                          &strip_attributes,
                                          requirements.format, &vertex,
                                          data) < 0) {
                            if(!errno)
                                errno = EIO;
                            return -1;
                        }
                        memcpy(&vertex, &command, sizeof(command));
                        deform_vertices[vertex_index].position.x =
                            vertex.color.x;
                        deform_vertices[vertex_index].position.y =
                            vertex.color.y;
                        deform_vertices[vertex_index].position.z =
                            vertex.color.z;
                    }
                    if(finite_deformation(
                           deform_vertices + vertex_index) < 0)
                        return -1;
                    cached_strip_bound_include(
                        cached, &deform_vertices[vertex_index].position,
                        destination_index);
                    memcpy(vertices + vertex_index * requirements.vertex_size,
                           &vertex, requirements.vertex_size);
                }
                ++strip_index;
            }
            if(strip_rv < 0)
                return -1;
        }
    }
    if(rv < 0 || strip_index != requirements.strip_count ||
       vertex_index != requirements.vertex_count) {
        if(rv >= 0)
            errno = EILSEQ;
        return -1;
    }

    prepared.version = PVR_CHUNK_CACHE_VERSION;
    prepared.storage = storage;
    prepared.storage_bytes = requirements.bytes;
    prepared.strips = strips;
    prepared.strip_count = requirements.strip_count;
    prepared.vertices = vertices;
    prepared.deform_vertices = deform_vertices;
    prepared.source_indices = source_indices;
    prepared.vertex_count = requirements.vertex_count;
    prepared.maximum_strip_vertices = requirements.maximum_strip_vertices;
    prepared.format = requirements.format;
    prepared.vertex_size = requirements.vertex_size;
    memcpy(prepared.center, plan->view.model.center,
           sizeof(prepared.center));
    prepared.radius = plan->view.model.radius;
    *cache = prepared;
    return 0;
}

static int two_volume_cache_valid(
    const pvr_chunk_two_volume_cache_t *cache) {
    pvr_chunk_two_volume_cache_requirements_t layout = { 0 };
    uintptr_t storage_start;
    uintptr_t storage_end;
    size_t vertex_cursor = 0;
    size_t maximum = 0;
    size_t index;

    if(!cache || cache->version != PVR_CHUNK_CACHE_VERSION ||
       !cache->storage || ((uintptr_t)cache->storage & 31u) ||
       !isfinite(cache->center[0]) || !isfinite(cache->center[1]) ||
       !isfinite(cache->center[2]) || !isfinite(cache->radius) ||
       cache->radius < 0.0f) {
        errno = EINVAL;
        return -1;
    }
    layout.format = cache->format;
    layout.strip_count = cache->strip_count;
    layout.vertex_count = cache->vertex_count;
    layout.maximum_strip_vertices = cache->maximum_strip_vertices;
    if(two_volume_cache_layout_finish(&layout) < 0)
        return -1;
    if(address_range(cache->storage, cache->storage_bytes,
                     &storage_start, &storage_end) < 0)
        return -1;
    (void)storage_end;
    if(!layout.strip_count || cache->vertex_size != layout.vertex_size ||
       cache->storage_bytes != layout.bytes ||
       (uintptr_t)cache->strips != storage_start ||
       (uintptr_t)cache->vertices != storage_start + layout.vertices_offset ||
       (uintptr_t)cache->deform_vertices !=
           storage_start + layout.deform_vertices_offset ||
       (uintptr_t)cache->source_indices !=
           storage_start + layout.source_indices_offset) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < cache->strip_count; ++index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + index;
        pvr_geometry_vertex_format_t format =
            pvr_chunk_render_two_volume_strip_format(strip->source_type);

        if(strip->reserved || strip->first_vertex != vertex_cursor ||
           strip->vertex_count < 3u ||
           (strip->source_type != PVR_CHUNK_STRIP_TWO_VOLUME &&
            strip->source_type != PVR_CHUNK_STRIP_UV8_TWO_VOLUME &&
            strip->source_type != PVR_CHUNK_STRIP_UV10_TWO_VOLUME &&
            strip->source_type != PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME &&
            strip->source_type != PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME) ||
           format != cache->format ||
           strip->source_flags & UINT8_C(0x80) ||
           strip->state.strip_flags != strip->source_flags ||
           accumulate(&vertex_cursor, strip->vertex_count) < 0 ||
           vertex_cursor > cache->vertex_count) {
            if(!errno)
                errno = EILSEQ;
            return -1;
        }
        if(cached_strip_bound_values_valid(strip) < 0)
            return -1;
        if(strip->vertex_count > maximum)
            maximum = strip->vertex_count;
    }
    if(vertex_cursor != cache->vertex_count ||
       maximum != cache->maximum_strip_vertices) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int two_volume_cache_sink_valid(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_geometry_vertex_sink_t *sink) {
    if(!sink || sink->format != cache->format ||
       sink->emitted_vertices > SIZE_MAX - cache->vertex_count) {
        errno = EINVAL;
        return -1;
    }
    switch(sink->kind) {
        case PVR_GEOMETRY_SINK_MEMORY:
            if(!sink->destination.memory.vertices ||
               ((uintptr_t)sink->destination.memory.vertices & 31u) ||
               sink->emitted_vertices > sink->destination.memory.capacity) {
                errno = EINVAL;
                return -1;
            }
            if(cache->vertex_count >
               sink->destination.memory.capacity - sink->emitted_vertices) {
                errno = ENOSPC;
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

static int two_volume_emit_preflight(
    const pvr_chunk_two_volume_cache_t *cache, const matrix_t *matrix,
    const pvr_geometry_vertex_sink_t *sink,
    const pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_begin_strip_t begin_strip) {
    uintptr_t cache_start;
    uintptr_t cache_end;
    uintptr_t descriptor_start;
    uintptr_t descriptor_end;
    uintptr_t workspace_start;
    uintptr_t workspace_end;
    uintptr_t matrix_start;
    uintptr_t matrix_end;
    uintptr_t output_start = 0;
    uintptr_t output_end = 0;
    size_t bytes;

    if(two_volume_cache_valid(cache) < 0 || matrix_valid(matrix) < 0 ||
       two_volume_cache_sink_valid(cache, sink) < 0)
        return -1;
    if(!workspace || ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(workspace_count < cache->maximum_strip_vertices) {
        errno = ENOSPC;
        return -1;
    }
    if(sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip) {
        errno = EINVAL;
        return -1;
    }
    if(address_range(cache->storage, cache->storage_bytes,
                     &cache_start, &cache_end) < 0 ||
       address_range(cache, sizeof(*cache),
                     &descriptor_start, &descriptor_end) < 0 ||
       multiply_size(workspace_count, sizeof(*workspace), &bytes) < 0 ||
       address_range(workspace, bytes,
                     &workspace_start, &workspace_end) < 0 ||
       address_range(matrix, sizeof(*matrix),
                     &matrix_start, &matrix_end) < 0)
        return -1;
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY) {
        if(multiply_size(sink->destination.memory.capacity,
                         cache->vertex_size, &bytes) < 0 ||
           address_range(sink->destination.memory.vertices, bytes,
                         &output_start, &output_end) < 0)
            return -1;
    }
    if(ranges_overlap(cache_start, cache_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(cache_start, cache_end, matrix_start, matrix_end) ||
       ranges_overlap(cache_start, cache_end, output_start, output_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      output_start, output_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      output_start, output_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(output_start, output_end,
                      matrix_start, matrix_end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_two_volume_cache_emit_filtered(
    const pvr_chunk_two_volume_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result) {
    pvr_chunk_cache_result_t progress = { 0 };
    size_t strip_index;

    if(result)
        *result = progress;
    if(two_volume_emit_preflight(cache, object_to_screen, sink, workspace,
                                 workspace_count, begin_strip) < 0)
        return -1;

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_geometry_vertex_stream_t stream;
        size_t index;

        if(filter_strip) {
            int filter_result;

            errno = 0;
            filter_result = filter_strip(strip, data);
            if(filter_result < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
            if(!filter_result) {
                ++progress.skipped_strips;
                progress.skipped_vertices += strip->vertex_count;
                continue;
            }
        }

        for(index = 0; index < strip->vertex_count; ++index) {
            size_t cached_index = strip->first_vertex + index;
            uint16_t source_index = cache->source_indices[cached_index];
            pvr_deform_vertex_t deformation =
                cache->deform_vertices[cached_index];
            pvr_chunk_two_volume_vertex_t vertex;
            uint32_t command = index + 1u == strip->vertex_count ?
                               PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

            if(resolve_vertex) {
                errno = 0;
                if(resolve_vertex(source_index, &deformation, data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            if(finite_deformation(&deformation) < 0)
                goto fail;

            memset(&vertex, 0, sizeof(vertex));
            memcpy(&vertex,
                   (const uint8_t *)cache->vertices +
                   cached_index * cache->vertex_size,
                   cache->vertex_size);
            vertex.color.x = deformation.position.x;
            vertex.color.y = deformation.position.y;
            vertex.color.z = deformation.position.z;
            if(prepare_vertex) {
                errno = 0;
                if(prepare_vertex(&strip->state, source_index,
                                  &deformation, cache->format,
                                  &vertex, data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            memcpy(&vertex, &command, sizeof(command));
            memcpy((uint8_t *)workspace + index * cache->vertex_size,
                   &vertex, cache->vertex_size);
        }

        stream.vertices = workspace;
        stream.vertex_count = strip->vertex_count;
        stream.stride = cache->vertex_size;
        stream.format = cache->format;
        if(pvr_geometry_project_vertices(workspace, workspace_count, &stream,
                                         object_to_screen, NULL) < 0)
            goto fail;
        if(begin_strip) {
            errno = 0;
            if(begin_strip(strip, data) < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
        }
        if(pvr_geometry_vertex_sink_emit(sink, workspace,
                                         strip->vertex_count) < 0)
            goto fail;

        ++progress.emitted_strips;
        progress.emitted_vertices += strip->vertex_count;
    }

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

int pvr_chunk_model_two_volume_cache_emit(
    const pvr_chunk_two_volume_cache_t *cache,
    const matrix_t *object_to_screen, pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_vertex_t *workspace, size_t workspace_count,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_cache_result_t *result) {
    return pvr_chunk_model_two_volume_cache_emit_filtered(
        cache, object_to_screen, sink, workspace, workspace_count, NULL,
        begin_strip, resolve_vertex, prepare_vertex, data, result);
}

static int modifier_cache_layout_finish(
    pvr_chunk_modifier_cache_requirements_t *result) {
    size_t cursor;
    size_t bytes;

    if(result->triangle_count > SIZE_MAX / 3u) {
        errno = ERANGE;
        return -1;
    }
    result->corner_count = result->triangle_count * 3u;
    if(multiply_size(result->triangle_count,
                     sizeof(pvr_chunk_cached_modifier_triangle_t),
                     &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->packets_offset) < 0 ||
       multiply_size(result->triangle_count,
                     sizeof(pvr_modifier_vol_t), &bytes) < 0 ||
       add_size(result->packets_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT,
                  &result->deform_vertices_offset) < 0 ||
       multiply_size(result->corner_count,
                     sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       add_size(result->deform_vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, _Alignof(uint16_t),
                  &result->source_indices_offset) < 0 ||
       multiply_size(result->corner_count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(result->source_indices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, _Alignof(uint16_t),
                  &result->user_words_offset) < 0 ||
       multiply_size(result->user_word_count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(result->user_words_offset, bytes, &cursor) < 0 ||
       align_size(cursor, PVR_CHUNK_CACHE_ALIGNMENT, &result->bytes) < 0)
        return -1;

    result->alignment = PVR_CHUNK_CACHE_ALIGNMENT;
    return 0;
}

typedef struct modifier_query_context {
    const pvr_chunk_model_view_t *view;
    const pvr_chunk_model_plan_t *plan;
    pvr_chunk_modifier_cache_requirements_t *requirements;
} modifier_query_context_t;

static int query_modifier_triangle(
    const uint16_t indices[3], const uint16_t *user_words,
    size_t user_word_count, uint8_t source_type, int final_in_volume,
    void *data) {
    modifier_query_context_t *context = data;
    size_t corner;

    (void)user_words;
    (void)source_type;
    (void)final_in_volume;
    for(corner = 0; corner < 3u; ++corner) {
        pvr_chunk_vertex_attributes_t attributes;

        if(pvr_chunk_render_vertex_attributes_get(
               context->view, context->plan, indices[corner],
               &attributes) < 0)
            return -1;
    }
    if(accumulate(&context->requirements->triangle_count, 1u) < 0 ||
       accumulate(&context->requirements->user_word_count,
                  user_word_count) < 0)
        return -1;
    return 0;
}

int pvr_chunk_model_modifier_cache_query(
    const pvr_chunk_model_plan_t *plan,
    pvr_chunk_modifier_cache_requirements_t *requirements) {
    pvr_chunk_modifier_cache_requirements_t result = { 0 };
    pvr_chunk_model_view_t checked;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    modifier_query_context_t context;
    int rv;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!plan || !requirements) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&plan->view.model, &checked) < 0 ||
       pvr_chunk_polygon_iterator_init(&iterator,
           checked.model.polygon_words,
           checked.model.polygon_word_count) < 0)
        return -1;

    context.view = &checked;
    context.plan = plan;
    context.requirements = &result;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        size_t triangles;

        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        if(pvr_chunk_render_modifier_triangle_count(
               &record, &triangles) < 0)
            return -1;
        if(!triangles)
            continue;
        if(accumulate(&result.volume_count, 1u) < 0 ||
           pvr_chunk_render_visit_modifier_record(
               &record, query_modifier_triangle, &context) < 0)
            return -1;
    }
    if(rv < 0)
        return -1;
    if(!result.volume_count || !result.triangle_count) {
        errno = EILSEQ;
        return -1;
    }
    if(modifier_cache_layout_finish(&result) < 0)
        return -1;

    *requirements = result;
    return 0;
}

typedef struct modifier_build_context {
    const pvr_chunk_model_view_t *view;
    const pvr_chunk_model_plan_t *plan;
    pvr_chunk_cached_modifier_triangle_t *triangles;
    pvr_modifier_vol_t *packets;
    pvr_deform_vertex_t *deform_vertices;
    uint16_t *source_indices;
    uint16_t *user_words;
    size_t triangle_index;
    size_t corner_index;
    size_t user_word_index;
    pvr_chunk_render_prepare_modifier_t prepare_triangle;
    void *data;
} modifier_build_context_t;

static int build_modifier_triangle(
    const uint16_t indices[3], const uint16_t *user_words,
    size_t user_word_count, uint8_t source_type, int final_in_volume,
    void *data) {
    modifier_build_context_t *context = data;
    pvr_chunk_cached_modifier_triangle_t *descriptor =
        context->triangles + context->triangle_index;
    pvr_modifier_vol_t *packet =
        context->packets + context->triangle_index;
    pvr_chunk_vertex_attributes_t attributes[3];
    size_t corner;

    for(corner = 0; corner < 3u; ++corner) {
        if(pvr_chunk_render_vertex_attributes_get(
               context->view, context->plan, indices[corner],
               attributes + corner) < 0)
            return -1;
        if(attributes[corner].position.w != 1.0f &&
           !context->prepare_triangle) {
            errno = ENOTSUP;
            return -1;
        }
        base_deformation(attributes + corner,
                         context->deform_vertices +
                         context->corner_index + corner);
        context->source_indices[context->corner_index + corner] =
            indices[corner];
    }

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->first_corner = context->corner_index;
    descriptor->first_user_word = context->user_word_index;
    descriptor->user_word_count = user_word_count;
    descriptor->source_type = source_type;
    descriptor->final_in_volume = final_in_volume != 0;
    if(user_word_count)
        memcpy(context->user_words + context->user_word_index,
               user_words, user_word_count * sizeof(*user_words));

    memset(packet, 0, sizeof(*packet));
    packet->flags = PVR_CMD_VERTEX_EOL;
    packet->ax = attributes[0].position.x;
    packet->ay = attributes[0].position.y;
    packet->az = attributes[0].position.z;
    packet->bx = attributes[1].position.x;
    packet->by = attributes[1].position.y;
    packet->bz = attributes[1].position.z;
    packet->cx = attributes[2].position.x;
    packet->cy = attributes[2].position.y;
    packet->cz = attributes[2].position.z;
    if(context->prepare_triangle) {
        errno = 0;
        if(context->prepare_triangle(attributes, user_words,
                                     user_word_count, packet,
                                     context->data) < 0) {
            if(!errno)
                errno = EIO;
            return -1;
        }
        packet->flags = PVR_CMD_VERTEX_EOL;
        context->deform_vertices[context->corner_index].position.x =
            packet->ax;
        context->deform_vertices[context->corner_index].position.y =
            packet->ay;
        context->deform_vertices[context->corner_index].position.z =
            packet->az;
        context->deform_vertices[context->corner_index + 1u].position.x =
            packet->bx;
        context->deform_vertices[context->corner_index + 1u].position.y =
            packet->by;
        context->deform_vertices[context->corner_index + 1u].position.z =
            packet->bz;
        context->deform_vertices[context->corner_index + 2u].position.x =
            packet->cx;
        context->deform_vertices[context->corner_index + 2u].position.y =
            packet->cy;
        context->deform_vertices[context->corner_index + 2u].position.z =
            packet->cz;
    }

    ++context->triangle_index;
    context->corner_index += 3u;
    context->user_word_index += user_word_count;
    return 0;
}

int pvr_chunk_model_modifier_cache_build(
    const pvr_chunk_model_plan_t *plan, void *storage, size_t storage_bytes,
    pvr_chunk_render_prepare_modifier_t prepare_triangle, void *data,
    pvr_chunk_modifier_cache_t *cache) {
    pvr_chunk_modifier_cache_requirements_t requirements;
    pvr_chunk_modifier_cache_t prepared = { 0 };
    modifier_build_context_t context;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t volume_count = 0;
    int rv;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!plan || !cache || pvr_chunk_model_modifier_cache_query(
           plan, &requirements) < 0)
        return -1;
    if(!storage || ((uintptr_t)storage &
                    (PVR_CHUNK_CACHE_ALIGNMENT - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(storage_bytes < requirements.bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(reject_storage_overlap(plan, storage, requirements.bytes,
                              cache, sizeof(*cache)) < 0)
        return -1;

    memset(&context, 0, sizeof(context));
    context.view = &plan->view;
    context.plan = plan;
    context.triangles = storage;
    context.packets = (pvr_modifier_vol_t *)((uint8_t *)storage +
                                             requirements.packets_offset);
    context.deform_vertices = (pvr_deform_vertex_t *)(
        (uint8_t *)storage + requirements.deform_vertices_offset);
    context.source_indices = (uint16_t *)(
        (uint8_t *)storage + requirements.source_indices_offset);
    context.user_words = (uint16_t *)(
        (uint8_t *)storage + requirements.user_words_offset);
    context.prepare_triangle = prepare_triangle;
    context.data = data;

    if(pvr_chunk_polygon_iterator_init(&iterator,
           plan->view.model.polygon_words,
           plan->view.model.polygon_word_count) < 0)
        return -1;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        size_t triangles;

        if(record.record_class == PVR_CHUNK_RECORD_END)
            break;
        if(record.record_class != PVR_CHUNK_RECORD_VOLUME)
            continue;
        if(pvr_chunk_render_modifier_triangle_count(
               &record, &triangles) < 0)
            return -1;
        if(!triangles)
            continue;
        if(pvr_chunk_render_visit_modifier_record(
               &record, build_modifier_triangle, &context) < 0)
            return -1;
        ++volume_count;
    }
    if(rv < 0 || volume_count != requirements.volume_count ||
       context.triangle_index != requirements.triangle_count ||
       context.corner_index != requirements.corner_count ||
       context.user_word_index != requirements.user_word_count) {
        if(rv >= 0)
            errno = EILSEQ;
        return -1;
    }

    prepared.version = PVR_CHUNK_CACHE_VERSION;
    prepared.storage = storage;
    prepared.storage_bytes = requirements.bytes;
    prepared.triangles = context.triangles;
    prepared.packets = context.packets;
    prepared.deform_vertices = context.deform_vertices;
    prepared.source_indices = context.source_indices;
    prepared.user_words = context.user_words;
    prepared.volume_count = requirements.volume_count;
    prepared.triangle_count = requirements.triangle_count;
    prepared.corner_count = requirements.corner_count;
    prepared.user_word_count = requirements.user_word_count;
    memcpy(prepared.center, plan->view.model.center,
           sizeof(prepared.center));
    prepared.radius = plan->view.model.radius;
    *cache = prepared;
    return 0;
}

static int modifier_cache_valid(const pvr_chunk_modifier_cache_t *cache) {
    pvr_chunk_modifier_cache_requirements_t layout = { 0 };
    uintptr_t storage_start;
    uintptr_t storage_end;
    size_t corner_cursor = 0;
    size_t user_word_cursor = 0;
    size_t volume_count = 0;
    size_t index;

    if(!cache || cache->version != PVR_CHUNK_CACHE_VERSION ||
       !cache->storage || ((uintptr_t)cache->storage & 31u) ||
       !isfinite(cache->center[0]) || !isfinite(cache->center[1]) ||
       !isfinite(cache->center[2]) || !isfinite(cache->radius) ||
       cache->radius < 0.0f) {
        errno = EINVAL;
        return -1;
    }
    layout.volume_count = cache->volume_count;
    layout.triangle_count = cache->triangle_count;
    layout.user_word_count = cache->user_word_count;
    if(modifier_cache_layout_finish(&layout) < 0)
        return -1;
    if(address_range(cache->storage, cache->storage_bytes,
                     &storage_start, &storage_end) < 0)
        return -1;
    (void)storage_end;
    if(!layout.volume_count || !layout.triangle_count ||
       cache->corner_count != layout.corner_count ||
       cache->storage_bytes != layout.bytes ||
       (uintptr_t)cache->triangles != storage_start ||
       (uintptr_t)cache->packets != storage_start + layout.packets_offset ||
       (uintptr_t)cache->deform_vertices !=
           storage_start + layout.deform_vertices_offset ||
       (uintptr_t)cache->source_indices !=
           storage_start + layout.source_indices_offset ||
       (uintptr_t)cache->user_words !=
           storage_start + layout.user_words_offset) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < cache->triangle_count; ++index) {
        const pvr_chunk_cached_modifier_triangle_t *triangle =
            cache->triangles + index;

        if(triangle->reserved || triangle->first_corner != corner_cursor ||
           triangle->first_user_word != user_word_cursor ||
           triangle->source_type < PVR_CHUNK_VOLUME_TRIANGLES ||
           triangle->source_type > PVR_CHUNK_VOLUME_STRIPS ||
           triangle->final_in_volume > 1u ||
           accumulate(&corner_cursor, 3u) < 0 ||
           accumulate(&user_word_cursor, triangle->user_word_count) < 0 ||
           corner_cursor > cache->corner_count ||
           user_word_cursor > cache->user_word_count) {
            if(!errno)
                errno = EILSEQ;
            return -1;
        }
        if(triangle->final_in_volume)
            ++volume_count;
    }
    if(corner_cursor != cache->corner_count ||
       user_word_cursor != cache->user_word_count ||
       volume_count != cache->volume_count ||
       !cache->triangles[cache->triangle_count - 1u].final_in_volume) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int modifier_cache_emit_preflight(
    const pvr_chunk_modifier_cache_t *cache, const matrix_t *matrix,
    const pvr_chunk_modifier_config_t *config,
    const pvr_geometry_vertex_sink_t *sink,
    const pvr_modifier_vol_t *workspace) {
    uintptr_t cache_start;
    uintptr_t cache_end;
    uintptr_t descriptor_start;
    uintptr_t descriptor_end;
    uintptr_t workspace_start;
    uintptr_t workspace_end;
    uintptr_t matrix_start;
    uintptr_t matrix_end;
    uintptr_t output_start = 0;
    uintptr_t output_end = 0;
    size_t bytes;

    if(modifier_cache_valid(cache) < 0 || matrix_valid(matrix) < 0 ||
       pvr_chunk_render_modifier_sink_valid(sink) < 0 ||
       pvr_chunk_render_modifier_config_valid(config, sink) < 0)
        return -1;
    if(!workspace || ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       cache->triangle_count >
       sink->destination.memory.capacity - sink->emitted_vertices) {
        errno = ENOSPC;
        return -1;
    }
    if(address_range(cache->storage, cache->storage_bytes,
                     &cache_start, &cache_end) < 0 ||
       address_range(cache, sizeof(*cache),
                     &descriptor_start, &descriptor_end) < 0 ||
       address_range(workspace, sizeof(*workspace),
                     &workspace_start, &workspace_end) < 0 ||
       address_range(matrix, sizeof(*matrix),
                     &matrix_start, &matrix_end) < 0)
        return -1;
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY) {
        if(multiply_size(sink->destination.memory.capacity,
                         sizeof(pvr_modifier_vol_t), &bytes) < 0 ||
           address_range(sink->destination.memory.vertices, bytes,
                         &output_start, &output_end) < 0)
            return -1;
    }
    if(ranges_overlap(cache_start, cache_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(cache_start, cache_end, matrix_start, matrix_end) ||
       ranges_overlap(cache_start, cache_end, output_start, output_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      output_start, output_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      workspace_start, workspace_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      output_start, output_end) ||
       ranges_overlap(descriptor_start, descriptor_end,
                      matrix_start, matrix_end) ||
       ranges_overlap(output_start, output_end,
                      matrix_start, matrix_end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_modifier_cache_emit(
    const pvr_chunk_modifier_cache_t *cache,
    const matrix_t *object_to_screen,
    const pvr_chunk_modifier_config_t *config,
    pvr_geometry_vertex_sink_t *sink, pvr_modifier_vol_t *workspace,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_modifier_t prepare_triangle,
    void *data, pvr_chunk_modifier_cache_result_t *result) {
    pvr_chunk_modifier_cache_result_t progress = { 0 };
    size_t triangle_index;

    if(result)
        *result = progress;
    if(modifier_cache_emit_preflight(cache, object_to_screen, config, sink,
                                     workspace) < 0)
        return -1;

    for(triangle_index = 0; triangle_index < cache->triangle_count;
        ++triangle_index) {
        const pvr_chunk_cached_modifier_triangle_t *descriptor =
            cache->triangles + triangle_index;
        uint16_t indices[3];
        pvr_deform_vertex_t deformations[3];
        pvr_geometry_vertex_stream_t stream;
        uint32_t mode = descriptor->final_in_volume ? config->final_mode :
                        PVR_MODIFIER_OTHER_POLY;
        size_t corner;

        *workspace = cache->packets[triangle_index];
        for(corner = 0; corner < 3u; ++corner) {
            size_t cached_corner = descriptor->first_corner + corner;

            indices[corner] = cache->source_indices[cached_corner];
            deformations[corner] = cache->deform_vertices[cached_corner];
            if(resolve_vertex) {
                errno = 0;
                if(resolve_vertex(indices[corner], deformations + corner,
                                  data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            if(finite_deformation(deformations + corner) < 0)
                goto fail;
        }

        workspace->ax = deformations[0].position.x;
        workspace->ay = deformations[0].position.y;
        workspace->az = deformations[0].position.z;
        workspace->bx = deformations[1].position.x;
        workspace->by = deformations[1].position.y;
        workspace->bz = deformations[1].position.z;
        workspace->cx = deformations[2].position.x;
        workspace->cy = deformations[2].position.y;
        workspace->cz = deformations[2].position.z;
        if(prepare_triangle) {
            errno = 0;
            if(prepare_triangle(indices, deformations,
                                cache->user_words +
                                descriptor->first_user_word,
                                descriptor->user_word_count,
                                workspace, data) < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
        }
        workspace->flags = PVR_CMD_VERTEX_EOL;
        stream.vertices = workspace;
        stream.vertex_count = 1u;
        stream.stride = sizeof(*workspace);
        stream.format = PVR_GEOMETRY_VERTEX_MODIFIER;
        if(pvr_geometry_project_vertices(workspace, 1u, &stream,
                                         object_to_screen, NULL) < 0 ||
           pvr_chunk_render_publish_modifier(sink, config, workspace,
                                             mode) < 0)
            goto fail;

        ++progress.emitted_triangles;
        if(descriptor->final_in_volume)
            ++progress.emitted_volumes;
    }

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
