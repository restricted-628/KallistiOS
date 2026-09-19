/* KallistiOS ##version##

   pvr_chunk_toon.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_toon.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
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

static int finite_vector(const vector_t *value) {
    return value && isfinite(value->x) && isfinite(value->y) &&
           isfinite(value->z) && isfinite(value->w);
}

static int finite_deformation(const pvr_deform_vertex_t *vertex) {
    if(!vertex || !isfinite(vertex->position.x) ||
       !isfinite(vertex->position.y) || !isfinite(vertex->position.z) ||
       vertex->position.w != 1.0f || !finite_vector(&vertex->normal) ||
       vertex->normal.w != 0.0f) {
        errno = EILSEQ;
        return -1;
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

static int frustum_valid(const pvr_frustum_t *frustum) {
    const float *matrix_values = frustum ?
        &frustum->object_to_screen[0][0] : NULL;
    size_t index;

    if(!frustum ||
       ((uintptr_t)&frustum->object_to_screen & 7u) ||
       !isfinite(frustum->left) || !isfinite(frustum->top) ||
       !isfinite(frustum->right) || !isfinite(frustum->bottom) ||
       !isfinite(frustum->w_near) || !isfinite(frustum->w_far) ||
       frustum->left >= frustum->right || frustum->top >= frustum->bottom ||
       frustum->w_near <= 0.0f || frustum->w_near >= frustum->w_far) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < 16u; ++index) {
        if(!isfinite(matrix_values[index])) {
            errno = EDOM;
            return -1;
        }
    }
    return 0;
}

static int transforms_valid(const pvr_normal_matrix_t *normal_matrix,
                            const pvr_frustum_t *frustum) {
    const float *normal_values = normal_matrix ?
        &normal_matrix->column[0][0] : NULL;
    size_t index;

    if(!normal_matrix) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < 9u; ++index) {
        if(!isfinite(normal_values[index])) {
            errno = EDOM;
            return -1;
        }
    }
    return frustum_valid(frustum);
}

int pvr_chunk_toon_profile_validate(const pvr_chunk_toon_profile_t *profile) {
    pvr_toon_light_t light;
    size_t band;

    if(!profile || !isfinite(profile->epsilon) || profile->epsilon < 0.0f ||
       profile->equation < PVR_TOON_SHADE_DOT ||
       profile->equation > PVR_TOON_SHADE_HALF_LAMBERT ||
       (profile->threshold_count && !profile->thresholds)) {
        errno = EINVAL;
        return -1;
    }
    if(profile->threshold_count == SIZE_MAX ||
       profile->threshold_count > SIZE_MAX / sizeof(float) ||
       profile->threshold_count + 1u > SIZE_MAX / sizeof(uint32_t)) {
        errno = ERANGE;
        return -1;
    }
    if(pvr_toon_light_init(&light, &profile->light.direction,
                           profile->light.intensity,
                           profile->light.ambient) < 0)
        return -1;
    for(band = 0; band < profile->threshold_count; ++band) {
        if(!isfinite(profile->thresholds[band])) {
            errno = EDOM;
            return -1;
        }
        if(band &&
           (profile->thresholds[band] <= profile->thresholds[band - 1u] ||
            profile->thresholds[band] - profile->thresholds[band - 1u] <=
                2.0f * profile->epsilon)) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int pvr_chunk_outline_profile_validate(
        const pvr_chunk_outline_profile_t *profile) {
    if(!profile || !isfinite(profile->distance) ||
       profile->distance <= 0.0f) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int workspace_valid(const pvr_chunk_model_cache_t *cache,
                           const pvr_normal_matrix_t *normal_matrix,
                           const pvr_frustum_t *frustum,
                           const pvr_geometry_sink_t *sink,
                           const pvr_chunk_toon_workspace_t *workspace,
                           pvr_chunk_clip_policy_t policy) {
    address_range_t ranges[11];
    size_t range_count = 0;
    size_t first;
    size_t second;

    if(!workspace || workspace->strip_capacity <
                       cache->maximum_strip_vertices ||
       (cache->vertex_count &&
        (!workspace->vertices || !workspace->deformations ||
         !workspace->normals || !workspace->shades)) ||
       ((uintptr_t)workspace->vertices & 31u) ||
       ((uintptr_t)workspace->deformations & 31u) ||
       ((uintptr_t)workspace->normals & 3u) ||
       ((uintptr_t)workspace->shades & 3u) ||
       !workspace->toon_triangles ||
       !workspace->toon_triangle_capacity ||
       ((uintptr_t)workspace->toon_triangles & 3u) ||
       (policy == PVR_CHUNK_CLIP_SPLIT &&
        (!workspace->clip_vertices ||
         ((uintptr_t)workspace->clip_vertices & 31u) ||
         workspace->clip_vertex_capacity <
             PVR_FRUSTUM_CLIP_MAX_VERTICES))) {
        errno = EINVAL;
        return -1;
    }

#define ADD_RANGE(pointer, count, type) do {                                  \
    if(range_get((pointer), (count), sizeof(type),                            \
                 ranges + range_count) < 0)                                  \
        return -1;                                                            \
    ++range_count;                                                            \
} while(0)

    ADD_RANGE(cache->storage, cache->storage_bytes, uint8_t);
    ADD_RANGE(cache, 1u, pvr_chunk_model_cache_t);
    ADD_RANGE(normal_matrix, 1u, pvr_normal_matrix_t);
    ADD_RANGE(frustum, 1u, pvr_frustum_t);
    ADD_RANGE(workspace->vertices, workspace->strip_capacity, pvr_vertex_t);
    ADD_RANGE(workspace->deformations, workspace->strip_capacity,
              pvr_deform_vertex_t);
    ADD_RANGE(workspace->normals, workspace->strip_capacity, vector_t);
    ADD_RANGE(workspace->shades, workspace->strip_capacity, float);
    ADD_RANGE(workspace->toon_triangles, workspace->toon_triangle_capacity,
              pvr_toon_triangle_t);
    if(policy == PVR_CHUNK_CLIP_SPLIT)
        ADD_RANGE(workspace->clip_vertices, workspace->clip_vertex_capacity,
                  pvr_vertex_t);
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

static int profile_storage_valid(
    const pvr_chunk_toon_profile_t *profile,
    const pvr_geometry_sink_t *sink,
    const pvr_chunk_toon_workspace_t *workspace) {
    address_range_t source[3];
    address_range_t mutable[6];
    size_t source_count = 0;
    size_t mutable_count = 0;
    size_t band_count = profile->threshold_count + 1u;
    size_t first;
    size_t second;

#define ADD_SOURCE(pointer, count, type) do {                                 \
    if((pointer)) {                                                           \
        if(range_get((pointer), (count), sizeof(type),                        \
                     source + source_count) < 0)                              \
            return -1;                                                        \
        ++source_count;                                                       \
    }                                                                         \
} while(0)
#define ADD_MUTABLE(pointer, count, type) do {                                \
    if(range_get((pointer), (count), sizeof(type),                            \
                 mutable + mutable_count) < 0)                                \
        return -1;                                                            \
    ++mutable_count;                                                          \
} while(0)

    ADD_SOURCE(profile->thresholds, profile->threshold_count, float);
    ADD_SOURCE(profile->argb_modulation, band_count, uint32_t);
    ADD_SOURCE(profile->oargb_modulation, band_count, uint32_t);
    ADD_MUTABLE(workspace->vertices, workspace->strip_capacity, pvr_vertex_t);
    ADD_MUTABLE(workspace->deformations, workspace->strip_capacity,
                pvr_deform_vertex_t);
    ADD_MUTABLE(workspace->normals, workspace->strip_capacity, vector_t);
    ADD_MUTABLE(workspace->shades, workspace->strip_capacity, float);
    ADD_MUTABLE(workspace->toon_triangles,
                workspace->toon_triangle_capacity, pvr_toon_triangle_t);
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY)
        ADD_MUTABLE(sink->destination.memory.vertices,
                    sink->destination.memory.capacity, pvr_vertex_t);

#undef ADD_SOURCE
#undef ADD_MUTABLE

    for(first = 0; first < source_count; ++first) {
        for(second = 0; second < mutable_count; ++second) {
            if(ranges_overlap(source + first, mutable + second)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

static void independent_indices(size_t triangle, size_t indices[3]) {
    indices[0] = triangle;
    indices[1] = triangle + 1u;
    indices[2] = triangle + 2u;
    if(triangle & 1u) {
        size_t swap = indices[0];

        indices[0] = indices[1];
        indices[1] = swap;
    }
}

static int face_normal(vector_t *normal,
                       const pvr_vertex_t vertices[3]) {
    float ax = vertices[1].x - vertices[0].x;
    float ay = vertices[1].y - vertices[0].y;
    float az = vertices[1].z - vertices[0].z;
    float bx = vertices[2].x - vertices[0].x;
    float by = vertices[2].y - vertices[0].y;
    float bz = vertices[2].z - vertices[0].z;
    float length_squared;

#ifdef __DREAMCAST__
    {
        shz_vec3_t result = shz_vec3_cross(shz_vec3_init(ax, ay, az),
                                           shz_vec3_init(bx, by, bz));

        normal->x = result.x;
        normal->y = result.y;
        normal->z = result.z;
        length_squared = shz_vec3_dot(result, result);
    }
#else
    normal->x = ay * bz - az * by;
    normal->y = az * bx - ax * bz;
    normal->z = ax * by - ay * bx;
    length_squared = normal->x * normal->x + normal->y * normal->y +
                     normal->z * normal->z;
#endif
    normal->w = 0.0f;
    if(!isfinite(length_squared)) {
        errno = ERANGE;
        return -1;
    }
    return length_squared <= FLT_MIN ? 0 : 1;
}

static int prepare_output_triangle(
    pvr_vertex_t output[3], const pvr_toon_triangle_t *triangle,
    const pvr_chunk_toon_profile_t *profile) {
    size_t corner;

    for(corner = 0; corner < 3u; ++corner) {
        const pvr_toon_vertex_t *source = triangle->vertices + corner;

        memset(output + corner, 0, sizeof(*output));
        output[corner].flags = corner == 2u ? PVR_CMD_VERTEX_EOL :
                                             PVR_CMD_VERTEX;
        output[corner].x = source->position.x;
        output[corner].y = source->position.y;
        output[corner].z = source->position.z;
        output[corner].u = source->u;
        output[corner].v = source->v;
        output[corner].argb = source->argb;
        output[corner].oargb = source->oargb;
        if(profile->argb_modulation &&
           pvr_toon_color_modulate(
               &output[corner].argb, output[corner].argb,
               profile->argb_modulation[triangle->band]) < 0)
            return -1;
        if(profile->oargb_modulation &&
           pvr_toon_color_modulate(
               &output[corner].oargb, output[corner].oargb,
               profile->oargb_modulation[triangle->band]) < 0)
            return -1;
    }
    return 0;
}

static int project_or_clip(
    pvr_vertex_t triangle[3], const pvr_frustum_t *frustum,
    pvr_chunk_clip_policy_t policy, pvr_vertex_t *clip_vertices,
    const pvr_vertex_t **output, size_t *output_count) {
    pvr_frustum_classification_t classification;

    *output = NULL;
    *output_count = 0;
    if(policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE) {
        pvr_geometry_stream_t stream = {
            triangle, 3u, sizeof(triangle[0])
        };

        if(pvr_geometry_project(triangle, 3u, &stream,
                                &frustum->object_to_screen, NULL) < 0)
            return -1;
        *output = triangle;
        *output_count = 3u;
        return 0;
    }
    if(pvr_frustum_classify_triangle(triangle, frustum,
                                     &classification) < 0)
        return -1;
    if(classification == PVR_FRUSTUM_OUTSIDE ||
       (classification == PVR_FRUSTUM_INTERSECT &&
        policy == PVR_CHUNK_CLIP_DROP))
        return 0;
    if(classification == PVR_FRUSTUM_INTERSECT) {
        pvr_frustum_clip_result_t result;

        if(pvr_frustum_clip_triangle(
               clip_vertices, PVR_FRUSTUM_CLIP_MAX_VERTICES, triangle,
               frustum, PVR_FRUSTUM_CLIP_ALL, &result) < 0)
            return -1;
        *output = clip_vertices;
        *output_count = result.output_vertices;
    }
    else {
        pvr_geometry_stream_t stream = {
            triangle, 3u, sizeof(triangle[0])
        };

        if(pvr_geometry_project(triangle, 3u, &stream,
                                &frustum->object_to_screen, NULL) < 0)
            return -1;
        *output = triangle;
        *output_count = 3u;
    }
    return 0;
}

static int assemble_strip(
    const pvr_chunk_model_cache_t *cache,
    const pvr_chunk_cached_strip_t *strip,
    pvr_vertex_t *vertices, pvr_deform_vertex_t *deformations,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex, void *data) {
    size_t index;

    for(index = 0; index < strip->vertex_count; ++index) {
        size_t cached_index = strip->first_vertex + index;
        uint16_t source_index = cache->source_indices[cached_index];
        uint32_t command = index + 1u == strip->vertex_count ?
                           PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

        deformations[index] = cache->deform_vertices[cached_index];
        if(resolve_vertex) {
            errno = 0;
            if(resolve_vertex(source_index, deformations + index,
                              data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        if(finite_deformation(deformations + index) < 0)
            return -1;
        vertices[index] = cache->vertices[cached_index];
        vertices[index].x = deformations[index].position.x;
        vertices[index].y = deformations[index].position.y;
        vertices[index].z = deformations[index].position.z;
        if(prepare_vertex) {
            errno = 0;
            if(prepare_vertex(&strip->state, source_index,
                              deformations + index,
                              vertices + index, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        vertices[index].flags = command;
        if(!isfinite(vertices[index].x) ||
           !isfinite(vertices[index].y) ||
           !isfinite(vertices[index].z) ||
           !isfinite(vertices[index].u) ||
           !isfinite(vertices[index].v)) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}

static void toon_input(pvr_toon_vertex_t *output,
                       const pvr_vertex_t *vertex,
                       const vector_t *normal, float shade,
                       uint32_t source_index) {
    memset(output, 0, sizeof(*output));
    output->position.x = vertex->x;
    output->position.y = vertex->y;
    output->position.z = vertex->z;
    output->position.w = 1.0f;
    output->normal = *normal;
    output->u = vertex->u;
    output->v = vertex->v;
    output->shade = shade;
    output->argb = vertex->argb;
    output->oargb = vertex->oargb;
    output->source_index = source_index;
}

int pvr_chunk_model_cache_emit_toon(
    const pvr_chunk_model_cache_t *cache,
    const pvr_normal_matrix_t *normal_matrix,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t clip_policy,
    const pvr_chunk_toon_profile_t *default_profile,
    pvr_geometry_sink_t *sink, pvr_chunk_toon_workspace_t *workspace,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_vertex_t prepare_vertex,
    pvr_chunk_toon_resolve_profile_t resolve_profile,
    void *data, pvr_chunk_toon_result_t *result) {
    pvr_chunk_toon_result_t progress = { 0 };
    size_t strip_index;

    if(result)
        *result = progress;
    if(clip_policy < PVR_CHUNK_CLIP_SPLIT ||
       clip_policy > PVR_CHUNK_CLIP_ASSUME_VISIBLE ||
       transforms_valid(normal_matrix, frustum) < 0 ||
       pvr_chunk_model_cache_validate(cache) < 0 || sink_valid(sink) < 0 ||
       workspace_valid(cache, normal_matrix, frustum, sink, workspace,
                       clip_policy) < 0 ||
       pvr_chunk_toon_profile_validate(default_profile) < 0 ||
       (sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip))
        return -1;

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_chunk_toon_profile_t profile = *default_profile;
        pvr_normal_stream_t normal_stream;
        size_t required_triangles;
        size_t triangle_index;
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
        if(pvr_chunk_toon_profile_validate(&profile) < 0 ||
           profile_storage_valid(&profile, sink, workspace) < 0 ||
           pvr_toon_triangle_capacity(profile.threshold_count,
                                      &required_triangles) < 0)
            goto fail;
        if(required_triangles > workspace->toon_triangle_capacity) {
            errno = ENOSPC;
            goto fail;
        }
        if(assemble_strip(cache, strip, workspace->vertices,
                          workspace->deformations, resolve_vertex,
                          prepare_vertex, data) < 0)
            goto fail;

        normal_stream.normals = &workspace->deformations[0].normal;
        normal_stream.normal_count = strip->vertex_count;
        normal_stream.stride = sizeof(workspace->deformations[0]);
        if(!(strip->state.strip_flags & PVR_CHUNK_STRIP_FLAT_SHADED) &&
           !(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT)) {
            pvr_toon_shade_stream_t shade_stream;

            if(pvr_normal_transform(
                   workspace->normals, workspace->strip_capacity,
                   &normal_stream, normal_matrix, NULL) < 0)
                goto fail;
            shade_stream.normals = workspace->normals;
            shade_stream.normal_count = strip->vertex_count;
            shade_stream.stride = sizeof(workspace->normals[0]);
            if(pvr_toon_shade_apply(
                   workspace->shades, workspace->strip_capacity,
                   &shade_stream, &profile.light, profile.equation,
                   NULL) < 0)
                goto fail;
        }

        for(triangle_index = 0;
            triangle_index + 2u < strip->vertex_count;
            ++triangle_index) {
            pvr_toon_vertex_t input[3];
            pvr_toon_split_result_t split;
            size_t indices[3];
            size_t output_index;

            ++progress.source_triangles;
            independent_indices(triangle_index, indices);
            if(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT) {
                for(output_index = 0; output_index < 3u; ++output_index) {
                    size_t index = indices[output_index];

                    toon_input(input + output_index,
                               workspace->vertices + index,
                               &workspace->deformations[index].normal,
                               0.0f, cache->source_indices[
                                   strip->first_vertex + index]);
                }
                workspace->toon_triangles[0].band = 0;
                memcpy(workspace->toon_triangles[0].vertices, input,
                       sizeof(input));
                memset(&split, 0, sizeof(split));
                split.output_triangles = 1;
            }
            else if(strip->state.strip_flags &
                    PVR_CHUNK_STRIP_FLAT_SHADED) {
                alignas(32) pvr_vertex_t face_vertices[3];
                pvr_normal_stream_t face_stream;
                vector_t object_normal;
                vector_t transformed_normal;
                float shade;
                int normal_result;

                for(output_index = 0; output_index < 3u; ++output_index)
                    face_vertices[output_index] =
                        workspace->vertices[indices[output_index]];
                normal_result = face_normal(&object_normal, face_vertices);
                if(normal_result < 0)
                    goto fail;
                if(!normal_result)
                    continue;
                face_stream.normals = &object_normal;
                face_stream.normal_count = 1;
                face_stream.stride = sizeof(object_normal);
                if(pvr_normal_transform(&transformed_normal, 1,
                                        &face_stream, normal_matrix,
                                        NULL) < 0 ||
                   pvr_toon_shade_evaluate(
                       &shade, &transformed_normal, &profile.light,
                       profile.equation) < 0)
                    goto fail;
                for(output_index = 0; output_index < 3u; ++output_index) {
                    size_t index = indices[output_index];

                    toon_input(input + output_index,
                               workspace->vertices + index,
                               &transformed_normal, shade,
                               cache->source_indices[
                                   strip->first_vertex + index]);
                }
                if(pvr_toon_split_triangle(
                       workspace->toon_triangles,
                       workspace->toon_triangle_capacity, input,
                       profile.thresholds, profile.threshold_count,
                       profile.epsilon, &split) < 0)
                    goto fail;
            }
            else {
                for(output_index = 0; output_index < 3u; ++output_index) {
                    size_t index = indices[output_index];

                    toon_input(input + output_index,
                               workspace->vertices + index,
                               workspace->normals + index,
                               workspace->shades[index],
                               cache->source_indices[
                                   strip->first_vertex + index]);
                }
                if(pvr_toon_split_triangle(
                       workspace->toon_triangles,
                       workspace->toon_triangle_capacity, input,
                       profile.thresholds, profile.threshold_count,
                       profile.epsilon, &split) < 0)
                    goto fail;
            }

            progress.generated_vertices += split.generated_vertices;
            for(output_index = 0; output_index < split.output_triangles;
                ++output_index) {
                alignas(32) pvr_vertex_t triangle[3];
                const pvr_vertex_t *output;
                size_t output_count;
                const pvr_chunk_toon_profile_t *color_profile = &profile;
                pvr_chunk_toon_profile_t unlit_profile;

                if(strip->state.strip_flags &
                   PVR_CHUNK_STRIP_IGNORE_LIGHT) {
                    unlit_profile = profile;
                    unlit_profile.argb_modulation = NULL;
                    unlit_profile.oargb_modulation = NULL;
                    color_profile = &unlit_profile;
                }
                if(prepare_output_triangle(
                       triangle, workspace->toon_triangles + output_index,
                       color_profile) < 0 ||
                   project_or_clip(triangle, frustum, clip_policy,
                                   workspace->clip_vertices, &output,
                                   &output_count) < 0)
                    goto fail;
                if(!output_count)
                    continue;
                if(!strip_started && begin_strip) {
                    errno = 0;
                    if(begin_strip(strip, data) < 0) {
                        if(!errno)
                            errno = EIO;
                        goto fail;
                    }
                }
                if(pvr_geometry_sink_emit(sink, output, output_count) < 0)
                    goto fail;
                strip_started = 1;
                progress.emitted_triangles += output_count / 3u;
                progress.emitted_vertices += output_count;
            }
        }
        if(strip_started)
            ++progress.emitted_strips;
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

static int two_volume_toon_sink_valid(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_geometry_vertex_sink_t *sink) {
    if(!sink || sink->kind < PVR_GEOMETRY_SINK_MEMORY ||
       sink->kind > PVR_GEOMETRY_SINK_BUFFERED_LIST ||
       sink->format != cache->format) {
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

static int two_volume_toon_workspace_valid(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_normal_matrix_t *normal_matrix,
    const pvr_frustum_t *frustum,
    const pvr_geometry_vertex_sink_t *sink,
    const pvr_chunk_two_volume_toon_workspace_t *workspace,
    pvr_chunk_clip_policy_t policy,
    const pvr_chunk_toon_profile_t *profile,
    const pvr_chunk_two_volume_toon_modulation_t *secondary) {
    address_range_t ranges[16];
    address_range_t sources[5];
    size_t range_count = 0;
    size_t source_count = 0;
    size_t band_count = profile->threshold_count + 1u;
    size_t sink_bytes = 0;
    const size_t mutable_first = 4u;
    size_t first;
    size_t second;

    if(!workspace || workspace->strip_capacity <
                       cache->maximum_strip_vertices ||
       (cache->vertex_count &&
        (!workspace->vertices || !workspace->deformations ||
         !workspace->normals || !workspace->shades)) ||
       !workspace->toon_triangles ||
       !workspace->secondary_toon_triangles ||
       !workspace->toon_triangle_capacity ||
       ((uintptr_t)workspace->vertices & 31u) ||
       ((uintptr_t)workspace->deformations & 31u) ||
       ((uintptr_t)workspace->normals & 3u) ||
       ((uintptr_t)workspace->shades & 3u) ||
       ((uintptr_t)workspace->toon_triangles & 3u) ||
       ((uintptr_t)workspace->secondary_toon_triangles & 3u) ||
       (policy == PVR_CHUNK_CLIP_SPLIT &&
        (!workspace->clip_primary || !workspace->clip_secondary ||
         !workspace->clip_vertices ||
         ((uintptr_t)workspace->clip_primary & 31u) ||
         ((uintptr_t)workspace->clip_secondary & 31u) ||
         ((uintptr_t)workspace->clip_vertices & 31u) ||
         workspace->clip_vertex_capacity <
             PVR_FRUSTUM_CLIP_MAX_VERTICES))) {
        errno = EINVAL;
        return -1;
    }

#define ADD_TWO_RANGE(pointer, count, type) do {                              \
    if(range_get((pointer), (count), sizeof(type),                            \
                 ranges + range_count) < 0)                                  \
        return -1;                                                            \
    ++range_count;                                                            \
} while(0)
#define ADD_TWO_SOURCE(pointer, count, type) do {                             \
    if((pointer)) {                                                           \
        if(range_get((pointer), (count), sizeof(type),                        \
                     sources + source_count) < 0)                             \
            return -1;                                                        \
        ++source_count;                                                       \
    }                                                                         \
} while(0)

    ADD_TWO_RANGE(cache->storage, cache->storage_bytes, uint8_t);
    ADD_TWO_RANGE(cache, 1u, pvr_chunk_two_volume_cache_t);
    ADD_TWO_RANGE(normal_matrix, 1u, pvr_normal_matrix_t);
    ADD_TWO_RANGE(frustum, 1u, pvr_frustum_t);
    ADD_TWO_RANGE(workspace->vertices, workspace->strip_capacity,
                  pvr_chunk_two_volume_vertex_t);
    ADD_TWO_RANGE(workspace->deformations, workspace->strip_capacity,
                  pvr_deform_vertex_t);
    ADD_TWO_RANGE(workspace->normals, workspace->strip_capacity, vector_t);
    ADD_TWO_RANGE(workspace->shades, workspace->strip_capacity, float);
    ADD_TWO_RANGE(workspace->toon_triangles,
                  workspace->toon_triangle_capacity, pvr_toon_triangle_t);
    ADD_TWO_RANGE(workspace->secondary_toon_triangles,
                  workspace->toon_triangle_capacity, pvr_toon_triangle_t);
    if(policy == PVR_CHUNK_CLIP_SPLIT) {
        ADD_TWO_RANGE(workspace->clip_primary,
                      workspace->clip_vertex_capacity, pvr_vertex_t);
        ADD_TWO_RANGE(workspace->clip_secondary,
                      workspace->clip_vertex_capacity, pvr_vertex_t);
        ADD_TWO_RANGE(workspace->clip_vertices,
                      workspace->clip_vertex_capacity,
                      pvr_chunk_two_volume_vertex_t);
    }
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY) {
        if(sink->destination.memory.capacity >
           SIZE_MAX / cache->vertex_size) {
            errno = ERANGE;
            return -1;
        }
        sink_bytes = sink->destination.memory.capacity * cache->vertex_size;
        ADD_TWO_RANGE(sink->destination.memory.vertices,
                      sink_bytes, uint8_t);
    }

    ADD_TWO_SOURCE(profile->thresholds, profile->threshold_count, float);
    ADD_TWO_SOURCE(profile->argb_modulation, band_count, uint32_t);
    ADD_TWO_SOURCE(profile->oargb_modulation, band_count, uint32_t);
    if(secondary) {
        ADD_TWO_SOURCE(secondary->argb_modulation, band_count, uint32_t);
        ADD_TWO_SOURCE(secondary->oargb_modulation, band_count, uint32_t);
    }

#undef ADD_TWO_RANGE
#undef ADD_TWO_SOURCE

    for(first = 0; first < range_count; ++first) {
        for(second = first + 1u; second < range_count; ++second) {
            if(ranges_overlap(ranges + first, ranges + second)) {
                errno = EINVAL;
                return -1;
            }
        }
        for(second = 0; first >= mutable_first &&
                        second < source_count; ++second) {
            if(ranges_overlap(ranges + first, sources + second)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    return 0;
}

static int assemble_two_volume_strip(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_two_volume_vertex_t *vertices,
    pvr_deform_vertex_t *deformations,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex, void *data) {
    size_t index;

    for(index = 0; index < strip->vertex_count; ++index) {
        size_t cached_index = strip->first_vertex + index;
        uint16_t source_index = cache->source_indices[cached_index];
        uint32_t command = index + 1u == strip->vertex_count ?
                           PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        pvr_chunk_two_volume_vertex_t vertex;

        deformations[index] = cache->deform_vertices[cached_index];
        if(resolve_vertex) {
            errno = 0;
            if(resolve_vertex(source_index, deformations + index, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        if(finite_deformation(deformations + index) < 0)
            return -1;
        memset(&vertex, 0, sizeof(vertex));
        memcpy(&vertex, (const uint8_t *)cache->vertices +
               cached_index * cache->vertex_size, cache->vertex_size);
        vertex.color.x = deformations[index].position.x;
        vertex.color.y = deformations[index].position.y;
        vertex.color.z = deformations[index].position.z;
        if(prepare_vertex) {
            errno = 0;
            if(prepare_vertex(&strip->state, source_index,
                              deformations + index, cache->format,
                              &vertex, data) < 0) {
                if(!errno)
                    errno = EIO;
                return -1;
            }
        }
        memcpy(&vertex, &command, sizeof(command));
        if(!isfinite(vertex.color.x) || !isfinite(vertex.color.y) ||
           !isfinite(vertex.color.z) ||
           (cache->format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED &&
            (!isfinite(vertex.textured.u0) ||
             !isfinite(vertex.textured.v0) ||
             !isfinite(vertex.textured.u1) ||
             !isfinite(vertex.textured.v1)))) {
            errno = EILSEQ;
            return -1;
        }
        vertices[index] = vertex;
    }
    return 0;
}

static void two_volume_toon_input(
    pvr_toon_vertex_t *primary, pvr_toon_vertex_t *secondary,
    const pvr_chunk_two_volume_vertex_t *vertex,
    pvr_geometry_vertex_format_t format,
    const vector_t *normal, float shade, uint32_t source_index) {
    memset(primary, 0, sizeof(*primary));
    primary->position.x = vertex->color.x;
    primary->position.y = vertex->color.y;
    primary->position.z = vertex->color.z;
    primary->position.w = 1.0f;
    primary->normal = *normal;
    primary->shade = shade;
    primary->source_index = source_index;
    *secondary = *primary;
    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
        primary->argb = vertex->color.argb0;
        secondary->argb = vertex->color.argb1;
    }
    else {
        primary->u = vertex->textured.u0;
        primary->v = vertex->textured.v0;
        primary->argb = vertex->textured.argb0;
        primary->oargb = vertex->textured.oargb0;
        secondary->u = vertex->textured.u1;
        secondary->v = vertex->textured.v1;
        secondary->argb = vertex->textured.argb1;
        secondary->oargb = vertex->textured.oargb1;
    }
}

static int prepare_two_volume_triangle(
    pvr_chunk_two_volume_vertex_t output[3],
    pvr_geometry_vertex_format_t format,
    const pvr_toon_triangle_t *primary_triangle,
    const pvr_toon_triangle_t *secondary_triangle,
    const pvr_chunk_toon_profile_t *profile,
    const pvr_chunk_two_volume_toon_modulation_t *secondary) {
    size_t corner;

    memset(output, 0, sizeof(*output) * 3u);
    for(corner = 0; corner < 3u; ++corner) {
        const pvr_toon_vertex_t *primary =
            primary_triangle->vertices + corner;
        const pvr_toon_vertex_t *secondary_vertex =
            secondary_triangle->vertices + corner;
        uint32_t command = corner == 2u ? PVR_CMD_VERTEX_EOL :
                                         PVR_CMD_VERTEX;
        uint32_t argb0 = primary->argb;
        uint32_t oargb0 = primary->oargb;
        uint32_t argb1 = secondary_vertex->argb;
        uint32_t oargb1 = secondary_vertex->oargb;

        if(primary_triangle->band != secondary_triangle->band ||
           memcmp(&primary->position, &secondary_vertex->position,
                  sizeof(primary->position)) ||
           primary->shade != secondary_vertex->shade) {
            errno = EILSEQ;
            return -1;
        }

        if(profile->argb_modulation &&
           pvr_toon_color_modulate(&argb0, argb0,
               profile->argb_modulation[primary_triangle->band]) < 0)
            return -1;
        if(profile->oargb_modulation &&
           pvr_toon_color_modulate(&oargb0, oargb0,
               profile->oargb_modulation[primary_triangle->band]) < 0)
            return -1;
        if(secondary && secondary->argb_modulation &&
           pvr_toon_color_modulate(&argb1, argb1,
               secondary->argb_modulation[primary_triangle->band]) < 0)
            return -1;
        if(secondary && secondary->oargb_modulation &&
           pvr_toon_color_modulate(&oargb1, oargb1,
               secondary->oargb_modulation[primary_triangle->band]) < 0)
            return -1;
        output[corner].color.flags = command;
        output[corner].color.x = primary->position.x;
        output[corner].color.y = primary->position.y;
        output[corner].color.z = primary->position.z;
        if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
            output[corner].color.argb0 = argb0;
            output[corner].color.argb1 = argb1;
        }
        else {
            output[corner].textured.u0 = primary->u;
            output[corner].textured.v0 = primary->v;
            output[corner].textured.argb0 = argb0;
            output[corner].textured.oargb0 = oargb0;
            output[corner].textured.u1 = secondary_vertex->u;
            output[corner].textured.v1 = secondary_vertex->v;
            output[corner].textured.argb1 = argb1;
            output[corner].textured.oargb1 = oargb1;
        }
    }
    return 0;
}

static void two_volume_canonical_pair(
    const pvr_chunk_two_volume_vertex_t *source,
    pvr_geometry_vertex_format_t format,
    pvr_vertex_t *primary, pvr_vertex_t *secondary) {
    memset(primary, 0, sizeof(*primary));
    memset(secondary, 0, sizeof(*secondary));
    primary->flags = secondary->flags = source->color.flags;
    primary->x = secondary->x = source->color.x;
    primary->y = secondary->y = source->color.y;
    primary->z = secondary->z = source->color.z;
    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
        primary->argb = source->color.argb0;
        secondary->argb = source->color.argb1;
    }
    else {
        primary->u = source->textured.u0;
        primary->v = source->textured.v0;
        primary->argb = source->textured.argb0;
        primary->oargb = source->textured.oargb0;
        secondary->u = source->textured.u1;
        secondary->v = source->textured.v1;
        secondary->argb = source->textured.argb1;
        secondary->oargb = source->textured.oargb1;
    }
}

static int two_volume_project_or_clip(
    pvr_chunk_two_volume_vertex_t triangle[3],
    pvr_geometry_vertex_format_t format,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t policy,
    pvr_chunk_two_volume_toon_workspace_t *workspace,
    const pvr_chunk_two_volume_vertex_t **output, size_t *output_count) {
    alignas(32) pvr_vertex_t primary[3];
    alignas(32) pvr_vertex_t secondary[3];
    pvr_frustum_classification_t classification;
    pvr_geometry_vertex_stream_t stream = {
        triangle, 3u,
        format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR ?
            sizeof(pvr_vertex_pcm_t) : sizeof(pvr_vertex_tpcm_t),
        format
    };
    size_t corner;

    *output = NULL;
    *output_count = 0;
    for(corner = 0; corner < 3u; ++corner)
        two_volume_canonical_pair(triangle + corner, format,
                                  primary + corner, secondary + corner);
    if(policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE) {
        if(pvr_geometry_project_vertices(triangle, 3u, &stream,
                                         &frustum->object_to_screen,
                                         NULL) < 0)
            return -1;
        *output = triangle;
        *output_count = 3u;
        return 0;
    }
    if(pvr_frustum_classify_triangle(primary, frustum, &classification) < 0)
        return -1;
    if(classification == PVR_FRUSTUM_OUTSIDE ||
       (classification == PVR_FRUSTUM_INTERSECT &&
        policy == PVR_CHUNK_CLIP_DROP))
        return 0;
    if(classification == PVR_FRUSTUM_INSIDE) {
        if(pvr_geometry_project_vertices(triangle, 3u, &stream,
                                         &frustum->object_to_screen,
                                         NULL) < 0)
            return -1;
        *output = triangle;
        *output_count = 3u;
        return 0;
    }
    else {
        pvr_frustum_clip_result_t first_result;
        pvr_frustum_clip_result_t second_result;
        uint32_t attributes = format ==
            PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR ?
            PVR_FRUSTUM_CLIP_ARGB : PVR_FRUSTUM_CLIP_ALL;

        if(pvr_frustum_clip_triangle(
               workspace->clip_primary, workspace->clip_vertex_capacity,
               primary, frustum, attributes, &first_result) < 0 ||
           pvr_frustum_clip_triangle(
               workspace->clip_secondary, workspace->clip_vertex_capacity,
               secondary, frustum, attributes, &second_result) < 0)
            return -1;
        if(first_result.output_vertices != second_result.output_vertices) {
            errno = EILSEQ;
            return -1;
        }
        for(corner = 0; corner < first_result.output_vertices; ++corner) {
            const pvr_vertex_t *first = workspace->clip_primary + corner;
            const pvr_vertex_t *second = workspace->clip_secondary + corner;
            pvr_chunk_two_volume_vertex_t *destination =
                workspace->clip_vertices + corner;

            if(memcmp(&first->x, &second->x, sizeof(float) * 3u)) {
                errno = EILSEQ;
                return -1;
            }
            memset(destination, 0, sizeof(*destination));
            destination->color.flags = first->flags;
            destination->color.x = first->x;
            destination->color.y = first->y;
            destination->color.z = first->z;
            if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
                destination->color.argb0 = first->argb;
                destination->color.argb1 = second->argb;
            }
            else {
                destination->textured.u0 = first->u;
                destination->textured.v0 = first->v;
                destination->textured.argb0 = first->argb;
                destination->textured.oargb0 = first->oargb;
                destination->textured.u1 = second->u;
                destination->textured.v1 = second->v;
                destination->textured.argb1 = second->argb;
                destination->textured.oargb1 = second->oargb;
            }
        }
        *output = workspace->clip_vertices;
        *output_count = first_result.output_vertices;
        return 0;
    }
}

int pvr_chunk_model_two_volume_cache_emit_toon(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_normal_matrix_t *normal_matrix,
    const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t clip_policy,
    const pvr_chunk_toon_profile_t *profile,
    const pvr_chunk_two_volume_toon_modulation_t *secondary_modulation,
    pvr_geometry_vertex_sink_t *sink,
    pvr_chunk_two_volume_toon_workspace_t *workspace,
    pvr_chunk_cache_filter_strip_t filter_strip,
    pvr_chunk_cache_begin_strip_t begin_strip,
    pvr_chunk_cache_resolve_vertex_t resolve_vertex,
    pvr_chunk_cache_prepare_two_volume_vertex_t prepare_vertex,
    void *data, pvr_chunk_toon_result_t *result) {
    pvr_chunk_toon_result_t progress = { 0 };
    size_t required_triangles = 0;
    size_t strip_index;

    if(result)
        *result = progress;
    if(clip_policy < PVR_CHUNK_CLIP_SPLIT ||
       clip_policy > PVR_CHUNK_CLIP_ASSUME_VISIBLE ||
       transforms_valid(normal_matrix, frustum) < 0 ||
       pvr_chunk_model_two_volume_cache_validate(cache) < 0 ||
       two_volume_toon_sink_valid(cache, sink) < 0 ||
       pvr_chunk_toon_profile_validate(profile) < 0 ||
       pvr_toon_triangle_capacity(profile->threshold_count,
                                  &required_triangles) < 0)
        return -1;
    if(two_volume_toon_workspace_valid(
           cache, normal_matrix, frustum, sink, workspace, clip_policy,
           profile, secondary_modulation) < 0 ||
       (sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip))
        return -1;
    if(required_triangles > workspace->toon_triangle_capacity) {
        errno = ENOSPC;
        return -1;
    }

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_normal_stream_t normal_stream;
        size_t triangle_index;
        int strip_started = 0;

        ++progress.visited_strips;
        if(filter_strip) {
            int keep;

            errno = 0;
            keep = filter_strip(strip, data);
            if(keep < 0) {
                if(!errno)
                    errno = EIO;
                goto two_fail;
            }
            if(!keep) {
                ++progress.skipped_strips;
                continue;
            }
        }
        if(assemble_two_volume_strip(
               cache, strip, workspace->vertices, workspace->deformations,
               resolve_vertex, prepare_vertex, data) < 0)
            goto two_fail;
        normal_stream.normals = &workspace->deformations[0].normal;
        normal_stream.normal_count = strip->vertex_count;
        normal_stream.stride = sizeof(workspace->deformations[0]);
        if(!(strip->state.strip_flags & PVR_CHUNK_STRIP_FLAT_SHADED) &&
           !(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT)) {
            pvr_toon_shade_stream_t shade_stream;

            if(pvr_normal_transform(
                   workspace->normals, workspace->strip_capacity,
                   &normal_stream, normal_matrix, NULL) < 0)
                goto two_fail;
            shade_stream.normals = workspace->normals;
            shade_stream.normal_count = strip->vertex_count;
            shade_stream.stride = sizeof(workspace->normals[0]);
            if(pvr_toon_shade_apply(
                   workspace->shades, workspace->strip_capacity,
                   &shade_stream, &profile->light, profile->equation,
                   NULL) < 0)
                goto two_fail;
        }

        for(triangle_index = 0;
            triangle_index + 2u < strip->vertex_count; ++triangle_index) {
            pvr_toon_vertex_t input[3];
            pvr_toon_vertex_t secondary_input[3];
            pvr_toon_split_result_t split = { 0 };
            pvr_toon_split_result_t secondary_split = { 0 };
            size_t indices[3];
            size_t output_index;

            ++progress.source_triangles;
            independent_indices(triangle_index, indices);
            if((strip->state.strip_flags & PVR_CHUNK_STRIP_FLAT_SHADED) &&
               !(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT)) {
                alignas(32) pvr_vertex_t positions[3];
                pvr_normal_stream_t face_stream;
                vector_t object_normal;
                vector_t transformed_normal;
                float shade;
                int normal_result;

                memset(positions, 0, sizeof(positions));
                for(output_index = 0; output_index < 3u; ++output_index) {
                    const pvr_vertex_pcm_t *vertex =
                        &workspace->vertices[indices[output_index]].color;

                    positions[output_index].x = vertex->x;
                    positions[output_index].y = vertex->y;
                    positions[output_index].z = vertex->z;
                }
                normal_result = face_normal(&object_normal, positions);
                if(normal_result < 0)
                    goto two_fail;
                if(!normal_result)
                    continue;
                face_stream.normals = &object_normal;
                face_stream.normal_count = 1u;
                face_stream.stride = sizeof(object_normal);
                if(pvr_normal_transform(&transformed_normal, 1u,
                                        &face_stream, normal_matrix,
                                        NULL) < 0 ||
                   pvr_toon_shade_evaluate(
                       &shade, &transformed_normal, &profile->light,
                       profile->equation) < 0)
                    goto two_fail;
                for(output_index = 0; output_index < 3u; ++output_index) {
                    size_t index = indices[output_index];

                    two_volume_toon_input(
                        input + output_index,
                        secondary_input + output_index,
                        workspace->vertices + index,
                        cache->format, &transformed_normal, shade,
                        cache->source_indices[strip->first_vertex + index]);
                }
            }
            else {
                for(output_index = 0; output_index < 3u; ++output_index) {
                    size_t index = indices[output_index];
                    const vector_t *normal =
                        strip->state.strip_flags &
                        PVR_CHUNK_STRIP_IGNORE_LIGHT ?
                        &workspace->deformations[index].normal :
                        workspace->normals + index;
                    float shade = strip->state.strip_flags &
                        PVR_CHUNK_STRIP_IGNORE_LIGHT ? 0.0f :
                        workspace->shades[index];

                    two_volume_toon_input(
                        input + output_index,
                        secondary_input + output_index,
                        workspace->vertices + index,
                        cache->format, normal, shade,
                        cache->source_indices[strip->first_vertex + index]);
                }
            }
            if(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT) {
                workspace->toon_triangles[0].band = 0;
                memcpy(workspace->toon_triangles[0].vertices, input,
                       sizeof(input));
                workspace->secondary_toon_triangles[0].band = 0;
                memcpy(workspace->secondary_toon_triangles[0].vertices,
                       secondary_input, sizeof(secondary_input));
                split.output_triangles = 1u;
                secondary_split.output_triangles = 1u;
            }
            else {
                if(pvr_toon_split_triangle(
                       workspace->toon_triangles,
                       workspace->toon_triangle_capacity, input,
                       profile->thresholds, profile->threshold_count,
                       profile->epsilon, &split) < 0 ||
                   pvr_toon_split_triangle(
                       workspace->secondary_toon_triangles,
                       workspace->toon_triangle_capacity, secondary_input,
                       profile->thresholds, profile->threshold_count,
                       profile->epsilon, &secondary_split) < 0)
                    goto two_fail;
                if(split.output_triangles !=
                       secondary_split.output_triangles ||
                   split.generated_vertices !=
                       secondary_split.generated_vertices ||
                   split.crossed_thresholds !=
                       secondary_split.crossed_thresholds) {
                    errno = EILSEQ;
                    goto two_fail;
                }
            }

            progress.generated_vertices += split.generated_vertices;
            for(output_index = 0; output_index < split.output_triangles;
                ++output_index) {
                alignas(32) pvr_chunk_two_volume_vertex_t triangle[3];
                const pvr_chunk_two_volume_vertex_t *output;
                size_t output_count;
                const pvr_chunk_toon_profile_t *color_profile = profile;
                const pvr_chunk_two_volume_toon_modulation_t *color_secondary =
                    secondary_modulation;
                pvr_chunk_toon_profile_t unlit;

                if(strip->state.strip_flags & PVR_CHUNK_STRIP_IGNORE_LIGHT) {
                    unlit = *profile;
                    unlit.argb_modulation = NULL;
                    unlit.oargb_modulation = NULL;
                    color_profile = &unlit;
                    color_secondary = NULL;
                }
                if(prepare_two_volume_triangle(
                       triangle, cache->format,
                       workspace->toon_triangles + output_index,
                       workspace->secondary_toon_triangles + output_index,
                       color_profile, color_secondary) < 0 ||
                   two_volume_project_or_clip(
                       triangle, cache->format, frustum, clip_policy,
                       workspace, &output, &output_count) < 0)
                    goto two_fail;
                if(!output_count)
                    continue;
                if(!strip_started && begin_strip) {
                    errno = 0;
                    if(begin_strip(strip, data) < 0) {
                        if(!errno)
                            errno = EIO;
                        goto two_fail;
                    }
                }
                if(pvr_geometry_vertex_sink_emit(sink, output,
                                                  output_count) < 0)
                    goto two_fail;
                strip_started = 1;
                progress.emitted_triangles += output_count / 3u;
                progress.emitted_vertices += output_count;
            }
        }
        if(strip_started)
            ++progress.emitted_strips;
    }
    if(result)
        *result = progress;
    return 0;

two_fail:
    if(!errno)
        errno = EIO;
    if(result)
        *result = progress;
    return -1;
}

static int outline_workspace_valid(
        const pvr_chunk_model_cache_t *cache,
        const pvr_frustum_t *frustum,
        const pvr_chunk_outline_profile_t *default_profile,
        const pvr_geometry_sink_t *sink,
        const pvr_chunk_outline_workspace_t *workspace,
        pvr_chunk_clip_policy_t policy) {
    address_range_t ranges[10];
    size_t range_count = 0;
    size_t first;
    size_t second;

    if(!workspace ||
       workspace->strip_capacity < cache->maximum_strip_vertices ||
       (cache->vertex_count &&
        (!workspace->vertices || !workspace->deformations)) ||
       ((uintptr_t)workspace->vertices & 31u) ||
       ((uintptr_t)workspace->deformations & 31u) ||
       (policy == PVR_CHUNK_CLIP_SPLIT &&
        (!workspace->clip_vertices ||
         ((uintptr_t)workspace->clip_vertices & 31u) ||
         workspace->clip_vertex_capacity <
             PVR_FRUSTUM_CLIP_MAX_VERTICES))) {
        errno = EINVAL;
        return -1;
    }

#define ADD_OUTLINE_RANGE(pointer, count, type) do {                          \
    if(range_get((pointer), (count), sizeof(type),                            \
                 ranges + range_count) < 0)                                  \
        return -1;                                                            \
    ++range_count;                                                            \
} while(0)

    ADD_OUTLINE_RANGE(cache->storage, cache->storage_bytes, uint8_t);
    ADD_OUTLINE_RANGE(cache, 1u, pvr_chunk_model_cache_t);
    ADD_OUTLINE_RANGE(frustum, 1u, pvr_frustum_t);
    ADD_OUTLINE_RANGE(default_profile, 1u, pvr_chunk_outline_profile_t);
    ADD_OUTLINE_RANGE(sink, 1u, pvr_geometry_sink_t);
    ADD_OUTLINE_RANGE(workspace, 1u, pvr_chunk_outline_workspace_t);
    ADD_OUTLINE_RANGE(workspace->vertices, workspace->strip_capacity,
                      pvr_vertex_t);
    ADD_OUTLINE_RANGE(workspace->deformations, workspace->strip_capacity,
                      pvr_deform_vertex_t);
    if(policy == PVR_CHUNK_CLIP_SPLIT)
        ADD_OUTLINE_RANGE(workspace->clip_vertices,
                          workspace->clip_vertex_capacity, pvr_vertex_t);
    if(sink->kind == PVR_GEOMETRY_SINK_MEMORY)
        ADD_OUTLINE_RANGE(sink->destination.memory.vertices,
                          sink->destination.memory.capacity, pvr_vertex_t);

#undef ADD_OUTLINE_RANGE

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

static int outline_vertex(pvr_vertex_t *vertex, const vector_t *normal,
                          const pvr_chunk_outline_profile_t *profile) {
    point_t position = { vertex->x, vertex->y, vertex->z, 1.0f };
    point_t expanded;

    if(pvr_toon_outline_expand(&expanded, &position, normal,
                               profile->distance) < 0)
        return -1;
    vertex->x = expanded.x;
    vertex->y = expanded.y;
    vertex->z = expanded.z;
    vertex->argb = profile->argb;
    vertex->oargb = profile->oargb;
    return 0;
}

int pvr_chunk_model_cache_emit_outline(
        const pvr_chunk_model_cache_t *cache,
        const pvr_frustum_t *frustum, pvr_chunk_clip_policy_t clip_policy,
        const pvr_chunk_outline_profile_t *default_profile,
        pvr_geometry_sink_t *sink,
        pvr_chunk_outline_workspace_t *workspace,
        pvr_chunk_cache_filter_strip_t filter_strip,
        pvr_chunk_cache_begin_strip_t begin_strip,
        pvr_chunk_cache_resolve_vertex_t resolve_vertex,
        pvr_chunk_cache_prepare_vertex_t prepare_vertex,
        pvr_chunk_outline_resolve_profile_t resolve_profile,
        void *data, pvr_chunk_outline_result_t *result) {
    pvr_chunk_outline_result_t progress = { 0 };
    size_t strip_index;

    if(result)
        *result = progress;
    if(clip_policy < PVR_CHUNK_CLIP_SPLIT ||
       clip_policy > PVR_CHUNK_CLIP_ASSUME_VISIBLE ||
       pvr_chunk_model_cache_validate(cache) < 0 ||
       frustum_valid(frustum) < 0 || sink_valid(sink) < 0 ||
       pvr_chunk_outline_profile_validate(default_profile) < 0 ||
       outline_workspace_valid(cache, frustum, default_profile, sink,
                               workspace, clip_policy) < 0 ||
       (sink->kind != PVR_GEOMETRY_SINK_MEMORY && !begin_strip))
        return -1;

    for(strip_index = 0; strip_index < cache->strip_count; ++strip_index) {
        const pvr_chunk_cached_strip_t *strip = cache->strips + strip_index;
        pvr_chunk_outline_profile_t profile = *default_profile;
        size_t triangle_index;
        int flat;
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
        if(pvr_chunk_outline_profile_validate(&profile) < 0 ||
           assemble_strip(cache, strip, workspace->vertices,
                          workspace->deformations, resolve_vertex,
                          prepare_vertex, data) < 0)
            goto fail;

        flat = !!(strip->state.strip_flags &
                  PVR_CHUNK_STRIP_FLAT_SHADED);
        if(!flat) {
            size_t vertex_index;

            for(vertex_index = 0; vertex_index < strip->vertex_count;
                ++vertex_index) {
                if(outline_vertex(workspace->vertices + vertex_index,
                                  &workspace->deformations[vertex_index].normal,
                                  &profile) < 0)
                    goto fail;
            }
        }

        for(triangle_index = 0;
            triangle_index + 2u < strip->vertex_count;
            ++triangle_index) {
            alignas(32) pvr_vertex_t triangle[3];
            const pvr_vertex_t *output;
            size_t output_count;
            size_t indices[3];
            size_t corner;

            ++progress.source_triangles;
            independent_indices(triangle_index, indices);
            for(corner = 0; corner < 3u; ++corner)
                triangle[corner] = workspace->vertices[indices[corner]];
            if(flat) {
                vector_t normal;
                int normal_result = face_normal(&normal, triangle);

                if(normal_result < 0)
                    goto fail;
                if(!normal_result) {
                    ++progress.dropped_triangles;
                    continue;
                }
                for(corner = 0; corner < 3u; ++corner) {
                    if(outline_vertex(triangle + corner, &normal,
                                      &profile) < 0)
                        goto fail;
                }
            }
            else {
                for(corner = 0; corner < 3u; ++corner) {
                    triangle[corner].argb = profile.argb;
                    triangle[corner].oargb = profile.oargb;
                }
            }
            triangle[0].flags = PVR_CMD_VERTEX;
            triangle[1].flags = PVR_CMD_VERTEX;
            triangle[2].flags = PVR_CMD_VERTEX_EOL;
            if(project_or_clip(triangle, frustum, clip_policy,
                               workspace->clip_vertices, &output,
                               &output_count) < 0)
                goto fail;
            if(!output_count) {
                ++progress.dropped_triangles;
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
            if(pvr_geometry_sink_emit(sink, output, output_count) < 0)
                goto fail;
            strip_started = 1;
            progress.emitted_triangles += output_count / 3u;
            progress.emitted_vertices += output_count;
        }
        if(strip_started)
            ++progress.emitted_strips;
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
