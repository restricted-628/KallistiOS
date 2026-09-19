/* KallistiOS ##version##

   pvr_chunk_shape.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_shape.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int multiply_size(size_t count, size_t size, size_t *bytes) {
    if(count && size > SIZE_MAX / count) {
        errno = ERANGE;
        return -1;
    }

    *bytes = count * size;
    return 0;
}

static int add_size(size_t left, size_t right, size_t *sum) {
    if(right > SIZE_MAX - left) {
        errno = ERANGE;
        return -1;
    }

    *sum = left + right;
    return 0;
}

static int align_size(size_t size, size_t alignment, size_t *aligned) {
    size_t mask = alignment - 1u;

    if(size > SIZE_MAX - mask) {
        errno = ERANGE;
        return -1;
    }

    *aligned = (size + mask) & ~mask;
    return 0;
}

static int address_range(const void *address, size_t bytes,
                         uintptr_t *start, uintptr_t *end) {
    uintptr_t first = (uintptr_t)address;

    if((bytes && !address) || bytes > UINTPTR_MAX - first) {
        errno = bytes && !address ? EINVAL : ERANGE;
        return -1;
    }

    *start = first;
    *end = first + bytes;
    return 0;
}

static int ranges_overlap(uintptr_t first_start, uintptr_t first_end,
                          uintptr_t second_start, uintptr_t second_end) {
    return first_start < first_end && second_start < second_end &&
           first_start < second_end && second_start < first_end;
}

static int reject_overlap(const void *first, size_t first_bytes,
                          const void *second, size_t second_bytes) {
    uintptr_t first_start;
    uintptr_t first_end;
    uintptr_t second_start;
    uintptr_t second_end;

    if(address_range(first, first_bytes, &first_start, &first_end) < 0 ||
       address_range(second, second_bytes, &second_start, &second_end) < 0)
        return -1;
    if(ranges_overlap(first_start, first_end, second_start, second_end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int finite_delta(const pvr_morph_delta_t *delta) {
    return isfinite(delta->position.x) && isfinite(delta->position.y) &&
           isfinite(delta->position.z) && isfinite(delta->normal.x) &&
           isfinite(delta->normal.y) && isfinite(delta->normal.z) &&
           delta->position.w == 0.0f && delta->normal.w == 0.0f;
}

static int plan_slot(const pvr_chunk_model_plan_t *plan,
                     uint16_t vertex_index, size_t *slot) {
    size_t page = vertex_index / PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE;
    size_t page_slot;
    size_t mapped;

    if(!plan || !plan->vertex_index || !slot ||
       page >= PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT) {
        errno = EINVAL;
        return -1;
    }

    page_slot = plan->vertex_page_slots[page];
    if(!page_slot || page_slot > plan->indexed_page_count) {
        errno = ENOENT;
        return -1;
    }

    mapped = (page_slot - 1u) * PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE +
             vertex_index % PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE;
    if(mapped >= plan->vertex_index_count ||
       plan->vertex_index[mapped].reserved) {
        errno = EILSEQ;
        return -1;
    }
    if(!plan->vertex_index[mapped].type) {
        errno = ENOENT;
        return -1;
    }

    *slot = mapped;
    return 0;
}

static int plan_vertex_index(const pvr_chunk_model_plan_t *plan,
                             size_t page, size_t offset,
                             uint16_t *vertex_index, size_t *slot) {
    size_t page_slot;
    size_t mapped;

    if(page >= PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT ||
       offset >= PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE) {
        errno = EINVAL;
        return -1;
    }

    page_slot = plan->vertex_page_slots[page];
    if(!page_slot)
        return 0;
    if(page_slot > plan->indexed_page_count) {
        errno = EILSEQ;
        return -1;
    }

    mapped = (page_slot - 1u) * PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE + offset;
    if(mapped >= plan->vertex_index_count ||
       plan->vertex_index[mapped].reserved) {
        errno = EILSEQ;
        return -1;
    }
    if(!plan->vertex_index[mapped].type)
        return 0;

    *vertex_index = (uint16_t)(page * PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE +
                               offset);
    *slot = mapped;
    return 1;
}

static int plan_pages_valid(const pvr_chunk_model_plan_t *plan) {
    uint8_t seen[PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT] = { 0 };
    size_t present = 0;
    size_t page;

    if(!plan->vertex_index ||
       ((uintptr_t)plan->vertex_index &
        (_Alignof(pvr_chunk_vertex_index_entry_t) - 1u)) ||
       plan->indexed_page_count > PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT) {
        errno = EILSEQ;
        return 0;
    }

    for(page = 0; page < PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT; ++page) {
        size_t slot = plan->vertex_page_slots[page];

        if(!slot)
            continue;
        if(slot > plan->indexed_page_count || seen[slot - 1u]) {
            errno = EILSEQ;
            return 0;
        }
        seen[slot - 1u] = 1u;
        ++present;
    }
    if(present != plan->indexed_page_count) {
        errno = EILSEQ;
        return 0;
    }
    return 1;
}

int pvr_chunk_shape_query(const pvr_chunk_model_plan_t *plan,
                          size_t target_count,
                          pvr_chunk_shape_requirements_t *requirements) {
    pvr_chunk_shape_requirements_t result = { 0 };
    pvr_chunk_model_plan_requirements_t plan_requirements;
    size_t vertex_bytes;
    size_t delta_count;
    size_t delta_bytes;
    size_t source_bytes;

    if(requirements)
        *requirements = result;
    if(!plan || !requirements || !target_count) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_plan_query(&plan->view, &plan_requirements) < 0)
        return -1;
    if(!plan_pages_valid(plan) ||
       plan->indexed_page_count != plan_requirements.indexed_pages ||
       plan->vertex_index_count != plan_requirements.vertex_index_entries ||
       !plan->view.info.vertex_entries) {
        errno = EILSEQ;
        return -1;
    }

    result.alignment = 32u;
    result.lookup_entries = plan->vertex_index_count;
    result.source_vertices = plan->view.info.vertex_entries;
    result.source_targets = target_count;
    if(multiply_size(result.lookup_entries, sizeof(uint32_t),
                     &result.lookup_bytes) < 0 ||
       multiply_size(result.source_vertices, sizeof(pvr_deform_vertex_t),
                     &vertex_bytes) < 0 ||
       multiply_size(result.source_vertices, target_count,
                     &delta_count) < 0 ||
       multiply_size(delta_count, sizeof(pvr_morph_delta_t),
                     &delta_bytes) < 0 ||
       add_size(vertex_bytes, delta_bytes, &source_bytes) < 0 ||
       align_size(source_bytes, result.alignment,
                  &result.source_bytes) < 0)
        return -1;

    *requirements = result;
    return 0;
}

static int validate_targets(const pvr_chunk_model_plan_t *plan,
                            const pvr_chunk_shape_set_t *shapes) {
    size_t target_index;

    if(!shapes || !shapes->targets || !shapes->target_count ||
       ((uintptr_t)shapes->targets &
        (_Alignof(pvr_chunk_shape_target_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(shapes->target_count > SIZE_MAX / sizeof(*shapes->targets) ||
       shapes->target_count * sizeof(*shapes->targets) >
       UINTPTR_MAX - (uintptr_t)shapes->targets) {
        errno = ERANGE;
        return -1;
    }

    for(target_index = 0; target_index < shapes->target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            shapes->targets + target_index;
        size_t delta_index;

        if(!target->deltas || !target->delta_count ||
           ((uintptr_t)target->deltas &
            (_Alignof(pvr_chunk_shape_delta_t) - 1u))) {
            errno = EINVAL;
            return -1;
        }
        if(target->delta_count > SIZE_MAX / sizeof(*target->deltas) ||
           target->delta_count * sizeof(*target->deltas) >
           UINTPTR_MAX - (uintptr_t)target->deltas) {
            errno = ERANGE;
            return -1;
        }

        for(delta_index = 0; delta_index < target->delta_count;
            ++delta_index) {
            const pvr_chunk_shape_delta_t *delta =
                target->deltas + delta_index;
            size_t mapped;

            if(delta->reserved || !finite_delta(&delta->delta)) {
                errno = EILSEQ;
                return -1;
            }
            if(delta_index && delta->vertex_index <=
               target->deltas[delta_index - 1u].vertex_index) {
                errno = delta->vertex_index ==
                        target->deltas[delta_index - 1u].vertex_index ?
                        EEXIST : EILSEQ;
                return -1;
            }
            if(plan_slot(plan, delta->vertex_index, &mapped) < 0) {
                if(errno == ENOENT)
                    errno = EILSEQ;
                return -1;
            }
        }
    }

    return 0;
}

int pvr_chunk_shape_bind(const pvr_chunk_model_plan_t *plan,
                         const pvr_chunk_shape_set_t *shapes,
                         uint32_t *dense_lookup,
                         size_t dense_lookup_capacity,
                         pvr_chunk_shape_binding_t *binding) {
    pvr_chunk_shape_requirements_t requirements;
    pvr_chunk_shape_binding_t prepared = { 0 };
    size_t lookup_bytes;
    size_t target_bytes;
    size_t plan_index_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t target_index;
    size_t page;
    size_t dense = 0;

    if(binding)
        memset(binding, 0, sizeof(*binding));
    if(!plan || !shapes || !dense_lookup || !binding) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_shape_query(plan, shapes->target_count, &requirements) < 0 ||
       validate_targets(plan, shapes) < 0)
        return -1;
    if(dense_lookup_capacity < requirements.lookup_entries) {
        errno = ENOSPC;
        return -1;
    }
    if((uintptr_t)dense_lookup & (_Alignof(uint32_t) - 1u)) {
        errno = EINVAL;
        return -1;
    }

    if(multiply_size(requirements.lookup_entries, sizeof(*dense_lookup),
                     &lookup_bytes) < 0 ||
       multiply_size(shapes->target_count, sizeof(*shapes->targets),
                     &target_bytes) < 0 ||
       multiply_size(plan->vertex_index_count, sizeof(*plan->vertex_index),
                     &plan_index_bytes) < 0 ||
       multiply_size(plan->view.model.vertex_word_count,
                     sizeof(*plan->view.model.vertex_words),
                     &vertex_bytes) < 0 ||
       multiply_size(plan->view.model.polygon_word_count,
                     sizeof(*plan->view.model.polygon_words),
                     &polygon_bytes) < 0)
        return -1;

    if(reject_overlap(dense_lookup, lookup_bytes, binding,
                      sizeof(*binding)) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, plan->vertex_index,
                      plan_index_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, shapes->targets,
                      target_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, plan, sizeof(*plan)) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, shapes,
                      sizeof(*shapes)) < 0 ||
       reject_overlap(binding, sizeof(*binding), plan->vertex_index,
                      plan_index_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), shapes->targets,
                      target_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), plan, sizeof(*plan)) < 0 ||
       reject_overlap(binding, sizeof(*binding), shapes,
                      sizeof(*shapes)) < 0)
        return -1;

    for(target_index = 0; target_index < shapes->target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            shapes->targets + target_index;
        size_t delta_bytes;

        if(multiply_size(target->delta_count, sizeof(*target->deltas),
                         &delta_bytes) < 0 ||
           reject_overlap(dense_lookup, lookup_bytes, target->deltas,
                          delta_bytes) < 0 ||
           reject_overlap(binding, sizeof(*binding), target->deltas,
                          delta_bytes) < 0)
            return -1;
    }

    /* Validate the caller-supplied plan itself before the first lookup write.
       The view was re-admitted by query(), but the page map and index entries
       are separate caller-owned data and may have changed independently. */
    for(page = 0; page < PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT; ++page) {
        size_t offset;

        for(offset = 0; offset < PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE; ++offset) {
            pvr_chunk_vertex_attributes_t attributes;
            uint16_t vertex_index;
            size_t mapped;
            int present = plan_vertex_index(plan, page, offset,
                                            &vertex_index, &mapped);

            if(present < 0)
                return -1;
            if(!present)
                continue;
            if(dense >= requirements.source_vertices) {
                errno = EILSEQ;
                return -1;
            }
            if(pvr_chunk_model_plan_vertex_attributes_get(
                   plan, vertex_index, &attributes) < 0) {
                errno = EILSEQ;
                return -1;
            }
            ++dense;
        }
    }
    if(dense != requirements.source_vertices) {
        errno = EILSEQ;
        return -1;
    }

    /* Model admission, plan validation, and every sparse target check precede
       publication, preserving the lookup on all validation failures. */
    for(page = 0; page < requirements.lookup_entries; ++page) {
        dense_lookup[page] = PVR_CHUNK_SHAPE_INDEX_NONE;
    }
    dense = 0;
    for(page = 0; page < PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT; ++page) {
        size_t offset;

        for(offset = 0; offset < PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE; ++offset) {
            uint16_t vertex_index;
            size_t mapped;
            int present = plan_vertex_index(plan, page, offset,
                                            &vertex_index, &mapped);

            if(present < 0) {
                errno = EILSEQ;
                return -1;
            }
            if(present)
                dense_lookup[mapped] = (uint32_t)dense++;
        }
    }

    prepared.plan = *plan;
    prepared.shapes = *shapes;
    prepared.dense_lookup = dense_lookup;
    prepared.dense_lookup_count = requirements.lookup_entries;
    *binding = prepared;
    return 0;
}

int pvr_chunk_shape_source_build(
        const pvr_chunk_shape_binding_t *binding, void *workspace,
        size_t workspace_bytes, pvr_chunk_shape_source_t *source) {
    pvr_chunk_shape_source_t prepared = { 0 };
    pvr_chunk_shape_requirements_t requirements;
    pvr_deform_vertex_t *vertices;
    pvr_morph_delta_t *deltas;
    size_t vertex_bytes;
    size_t delta_count;
    size_t delta_bytes;
    size_t lookup_bytes;
    size_t plan_index_bytes;
    size_t model_vertex_bytes;
    size_t model_polygon_bytes;
    size_t target_bytes;
    size_t page;
    size_t dense = 0;
    size_t target_index;

    if(source)
        *source = prepared;
    if(!binding || !workspace || !source || !binding->dense_lookup ||
       ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_shape_query(&binding->plan,
                             binding->shapes.target_count,
                             &requirements) < 0 ||
       validate_targets(&binding->plan, &binding->shapes) < 0)
        return -1;
    if(binding->dense_lookup_count != requirements.lookup_entries) {
        errno = EILSEQ;
        return -1;
    }
    if(workspace_bytes < requirements.source_bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(multiply_size(requirements.source_vertices,
                     sizeof(pvr_deform_vertex_t), &vertex_bytes) < 0 ||
       multiply_size(requirements.source_vertices,
                     requirements.source_targets, &delta_count) < 0 ||
       multiply_size(delta_count, sizeof(pvr_morph_delta_t),
                     &delta_bytes) < 0 ||
       multiply_size(requirements.lookup_entries,
                     sizeof(*binding->dense_lookup), &lookup_bytes) < 0 ||
       multiply_size(binding->plan.vertex_index_count,
                     sizeof(*binding->plan.vertex_index),
                     &plan_index_bytes) < 0 ||
       multiply_size(binding->plan.view.model.vertex_word_count,
                     sizeof(*binding->plan.view.model.vertex_words),
                     &model_vertex_bytes) < 0 ||
       multiply_size(binding->plan.view.model.polygon_word_count,
                     sizeof(*binding->plan.view.model.polygon_words),
                     &model_polygon_bytes) < 0 ||
       multiply_size(binding->shapes.target_count,
                     sizeof(*binding->shapes.targets), &target_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->dense_lookup, lookup_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.vertex_index, plan_index_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.view.model.vertex_words,
                      model_vertex_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.view.model.polygon_words,
                      model_polygon_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->shapes.targets, target_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding, sizeof(*binding)) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      source, sizeof(*source)) < 0)
        return -1;

    for(target_index = 0; target_index < binding->shapes.target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            binding->shapes.targets + target_index;
        size_t sparse_bytes;

        if(multiply_size(target->delta_count, sizeof(*target->deltas),
                         &sparse_bytes) < 0 ||
           reject_overlap(workspace, requirements.source_bytes,
                          target->deltas, sparse_bytes) < 0)
            return -1;
    }

    vertices = workspace;
    deltas = (pvr_morph_delta_t *)((uint8_t *)workspace + vertex_bytes);
    memset(deltas, 0, delta_bytes);

    for(page = 0; page < PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT; ++page) {
        size_t offset;

        for(offset = 0; offset < PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE; ++offset) {
            pvr_chunk_vertex_attributes_t attributes;
            uint16_t vertex_index;
            size_t mapped;
            int present = plan_vertex_index(&binding->plan, page, offset,
                                            &vertex_index, &mapped);

            if(present < 0)
                return -1;
            if(!present)
                continue;
            if(dense >= requirements.source_vertices ||
               mapped >= binding->dense_lookup_count ||
               binding->dense_lookup[mapped] != dense ||
               pvr_chunk_model_plan_vertex_attributes_get(
                   &binding->plan, vertex_index, &attributes) < 0) {
                errno = EILSEQ;
                return -1;
            }

            vertices[dense].position = attributes.position;
            vertices[dense].position.w = 1.0f;
            if(attributes.present & PVR_CHUNK_VERTEX_ATTR_NORMAL)
                vertices[dense].normal = attributes.normal;
            else {
                vertices[dense].normal.x = 0.0f;
                vertices[dense].normal.y = 0.0f;
                vertices[dense].normal.z = 1.0f;
                vertices[dense].normal.w = 0.0f;
            }
            ++dense;
        }
    }
    if(dense != requirements.source_vertices) {
        errno = EILSEQ;
        return -1;
    }

    for(target_index = 0; target_index < binding->shapes.target_count;
        ++target_index) {
        const pvr_chunk_shape_target_t *target =
            binding->shapes.targets + target_index;
        size_t sparse_index;

        for(sparse_index = 0; sparse_index < target->delta_count;
            ++sparse_index) {
            const pvr_chunk_shape_delta_t *sparse =
                target->deltas + sparse_index;
            size_t mapped;
            uint32_t target_dense;

            if(plan_slot(&binding->plan, sparse->vertex_index,
                         &mapped) < 0 ||
               mapped >= binding->dense_lookup_count) {
                errno = EILSEQ;
                return -1;
            }
            target_dense = binding->dense_lookup[mapped];
            if(target_dense == PVR_CHUNK_SHAPE_INDEX_NONE ||
               target_dense >= requirements.source_vertices) {
                errno = EILSEQ;
                return -1;
            }
            deltas[target_index * requirements.source_vertices +
                   target_dense] = sparse->delta;
        }
    }

    prepared.vertices = vertices;
    prepared.deltas = deltas;
    prepared.vertex_count = requirements.source_vertices;
    prepared.target_count = requirements.source_targets;
    *source = prepared;
    return 0;
}

static int source_valid(const pvr_chunk_shape_source_t *source) {
    size_t delta_count;
    size_t delta_bytes;

    if(!source || !source->vertices || !source->deltas ||
       !source->vertex_count || !source->target_count ||
       ((uintptr_t)source->vertices &
        (_Alignof(pvr_deform_vertex_t) - 1u)) ||
       ((uintptr_t)source->deltas &
        (_Alignof(pvr_morph_delta_t) - 1u))) {
        errno = EINVAL;
        return 0;
    }
    if(multiply_size(source->vertex_count, source->target_count,
                     &delta_count) < 0 ||
       multiply_size(delta_count, sizeof(*source->deltas),
                     &delta_bytes) < 0 ||
       delta_bytes > UINTPTR_MAX - (uintptr_t)source->deltas) {
        errno = ERANGE;
        return 0;
    }
    return 1;
}

int pvr_chunk_shape_motion_bind(
        const pvr_chunk_shape_source_t *source,
        const pvr_chunk_shape_channel_t *channels, size_t channel_count,
        anim_morph_target_tracks_t *tracks, size_t track_capacity) {
    size_t channel_bytes;
    size_t track_bytes;
    size_t vertex_bytes;
    size_t delta_count;
    size_t delta_bytes;
    size_t index;

    if(!source_valid(source))
        return -1;
    if(!channels || !tracks ||
       channel_count != source->target_count ||
       ((uintptr_t)channels &
        (_Alignof(pvr_chunk_shape_channel_t) - 1u)) ||
       ((uintptr_t)tracks &
        (_Alignof(anim_morph_target_tracks_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(track_capacity < channel_count) {
        errno = ENOSPC;
        return -1;
    }
    if(multiply_size(channel_count, sizeof(*channels), &channel_bytes) < 0 ||
       multiply_size(channel_count, sizeof(*tracks), &track_bytes) < 0 ||
       multiply_size(source->vertex_count, sizeof(*source->vertices),
                     &vertex_bytes) < 0 ||
       multiply_size(source->vertex_count, source->target_count,
                     &delta_count) < 0 ||
       multiply_size(delta_count, sizeof(*source->deltas),
                     &delta_bytes) < 0 ||
       reject_overlap(channels, channel_bytes, tracks, track_bytes) < 0 ||
       reject_overlap(tracks, track_bytes, source->vertices,
                      vertex_bytes) < 0 ||
       reject_overlap(tracks, track_bytes, source->deltas,
                      delta_bytes) < 0 ||
       reject_overlap(tracks, track_bytes, source, sizeof(*source)) < 0)
        return -1;

    for(index = 0; index < channel_count; ++index) {
        if(!isfinite(channels[index].fallback_weight)) {
            errno = EILSEQ;
            return -1;
        }
    }
    for(index = 0; index < channel_count; ++index) {
        tracks[index].weight = channels[index].weight;
        tracks[index].fallback.deltas =
            source->deltas + index * source->vertex_count;
        tracks[index].fallback.stride = sizeof(*source->deltas);
        tracks[index].fallback.weight = channels[index].fallback_weight;
    }
    return 0;
}

int pvr_chunk_shape_apply(const pvr_chunk_shape_source_t *source,
                          const pvr_morph_target_t *targets,
                          size_t target_count,
                          pvr_deform_vertex_t *output,
                          size_t output_capacity,
                          pvr_deform_result_t *result) {
    pvr_deform_stream_t base;
    size_t target_index;

    if(result)
        memset(result, 0, sizeof(*result));
    if(!source_valid(source))
        return -1;
    if(!targets || !output ||
       target_count != source->target_count ||
       output_capacity < source->vertex_count) {
        if(output_capacity < (source ? source->vertex_count : 0u))
            errno = ENOSPC;
        else
            errno = EINVAL;
        return -1;
    }

    for(target_index = 0; target_index < target_count; ++target_index) {
        const pvr_morph_delta_t *expected =
            source->deltas + target_index * source->vertex_count;

        if(targets[target_index].deltas != expected ||
           targets[target_index].stride != sizeof(*source->deltas) ||
           !isfinite(targets[target_index].weight)) {
            errno = EINVAL;
            return -1;
        }
    }

    base.vertices = source->vertices;
    base.vertex_count = source->vertex_count;
    base.stride = sizeof(*source->vertices);
    return pvr_morph_apply(output, output_capacity, &base, targets,
                           target_count, result);
}

int pvr_chunk_shape_pose_vertex_get(const pvr_chunk_shape_pose_t *pose,
                                    uint16_t vertex_index,
                                    pvr_deform_vertex_t *vertex) {
    size_t mapped;
    uint32_t dense;

    if(vertex)
        memset(vertex, 0, sizeof(*vertex));
    if(!pose || !pose->binding || !pose->vertices || !vertex ||
       pose->vertex_count != pose->binding->plan.view.info.vertex_entries) {
        errno = EINVAL;
        return -1;
    }
    if(plan_slot(&pose->binding->plan, vertex_index, &mapped) < 0)
        return -1;
    if(mapped >= pose->binding->dense_lookup_count) {
        errno = EILSEQ;
        return -1;
    }

    dense = pose->binding->dense_lookup[mapped];
    if(dense == PVR_CHUNK_SHAPE_INDEX_NONE || dense >= pose->vertex_count) {
        errno = EILSEQ;
        return -1;
    }

    *vertex = pose->vertices[dense];
    return 0;
}
