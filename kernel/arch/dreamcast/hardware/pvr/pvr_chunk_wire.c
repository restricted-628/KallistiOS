/* KallistiOS ##version##

   pvr_chunk_wire.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_wire.h>

#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

typedef struct address_range {
    uintptr_t start;
    uintptr_t end;
} address_range_t;

static int range_get(const void *pointer, size_t count, size_t element_size,
                     address_range_t *range) {
    size_t bytes;

    range->start = range->end = 0;
    if(!count)
        return 0;
    if(!pointer) {
        errno = EINVAL;
        return -1;
    }
    if(count > SIZE_MAX / element_size) {
        errno = ERANGE;
        return -1;
    }
    bytes = count * element_size;
    range->start = (uintptr_t)pointer;
    if(bytes > UINTPTR_MAX - range->start) {
        errno = ERANGE;
        return -1;
    }
    range->end = range->start + bytes;
    return 0;
}

static int ranges_overlap(const address_range_t *first,
                          const address_range_t *second) {
    return first->start < first->end && second->start < second->end &&
           first->start < second->end && second->start < first->end;
}

static int finite_deformation(const pvr_deform_vertex_t *vertex) {
    if(!vertex || !isfinite(vertex->position.x) ||
       !isfinite(vertex->position.y) || !isfinite(vertex->position.z) ||
       vertex->position.w != 1.0f || !isfinite(vertex->normal.x) ||
       !isfinite(vertex->normal.y) || !isfinite(vertex->normal.z) ||
       vertex->normal.w != 0.0f) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int frustum_valid(const pvr_frustum_t *frustum) {
    const float *matrix;
    size_t index;

    if(!frustum || ((uintptr_t)&frustum->object_to_screen & 7u) ||
       !isfinite(frustum->left) || !isfinite(frustum->top) ||
       !isfinite(frustum->right) || !isfinite(frustum->bottom) ||
       !isfinite(frustum->w_near) || !isfinite(frustum->w_far) ||
       frustum->left >= frustum->right || frustum->top >= frustum->bottom ||
       frustum->w_near <= 0.0f || frustum->w_near >= frustum->w_far) {
        errno = EINVAL;
        return -1;
    }
    matrix = &frustum->object_to_screen[0][0];
    for(index = 0; index < 16u; ++index) {
        if(!isfinite(matrix[index])) {
            errno = EDOM;
            return -1;
        }
    }
    return 0;
}

static int sink_valid(const pvr_geometry_sink_t *sink) {
    if(!sink || sink->kind < PVR_GEOMETRY_SINK_MEMORY ||
       sink->kind > PVR_GEOMETRY_SINK_BUFFERED_LIST) {
        errno = EINVAL;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY &&
       (!sink->destination.memory.vertices ||
        ((uintptr_t)sink->destination.memory.vertices & 31u) ||
        sink->emitted_vertices > sink->destination.memory.capacity)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_wire_profile_validate(const pvr_chunk_wire_profile_t *profile) {
    if(!profile || !isfinite(profile->width) || profile->width <= 0.0f ||
       profile->topology < PVR_CHUNK_WIRE_MESH ||
       profile->topology > PVR_CHUNK_WIRE_PATH ||
       profile->color_mode < PVR_CHUNK_WIRE_COLOR_PROFILE ||
       profile->color_mode > PVR_CHUNK_WIRE_COLOR_VERTEX) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int mesh_edge_count(size_t vertex_count, size_t *edges) {
    if(vertex_count < 3u) {
        errno = EILSEQ;
        return -1;
    }
    if(vertex_count - 2u > (SIZE_MAX - 1u) / 2u) {
        errno = ERANGE;
        return -1;
    }
    *edges = (vertex_count - 2u) * 2u + 1u;
    return 0;
}

int pvr_chunk_model_cache_wire_capacity(
    const pvr_chunk_model_cache_t *cache, size_t *vertices) {
    size_t total_edges = 0;
    size_t strip_index;

    if(vertices)
        *vertices = 0;
    if(!vertices || pvr_chunk_model_cache_validate(cache) < 0)
        return -1;
    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        size_t edges;

        if(mesh_edge_count(cache->strips[strip_index].vertex_count,
                           &edges) < 0)
            return -1;
        if(edges > SIZE_MAX - total_edges) {
            errno = ERANGE;
            return -1;
        }
        total_edges += edges;
    }
    if(total_edges > SIZE_MAX / PVR_GEOMETRY_LINE_VERTICES) {
        errno = ERANGE;
        return -1;
    }
    *vertices = total_edges * PVR_GEOMETRY_LINE_VERTICES;
    return 0;
}

static int preflight(const pvr_chunk_model_cache_t *cache,
                     const pvr_frustum_t *frustum,
                     pvr_chunk_clip_policy_t clip_policy,
                     const pvr_chunk_wire_profile_t *default_profile,
                     const pvr_geometry_sink_t *sink,
                     const pvr_chunk_wire_workspace_t *workspace,
                     pvr_chunk_cache_begin_strip_t begin_strip) {
    address_range_t ranges[9];
    size_t range_count = 0;
    size_t required_vertices;
    size_t first;
    size_t second;

    if(clip_policy < PVR_CHUNK_CLIP_SPLIT ||
       clip_policy > PVR_CHUNK_CLIP_ASSUME_VISIBLE ||
       pvr_chunk_model_cache_validate(cache) < 0 ||
       frustum_valid(frustum) < 0 ||
       pvr_chunk_wire_profile_validate(default_profile) < 0 ||
       sink_valid(sink) < 0 || !workspace ||
       workspace->strip_capacity < cache->maximum_strip_vertices ||
       (cache->vertex_count &&
        (!workspace->vertices || !workspace->deformations)) ||
       ((uintptr_t)workspace->vertices & 31u) ||
       ((uintptr_t)workspace->deformations & 31u) ||
       (sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip)) {
        if(!errno)
            errno = EINVAL;
        return -1;
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY) {
        if(pvr_chunk_model_cache_wire_capacity(cache, &required_vertices) < 0)
            return -1;
        if(required_vertices > sink->destination.memory.capacity -
                               sink->emitted_vertices) {
            errno = ENOSPC;
            return -1;
        }
    }

#define ADD_RANGE(pointer, count, type) do {                                  \
    if(range_get((pointer), (count), sizeof(type),                            \
                 ranges + range_count) < 0)                                  \
        return -1;                                                            \
    ++range_count;                                                            \
} while(0)

    ADD_RANGE(cache->storage, cache->storage_bytes, uint8_t);
    ADD_RANGE(cache, 1u, pvr_chunk_model_cache_t);
    ADD_RANGE(frustum, 1u, pvr_frustum_t);
    ADD_RANGE(default_profile, 1u, pvr_chunk_wire_profile_t);
    ADD_RANGE(sink, 1u, pvr_geometry_sink_t);
    ADD_RANGE(workspace, 1u, pvr_chunk_wire_workspace_t);
    ADD_RANGE(workspace->vertices, workspace->strip_capacity, pvr_vertex_t);
    ADD_RANGE(workspace->deformations, workspace->strip_capacity,
              pvr_deform_vertex_t);
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY)
        ADD_RANGE(sink->destination.memory.vertices,
                  sink->destination.memory.capacity, pvr_vertex_t);

#undef ADD_RANGE

    for(first = 0; first < range_count; ++first) {
        for(second = first + 1u; second < range_count; ++second) {
            if(ranges_overlap(ranges + first, ranges + second)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

static int assemble_strip(
    const pvr_chunk_model_cache_t *cache,
    const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_wire_workspace_t *workspace,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex, void *data) {
    size_t index;

    for(index = 0; index < strip->vertex_count; ++index) {
        size_t cached_index = strip->first_vertex + index;
        uint16_t source_index = cache->source_indices[cached_index];

        workspace->deformations[index] =
            cache->deform_vertices[cached_index];
        if(resolve_vertex) {
            errno = 0;
            if(resolve_vertex(source_index, workspace->deformations + index,
                              data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        if(finite_deformation(workspace->deformations + index) < 0)
            return -1;
        workspace->vertices[index] = cache->vertices[cached_index];
        workspace->vertices[index].x =
            workspace->deformations[index].position.x;
        workspace->vertices[index].y =
            workspace->deformations[index].position.y;
        workspace->vertices[index].z =
            workspace->deformations[index].position.z;
        if(prepare_vertex) {
            errno = 0;
            if(prepare_vertex(&strip->state, source_index,
                              workspace->deformations + index,
                              workspace->vertices + index, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        workspace->vertices[index].flags = PVR_CMD_VERTEX;
        if(!isfinite(workspace->vertices[index].x) ||
           !isfinite(workspace->vertices[index].y) ||
           !isfinite(workspace->vertices[index].z) ||
           !isfinite(workspace->vertices[index].u) ||
           !isfinite(workspace->vertices[index].v)) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}

static size_t topology_edge_count(size_t vertex_count,
                                  pvr_chunk_wire_topology_t topology) {
    if(topology == PVR_CHUNK_WIRE_MESH)
        return (vertex_count - 2u) * 2u + 1u;
    if(topology == PVR_CHUNK_WIRE_BOUNDARY)
        return vertex_count;
    return vertex_count - 1u;
}

static void topology_edge(size_t vertex_count,
                          pvr_chunk_wire_topology_t topology,
                          size_t edge, size_t indices[2]) {
    if(topology == PVR_CHUNK_WIRE_PATH) {
        indices[0] = edge;
        indices[1] = edge + 1u;
    }
    else if(topology == PVR_CHUNK_WIRE_MESH) {
        if(edge < vertex_count - 1u) {
            indices[0] = edge;
            indices[1] = edge + 1u;
        }
        else {
            indices[0] = edge - (vertex_count - 1u);
            indices[1] = indices[0] + 2u;
        }
    }
    else if(!edge) {
        indices[0] = 0;
        indices[1] = 1;
    }
    else if(edge + 1u == vertex_count) {
        indices[0] = vertex_count - 2u;
        indices[1] = vertex_count - 1u;
    }
    else {
        indices[0] = edge - 1u;
        indices[1] = edge + 1u;
    }
}

static int project_edge(
    pvr_vertex_t projected[2], const pvr_vertex_t source[2],
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t policy,
    pvr_frustum_segment_result_t *result) {
    pvr_frustum_segment_result_t progress = { 0, 0 };

    if(policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE) {
        pvr_geometry_stream_t stream = { source, 2u, sizeof(source[0]) };

        if(pvr_geometry_project(projected, 2u, &stream,
                                &frustum->object_to_screen, NULL) < 0)
            return -1;
        progress.visible = 1;
    }
    else {
        if(pvr_frustum_clip_segment(projected, source, frustum,
                                    PVR_FRUSTUM_CLIP_ALL, &progress) < 0)
            return -1;
        if(policy == PVR_CHUNK_CLIP_DROP && progress.clipped)
            progress.visible = 0;
    }
    *result = progress;
    return 0;
}

int pvr_chunk_model_cache_emit_wire(
    const pvr_chunk_model_cache_t *cache, const pvr_frustum_t *frustum,
    pvr_chunk_clip_policy_t clip_policy,
    const pvr_chunk_wire_profile_t *default_profile,
    pvr_geometry_sink_t *sink, pvr_chunk_wire_workspace_t *workspace,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    pvr_chunk_wire_resolve_profile_t resolve_profile,
    void *data, pvr_chunk_wire_result_t *result) {
    pvr_chunk_wire_result_t progress = { 0 };
    size_t strip_index;

    if(result)
        *result = progress;
    if(preflight(cache, frustum, clip_policy, default_profile, sink,
                 workspace, begin_strip) < 0)
        return -1;

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_chunk_wire_profile_t profile = *default_profile;
        size_t edge_count;
        size_t edge;
        int strip_started = 0;

        ++progress.visited_strips;
        if(filter_strip) {
            int keep;

            errno = 0;
            keep = filter_strip(strip, data);
            if(keep < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
            if(!keep) {
                ++progress.skipped_strips;
                continue;
            }
        }
        if(resolve_profile) {
            errno = 0;
            if(resolve_profile(strip, &profile, data) < 0) {
                if(!errno)
                    errno = EIO;
                goto fail;
            }
        }
        if(pvr_chunk_wire_profile_validate(&profile) < 0 ||
           assemble_strip(cache, strip, workspace, resolve_vertex,
                          prepare_vertex, data) < 0)
            goto fail;

        edge_count = topology_edge_count(strip->vertex_count,
                                         profile.topology);
        for(edge = 0; edge < edge_count; ++edge) {
            alignas(32) pvr_vertex_t source[2];
            alignas(32) pvr_vertex_t projected[2];
            alignas(32) pvr_vertex_t quad[PVR_GEOMETRY_LINE_VERTICES];
            pvr_frustum_segment_result_t segment;
            pvr_geometry_result_t line;
            size_t indices[2];

            ++progress.source_edges;
            topology_edge(strip->vertex_count, profile.topology,
                          edge, indices);
            source[0] = workspace->vertices[indices[0]];
            source[1] = workspace->vertices[indices[1]];
            source[0].flags = PVR_CMD_VERTEX;
            source[1].flags = PVR_CMD_VERTEX_EOL;
            if(profile.color_mode == PVR_CHUNK_WIRE_COLOR_PROFILE) {
                source[0].argb = source[1].argb = profile.argb;
                source[0].oargb = source[1].oargb = profile.oargb;
            }
            if(project_edge(projected, source, frustum, clip_policy,
                            &segment) < 0)
                goto fail;
            if(segment.clipped)
                ++progress.clipped_edges;
            if(!segment.visible) {
                ++progress.dropped_edges;
                continue;
            }
            if(pvr_geometry_expand_line(quad, projected, profile.width,
                                        &line) < 0)
                goto fail;
            if(!line.produced_vertices) {
                ++progress.dropped_edges;
                continue;
            }
            if(!strip_started && begin_strip) {
                errno = 0;
                if(begin_strip(strip, data) < 0) {
                    if(!errno)
                        errno = EIO;
                    goto fail;
                }
            }
            if(pvr_geometry_sink_emit(sink, quad,
                                      PVR_GEOMETRY_LINE_VERTICES) < 0)
                goto fail;
            strip_started = 1;
            ++progress.emitted_edges;
            progress.emitted_vertices += PVR_GEOMETRY_LINE_VERTICES;
        }
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
