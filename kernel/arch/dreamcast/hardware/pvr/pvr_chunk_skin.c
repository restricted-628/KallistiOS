/* KallistiOS ##version##

   dc/pvr/pvr_chunk_skin.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skin.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(pvr_chunk_skin_influence_t) == 20u,
               "compact skin influences must occupy 20 bytes");
_Static_assert(sizeof(pvr_chunk_skin_span_t) == 8u,
               "compact skin spans must occupy 8 bytes");
_Static_assert(sizeof(pvr_chunk_skin_weight_t) == 4u,
               "compact skin weights must occupy 4 bytes");

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

    if(!address || bytes > UINTPTR_MAX - first) {
        errno = ERANGE;
        return -1;
    }

    *start = first;
    *end = first + bytes;
    return 0;
}

static int ranges_overlap(uintptr_t first_start, uintptr_t first_end,
                          uintptr_t second_start, uintptr_t second_end) {
    return first_start < second_end && second_start < first_end;
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

int pvr_chunk_skin_query(const pvr_chunk_model_plan_t *plan,
                         pvr_chunk_skin_requirements_t *requirements) {
    pvr_chunk_skin_requirements_t result = { 0 };
    pvr_chunk_model_plan_requirements_t plan_requirements;
    size_t vertex_bytes;
    size_t influence_bytes;
    size_t source_bytes;

    if(requirements)
        *requirements = result;
    if(!plan || !requirements) {
        errno = EINVAL;
        return -1;
    }

    if(pvr_chunk_model_plan_query(&plan->view, &plan_requirements) < 0)
        return -1;
    if(plan->indexed_page_count != plan_requirements.indexed_pages ||
       plan->vertex_index_count != plan_requirements.vertex_index_entries ||
       plan->view.info.vertex_entries == 0) {
        errno = EILSEQ;
        return -1;
    }

    result.alignment = 32u;
    result.lookup_entries = plan->vertex_index_count;
    if(multiply_size(result.lookup_entries, sizeof(uint32_t),
                     &result.lookup_bytes) < 0 ||
       multiply_size(plan->view.info.vertex_entries,
                     sizeof(pvr_deform_vertex_t), &vertex_bytes) < 0 ||
       multiply_size(plan->view.info.vertex_entries,
                     sizeof(pvr_skin_influences_t), &influence_bytes) < 0 ||
       add_size(vertex_bytes, influence_bytes, &source_bytes) < 0 ||
       align_size(source_bytes, result.alignment,
                  &result.source_bytes) < 0)
        return -1;

    result.source_vertices = plan->view.info.vertex_entries;
    *requirements = result;
    return 0;
}

static int validate_influence(const pvr_chunk_skin_influence_t *influence,
                              size_t joint_count) {
    uint32_t total = 0;
    size_t slot;

    if(influence->reserved) {
        errno = EILSEQ;
        return -1;
    }

    for(slot = 0; slot < 4u; ++slot) {
        if(!influence->weight[slot]) {
            if(influence->joint[slot]) {
                errno = EILSEQ;
                return -1;
            }
        }
        else if(influence->joint[slot] >= joint_count) {
            errno = EILSEQ;
            return -1;
        }
        total += influence->weight[slot];
    }

    if(total != PVR_CHUNK_SKIN_WEIGHT_SUM) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

int pvr_chunk_skin_bind(const pvr_chunk_model_plan_t *plan,
                        const pvr_chunk_skin_t *skin,
                        uint32_t *dense_lookup,
                        size_t dense_lookup_capacity,
                        pvr_chunk_skin_binding_t *binding) {
    pvr_chunk_skin_requirements_t requirements;
    pvr_chunk_skin_binding_t prepared;
    size_t influence_bytes;
    size_t lookup_bytes;
    size_t plan_index_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    uintptr_t influence_start;
    uintptr_t influence_end;
    uintptr_t lookup_start;
    uintptr_t lookup_end;
    uintptr_t binding_start;
    uintptr_t binding_end;
    size_t index;

    if(binding)
        memset(binding, 0, sizeof(*binding));
    if(!plan || !skin || !dense_lookup || !binding || !skin->influences ||
       !skin->joint_count || skin->joint_count > UINT16_MAX + 1u) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_query(plan, &requirements) < 0)
        return -1;
    if(skin->influence_count != requirements.source_vertices) {
        errno = EILSEQ;
        return -1;
    }
    if(dense_lookup_capacity < requirements.lookup_entries) {
        errno = ENOSPC;
        return -1;
    }
    if(((uintptr_t)skin->influences &
        (_Alignof(pvr_chunk_skin_influence_t) - 1u)) ||
       ((uintptr_t)dense_lookup & (_Alignof(uint32_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    if(multiply_size(skin->influence_count, sizeof(*skin->influences),
                     &influence_bytes) < 0 ||
       multiply_size(requirements.lookup_entries, sizeof(*dense_lookup),
                     &lookup_bytes) < 0 ||
       multiply_size(plan->vertex_index_count, sizeof(*plan->vertex_index),
                     &plan_index_bytes) < 0 ||
       multiply_size(plan->view.model.vertex_word_count,
                     sizeof(*plan->view.model.vertex_words),
                     &vertex_bytes) < 0 ||
       multiply_size(plan->view.model.polygon_word_count,
                     sizeof(*plan->view.model.polygon_words),
                     &polygon_bytes) < 0 ||
       address_range(skin->influences, influence_bytes,
                     &influence_start, &influence_end) < 0 ||
       address_range(dense_lookup, lookup_bytes,
                     &lookup_start, &lookup_end) < 0 ||
       address_range(binding, sizeof(*binding),
                     &binding_start, &binding_end) < 0)
        return -1;

    if(ranges_overlap(influence_start, influence_end,
                      lookup_start, lookup_end) ||
       ranges_overlap(influence_start, influence_end,
                      binding_start, binding_end) ||
       ranges_overlap(lookup_start, lookup_end,
                      binding_start, binding_end)) {
        errno = EINVAL;
        return -1;
    }
    if(reject_overlap(dense_lookup, lookup_bytes,
                      plan->vertex_index, plan_index_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, plan,
                      sizeof(*plan)) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, plan, sizeof(*plan)) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->vertex_index, plan_index_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), plan,
                      sizeof(*plan)) < 0 ||
       reject_overlap(binding, sizeof(*binding), plan, sizeof(*plan)) < 0)
        return -1;

    /* Complete validation precedes the first caller-owned lookup write. */
    for(index = 0; index < skin->influence_count; ++index) {
        const pvr_chunk_skin_influence_t *influence =
            skin->influences + index;
        pvr_chunk_vertex_attributes_t attributes;
        size_t mapped;

        if((index && influence->vertex_index <=
                     skin->influences[index - 1u].vertex_index) ||
           validate_influence(influence, skin->joint_count) < 0 ||
           plan_slot(plan, influence->vertex_index, &mapped) < 0 ||
           pvr_chunk_model_plan_vertex_attributes_get(
               plan, influence->vertex_index, &attributes) < 0)
            return -1;
    }

    for(index = 0; index < requirements.lookup_entries; ++index)
        dense_lookup[index] = PVR_CHUNK_SKIN_INDEX_NONE;
    for(index = 0; index < skin->influence_count; ++index) {
        size_t mapped;

        if(plan_slot(plan, skin->influences[index].vertex_index,
                     &mapped) < 0) {
            errno = EILSEQ;
            return -1;
        }
        dense_lookup[mapped] = (uint32_t)index;
    }

    memset(&prepared, 0, sizeof(prepared));
    prepared.plan = *plan;
    prepared.skin = *skin;
    prepared.dense_lookup = dense_lookup;
    prepared.dense_lookup_count = requirements.lookup_entries;
    *binding = prepared;
    return 0;
}

int pvr_chunk_skin_source_build(
    const pvr_chunk_skin_binding_t *binding, void *workspace,
    size_t workspace_bytes, pvr_chunk_skin_source_t *source) {
    pvr_chunk_skin_source_t prepared = { 0 };
    pvr_chunk_skin_requirements_t requirements;
    pvr_deform_vertex_t *vertices;
    pvr_skin_influences_t *influences;
    size_t vertex_bytes;
    size_t compact_bytes;
    size_t lookup_bytes;
    size_t plan_index_bytes;
    size_t model_vertex_bytes;
    size_t model_polygon_bytes;
    uintptr_t workspace_start;
    uintptr_t workspace_end;
    uintptr_t compact_start;
    uintptr_t compact_end;
    uintptr_t lookup_start;
    uintptr_t lookup_end;
    size_t index;

    if(source)
        *source = prepared;
    if(!binding || !workspace || !source || !binding->skin.influences ||
       !binding->dense_lookup ||
       ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_query(&binding->plan, &requirements) < 0)
        return -1;
    if(binding->skin.influence_count != requirements.source_vertices ||
       binding->dense_lookup_count != requirements.lookup_entries) {
        errno = EILSEQ;
        return -1;
    }
    if(workspace_bytes < requirements.source_bytes) {
        errno = ENOSPC;
        return -1;
    }

    if(multiply_size(requirements.source_vertices,
                     sizeof(pvr_deform_vertex_t), &vertex_bytes) < 0 ||
       multiply_size(binding->skin.influence_count,
                     sizeof(*binding->skin.influences), &compact_bytes) < 0 ||
       multiply_size(binding->dense_lookup_count,
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
       address_range(workspace, requirements.source_bytes,
                     &workspace_start, &workspace_end) < 0 ||
       address_range(binding->skin.influences, compact_bytes,
                     &compact_start, &compact_end) < 0 ||
       address_range(binding->dense_lookup, lookup_bytes,
                     &lookup_start, &lookup_end) < 0)
        return -1;

    if(ranges_overlap(workspace_start, workspace_end,
                      compact_start, compact_end) ||
       ranges_overlap(workspace_start, workspace_end,
                      lookup_start, lookup_end)) {
        errno = EINVAL;
        return -1;
    }
    if(reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.vertex_index, plan_index_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.view.model.vertex_words,
                      model_vertex_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->plan.view.model.polygon_words,
                      model_polygon_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding, sizeof(*binding)) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      source, sizeof(*source)) < 0)
        return -1;

    vertices = workspace;
    influences = (pvr_skin_influences_t *)
        ((uint8_t *)workspace + vertex_bytes);

    for(index = 0; index < binding->skin.influence_count; ++index) {
        const pvr_chunk_skin_influence_t *compact =
            binding->skin.influences + index;
        pvr_chunk_vertex_attributes_t attributes;
        size_t mapped;
        size_t slot;

        if(validate_influence(compact, binding->skin.joint_count) < 0 ||
           plan_slot(&binding->plan, compact->vertex_index, &mapped) < 0 ||
           mapped >= binding->dense_lookup_count ||
           binding->dense_lookup[mapped] != index ||
           pvr_chunk_model_plan_vertex_attributes_get(
               &binding->plan, compact->vertex_index, &attributes) < 0) {
            errno = EILSEQ;
            return -1;
        }

        vertices[index].position = attributes.position;
        vertices[index].position.w = 1.0f;
        if(attributes.present & PVR_CHUNK_VERTEX_ATTR_NORMAL)
            vertices[index].normal = attributes.normal;
        else {
            vertices[index].normal.x = 0.0f;
            vertices[index].normal.y = 0.0f;
            vertices[index].normal.z = 1.0f;
            vertices[index].normal.w = 0.0f;
        }

        for(slot = 0; slot < 4u; ++slot) {
            influences[index].joint[slot] = compact->joint[slot];
            influences[index].weight[slot] =
                (float)compact->weight[slot] /
                (float)PVR_CHUNK_SKIN_WEIGHT_SUM;
        }
    }

    prepared.vertices = vertices;
    prepared.influences = influences;
    prepared.vertex_count = requirements.source_vertices;
    prepared.joint_count = binding->skin.joint_count;
    *source = prepared;
    return 0;
}

int pvr_chunk_skin_apply(const pvr_chunk_skin_source_t *source,
                         const pvr_skin_palette_t *palette,
                         pvr_deform_vertex_t *output,
                         size_t output_capacity,
                         pvr_deform_result_t *result) {
    pvr_deform_stream_t vertices;
    pvr_skin_stream_t influences;

    if(result)
        memset(result, 0, sizeof(*result));
    if(!source || !palette || !source->vertices || !source->influences ||
       !source->vertex_count || !source->joint_count ||
       palette->joint_count != source->joint_count ||
       output_capacity < source->vertex_count) {
        errno = output_capacity < (source ? source->vertex_count : 0u) ?
                ENOSPC : EINVAL;
        return -1;
    }

    vertices.vertices = source->vertices;
    vertices.vertex_count = source->vertex_count;
    vertices.stride = sizeof(*source->vertices);
    influences.influences = source->influences;
    influences.vertex_count = source->vertex_count;
    influences.stride = sizeof(*source->influences);
    return pvr_skin_apply(output, output_capacity, &vertices, &influences,
                          palette, result);
}

int pvr_chunk_skin_pose_vertex_get(const pvr_chunk_skin_pose_t *pose,
                                   uint16_t vertex_index,
                                   pvr_deform_vertex_t *vertex) {
    size_t mapped;
    uint32_t dense;

    if(vertex)
        memset(vertex, 0, sizeof(*vertex));
    if(!pose || !pose->binding || !pose->vertices || !vertex ||
       pose->vertex_count != pose->binding->skin.influence_count) {
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
    if(dense == PVR_CHUNK_SKIN_INDEX_NONE || dense >= pose->vertex_count) {
        errno = EILSEQ;
        return -1;
    }

    *vertex = pose->vertices[dense];
    return 0;
}

static int general_skin_validate(const pvr_chunk_model_plan_t *plan,
                                 const pvr_chunk_skin_general_t *skin,
                                 size_t expected_vertices) {
    size_t next_weight = 0;
    size_t index;

    if(!plan || !skin || !skin->spans || !skin->weights ||
       !skin->span_count || !skin->weight_count || !skin->joint_count ||
       skin->joint_count > UINT16_MAX + 1u ||
       skin->weight_count > UINT32_MAX ||
       skin->span_count != expected_vertices ||
       ((uintptr_t)skin->spans &
        (_Alignof(pvr_chunk_skin_span_t) - 1u)) ||
       ((uintptr_t)skin->weights &
        (_Alignof(pvr_chunk_skin_weight_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(skin->span_count > SIZE_MAX / sizeof(*skin->spans) ||
       skin->weight_count > SIZE_MAX / sizeof(*skin->weights) ||
       skin->span_count * sizeof(*skin->spans) >
       UINTPTR_MAX - (uintptr_t)skin->spans ||
       skin->weight_count * sizeof(*skin->weights) >
       UINTPTR_MAX - (uintptr_t)skin->weights) {
        errno = ERANGE;
        return -1;
    }

    for(index = 0; index < skin->span_count; ++index) {
        const pvr_chunk_skin_span_t *span = skin->spans + index;
        pvr_chunk_vertex_attributes_t attributes;
        uint32_t total = 0;
        size_t mapped;
        size_t slot;

        if((index && span->vertex_index <=
                     skin->spans[index - 1u].vertex_index) ||
           !span->weight_count || span->first_weight != next_weight ||
           span->weight_count > skin->weight_count - next_weight) {
            errno = EILSEQ;
            return -1;
        }
        if(plan_slot(plan, span->vertex_index, &mapped) < 0 ||
           pvr_chunk_model_plan_vertex_attributes_get(
               plan, span->vertex_index, &attributes) < 0) {
            if(errno == ENOENT)
                errno = EILSEQ;
            return -1;
        }
        for(slot = 0; slot < span->weight_count; ++slot) {
            const pvr_chunk_skin_weight_t *weight =
                skin->weights + next_weight + slot;

            if(!weight->weight || weight->joint >= skin->joint_count) {
                errno = EILSEQ;
                return -1;
            }
            total += weight->weight;
        }
        if(total != PVR_CHUNK_SKIN_WEIGHT_SUM) {
            errno = EILSEQ;
            return -1;
        }
        next_weight += span->weight_count;
    }
    if(next_weight != skin->weight_count) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_skin_general_query(
    const pvr_chunk_model_plan_t *plan,
    const pvr_chunk_skin_general_t *skin,
    pvr_chunk_skin_general_requirements_t *requirements) {
    pvr_chunk_skin_general_requirements_t result = { 0 };
    pvr_chunk_skin_requirements_t base;
    size_t vertex_bytes;
    size_t span_bytes;
    size_t weight_bytes;
    size_t combined;

    if(requirements)
        *requirements = result;
    if(!plan || !skin || !requirements) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_query(plan, &base) < 0 ||
       general_skin_validate(plan, skin, base.source_vertices) < 0)
        return -1;

    result.alignment = 32u;
    result.lookup_entries = base.lookup_entries;
    result.lookup_bytes = base.lookup_bytes;
    result.source_vertices = base.source_vertices;
    result.source_spans = skin->span_count;
    result.source_weights = skin->weight_count;
    if(multiply_size(result.source_vertices, sizeof(pvr_deform_vertex_t),
                     &vertex_bytes) < 0 ||
       multiply_size(result.source_spans, sizeof(pvr_skin_span_t),
                     &span_bytes) < 0 ||
       multiply_size(result.source_weights, sizeof(pvr_skin_weight_t),
                     &weight_bytes) < 0 ||
       add_size(vertex_bytes, span_bytes, &combined) < 0 ||
       add_size(combined, weight_bytes, &combined) < 0 ||
       align_size(combined, result.alignment, &result.source_bytes) < 0)
        return -1;

    *requirements = result;
    return 0;
}

int pvr_chunk_skin_general_bind(
    const pvr_chunk_model_plan_t *plan,
    const pvr_chunk_skin_general_t *skin,
    uint32_t *dense_lookup, size_t dense_lookup_capacity,
    pvr_chunk_skin_general_binding_t *binding) {
    pvr_chunk_skin_general_requirements_t requirements;
    pvr_chunk_skin_general_binding_t prepared;
    size_t lookup_bytes;
    size_t span_bytes;
    size_t weight_bytes;
    size_t index_bytes;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t index;

    if(binding)
        memset(binding, 0, sizeof(*binding));
    if(!plan || !skin || !dense_lookup || !binding ||
       ((uintptr_t)dense_lookup & (_Alignof(uint32_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_general_query(plan, skin, &requirements) < 0)
        return -1;
    if(dense_lookup_capacity < requirements.lookup_entries) {
        errno = ENOSPC;
        return -1;
    }
    if(multiply_size(requirements.lookup_entries, sizeof(*dense_lookup),
                     &lookup_bytes) < 0 ||
       multiply_size(skin->span_count, sizeof(*skin->spans),
                     &span_bytes) < 0 ||
       multiply_size(skin->weight_count, sizeof(*skin->weights),
                     &weight_bytes) < 0 ||
       multiply_size(plan->vertex_index_count, sizeof(*plan->vertex_index),
                     &index_bytes) < 0 ||
       multiply_size(plan->view.model.vertex_word_count,
                     sizeof(*plan->view.model.vertex_words),
                     &vertex_bytes) < 0 ||
       multiply_size(plan->view.model.polygon_word_count,
                     sizeof(*plan->view.model.polygon_words),
                     &polygon_bytes) < 0)
        return -1;

    if(reject_overlap(dense_lookup, lookup_bytes, binding,
                      sizeof(*binding)) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, skin->spans,
                      span_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, skin->weights,
                      weight_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes, plan->vertex_index,
                      index_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(dense_lookup, lookup_bytes,
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), skin->spans,
                      span_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), skin->weights,
                      weight_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding), plan->vertex_index,
                      index_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.vertex_words, vertex_bytes) < 0 ||
       reject_overlap(binding, sizeof(*binding),
                      plan->view.model.polygon_words, polygon_bytes) < 0 ||
       reject_overlap(skin->spans, span_bytes, skin->weights,
                      weight_bytes) < 0)
        return -1;

    for(index = 0; index < requirements.lookup_entries; ++index)
        dense_lookup[index] = PVR_CHUNK_SKIN_INDEX_NONE;
    for(index = 0; index < skin->span_count; ++index) {
        size_t mapped;

        if(plan_slot(plan, skin->spans[index].vertex_index, &mapped) < 0) {
            errno = EILSEQ;
            return -1;
        }
        dense_lookup[mapped] = (uint32_t)index;
    }

    memset(&prepared, 0, sizeof(prepared));
    prepared.plan = *plan;
    prepared.skin = *skin;
    prepared.dense_lookup = dense_lookup;
    prepared.dense_lookup_count = requirements.lookup_entries;
    *binding = prepared;
    return 0;
}

int pvr_chunk_skin_general_source_build(
    const pvr_chunk_skin_general_binding_t *binding,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_skin_general_source_t *source) {
    pvr_chunk_skin_general_source_t prepared = { 0 };
    pvr_chunk_skin_general_requirements_t requirements;
    pvr_deform_vertex_t *vertices;
    pvr_skin_span_t *spans;
    pvr_skin_weight_t *weights;
    size_t vertex_bytes;
    size_t span_bytes;
    size_t compact_span_bytes;
    size_t compact_weight_bytes;
    size_t lookup_bytes;
    size_t plan_index_bytes;
    size_t model_vertex_bytes;
    size_t model_polygon_bytes;
    size_t index;

    if(source)
        *source = prepared;
    if(!binding || !workspace || !source || !binding->dense_lookup ||
       ((uintptr_t)workspace & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skin_general_query(&binding->plan, &binding->skin,
                                    &requirements) < 0)
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
       multiply_size(requirements.source_spans, sizeof(pvr_skin_span_t),
                     &span_bytes) < 0 ||
       multiply_size(binding->skin.span_count,
                     sizeof(*binding->skin.spans),
                     &compact_span_bytes) < 0 ||
       multiply_size(binding->skin.weight_count,
                     sizeof(*binding->skin.weights),
                     &compact_weight_bytes) < 0 ||
       multiply_size(binding->dense_lookup_count,
                     sizeof(*binding->dense_lookup), &lookup_bytes) < 0 ||
       multiply_size(binding->plan.vertex_index_count,
                     sizeof(*binding->plan.vertex_index),
                     &plan_index_bytes) < 0 ||
       multiply_size(binding->plan.view.model.vertex_word_count,
                     sizeof(*binding->plan.view.model.vertex_words),
                     &model_vertex_bytes) < 0 ||
       multiply_size(binding->plan.view.model.polygon_word_count,
                     sizeof(*binding->plan.view.model.polygon_words),
                     &model_polygon_bytes) < 0)
        return -1;

    if(reject_overlap(workspace, requirements.source_bytes,
                      binding->skin.spans, compact_span_bytes) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      binding->skin.weights, compact_weight_bytes) < 0 ||
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
                      binding, sizeof(*binding)) < 0 ||
       reject_overlap(workspace, requirements.source_bytes,
                      source, sizeof(*source)) < 0)
        return -1;

    vertices = workspace;
    spans = (pvr_skin_span_t *)((uint8_t *)workspace + vertex_bytes);
    weights = (pvr_skin_weight_t *)((uint8_t *)spans + span_bytes);

    for(index = 0; index < binding->skin.span_count; ++index) {
        const pvr_chunk_skin_span_t *compact = binding->skin.spans + index;
        pvr_chunk_vertex_attributes_t attributes;
        size_t mapped;
        size_t slot;

        if(plan_slot(&binding->plan, compact->vertex_index, &mapped) < 0 ||
           mapped >= binding->dense_lookup_count ||
           binding->dense_lookup[mapped] != index ||
           pvr_chunk_model_plan_vertex_attributes_get(
               &binding->plan, compact->vertex_index, &attributes) < 0) {
            errno = EILSEQ;
            return -1;
        }

        vertices[index].position = attributes.position;
        vertices[index].position.w = 1.0f;
        if(attributes.present & PVR_CHUNK_VERTEX_ATTR_NORMAL)
            vertices[index].normal = attributes.normal;
        else {
            vertices[index].normal.x = 0.0f;
            vertices[index].normal.y = 0.0f;
            vertices[index].normal.z = 1.0f;
            vertices[index].normal.w = 0.0f;
        }

        spans[index].first_weight = compact->first_weight;
        spans[index].weight_count = compact->weight_count;
        spans[index].reserved = 0;
        for(slot = 0; slot < compact->weight_count; ++slot) {
            size_t weight_index = compact->first_weight + slot;

            weights[weight_index].joint =
                binding->skin.weights[weight_index].joint;
            weights[weight_index].reserved = 0;
            weights[weight_index].weight =
                (float)binding->skin.weights[weight_index].weight /
                (float)PVR_CHUNK_SKIN_WEIGHT_SUM;
        }
    }

    prepared.vertices = vertices;
    prepared.spans = spans;
    prepared.weights = weights;
    prepared.vertex_count = requirements.source_vertices;
    prepared.weight_count = requirements.source_weights;
    prepared.joint_count = binding->skin.joint_count;
    *source = prepared;
    return 0;
}

int pvr_chunk_skin_general_apply(
    const pvr_chunk_skin_general_source_t *source,
    const pvr_skin_palette_t *palette,
    pvr_deform_vertex_t *output, size_t output_capacity,
    pvr_deform_result_t *result) {
    pvr_deform_stream_t vertices;
    pvr_skin_span_stream_t influences;

    if(result)
        memset(result, 0, sizeof(*result));
    if(!source || !palette || !source->vertices || !source->spans ||
       !source->weights || !source->vertex_count || !source->weight_count ||
       !source->joint_count || palette->joint_count != source->joint_count ||
       output_capacity < source->vertex_count) {
        errno = output_capacity < (source ? source->vertex_count : 0u) ?
                ENOSPC : EINVAL;
        return -1;
    }

    vertices.vertices = source->vertices;
    vertices.vertex_count = source->vertex_count;
    vertices.stride = sizeof(*source->vertices);
    influences.spans = source->spans;
    influences.vertex_count = source->vertex_count;
    influences.stride = sizeof(*source->spans);
    influences.weights = source->weights;
    influences.weight_count = source->weight_count;
    return pvr_skin_apply_spans(output, output_capacity, &vertices,
                                &influences, palette, result);
}

int pvr_chunk_skin_general_pose_vertex_get(
    const pvr_chunk_skin_general_pose_t *pose,
    uint16_t vertex_index, pvr_deform_vertex_t *vertex) {
    size_t mapped;
    uint32_t dense;

    if(vertex)
        memset(vertex, 0, sizeof(*vertex));
    if(!pose || !pose->binding || !pose->vertices || !vertex ||
       pose->vertex_count != pose->binding->skin.span_count) {
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
    if(dense == PVR_CHUNK_SKIN_INDEX_NONE || dense >= pose->vertex_count) {
        errno = EILSEQ;
        return -1;
    }
    *vertex = pose->vertices[dense];
    return 0;
}
