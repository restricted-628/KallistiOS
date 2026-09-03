/* KallistiOS ##version##

   dc/pvr/pvr_chunk_scene_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_scene.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    NODE_MODEL_OFFSET = 4,
    MODEL_VERTEX_OFFSET = 0,
    MODEL_POLYGON_OFFSET = 4
};

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int ranges_overlap(const void *left, size_t left_bytes,
                          const void *right, size_t right_bytes) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if(!left_bytes || !right_bytes)
        return 0;
    if(left_start > UINTPTR_MAX - left_bytes ||
       right_start > UINTPTR_MAX - right_bytes)
        return 1;
    return left_start < right_start + right_bytes &&
           right_start < left_start + left_bytes;
}

static int align_size(size_t value, size_t alignment, size_t *result) {
    size_t mask;

    if(!alignment || (alignment & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }
    mask = alignment - 1u;
    if(value > SIZE_MAX - mask) {
        errno = EOVERFLOW;
        return -1;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

static int stream_section_index(
    const pvr_chunk_asset_view_t *asset, uint32_t type, size_t ordinal,
    size_t *section_index, pvr_chunk_asset_section_t *section) {
    size_t matched = 0;
    size_t index;

    for(index = 0; index < asset->section_count; ++index) {
        pvr_chunk_asset_section_t candidate;

        if(pvr_chunk_asset_section_get(asset, index, &candidate) < 0)
            return -1;
        if(candidate.type != type)
            continue;
        if(matched++ == ordinal) {
            *section_index = index;
            *section = candidate;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int stream_first_model(
    const pvr_chunk_model_table_view_t *table, size_t model,
    uint32_t type, size_t ordinal, size_t *first_model) {
    size_t field_offset =
        type == PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM ?
        MODEL_VERTEX_OFFSET : MODEL_POLYGON_OFFSET;
    size_t previous;

    /* Scene materialization is a one-shot, allocation-free operation. Walking
       the already admitted fixed-width records avoids requiring a caller-owned
       ordinal map or revalidating the complete table for every comparison. */
    for(previous = 0; previous < model; ++previous) {
        const uint8_t *record =
            (const uint8_t *)table->records +
            previous * table->record_stride;

        if(read_le32(record + field_offset) == ordinal) {
            *first_model = previous;
            return 0;
        }
    }
    *first_model = model;
    return 0;
}

static int workspace_add_stream(
    const pvr_chunk_scene_asset_view_t *view, size_t model,
    uint32_t type, size_t ordinal, size_t *cursor) {
    pvr_chunk_asset_section_workspace_requirements_t section_requirements;
    pvr_chunk_asset_section_t section;
    size_t section_index;
    size_t first_model;
    size_t offset;

    if(stream_first_model(
           &view->model_table, model, type, ordinal, &first_model) < 0)
        return -1;
    if(first_model != model)
        return 0;
    if(stream_section_index(
           &view->asset, type, ordinal, &section_index, &section) < 0 ||
       pvr_chunk_asset_section_workspace_query(
           &view->asset, section_index, &section_requirements) < 0)
        return -1;
    if(!section_requirements.bytes)
        return 0;
    if(align_size(*cursor, section_requirements.alignment, &offset) < 0 ||
       section_requirements.bytes > SIZE_MAX - offset) {
        errno = EOVERFLOW;
        return -1;
    }
    *cursor = offset + section_requirements.bytes;
    return 0;
}

static int load_unique_direct_section(
    const pvr_chunk_asset_view_t *asset, uint32_t type,
    const void **decoded, size_t *decoded_bytes) {
    pvr_chunk_asset_section_t selected;
    size_t selected_index = 0;
    size_t matches = 0;
    size_t index;

    *decoded = NULL;
    *decoded_bytes = 0;
    for(index = 0; index < asset->section_count; ++index) {
        pvr_chunk_asset_section_t section;

        if(pvr_chunk_asset_section_get(asset, index, &section) < 0)
            return -1;
        if(section.type == type) {
            selected = section;
            selected_index = index;
            ++matches;
        }
    }
    if(matches != 1u) {
        errno = matches ? EILSEQ : ENOENT;
        return -1;
    }
    {
        pvr_chunk_asset_section_workspace_requirements_t requirements;

        if(pvr_chunk_asset_section_workspace_query(
               asset, selected_index, &requirements) < 0)
            return -1;
        if(requirements.bytes ||
           selected.codec != PVR_CHUNK_ASSET_CODEC_RAW) {
            errno = ENOTSUP;
            return -1;
        }
    }
    if(pvr_chunk_asset_section_load(
           asset, selected_index, NULL, NULL, NULL, 0, decoded) < 0)
        return -1;
    *decoded_bytes = selected.decoded_bytes;
    return 0;
}

int pvr_chunk_scene_asset_open(
    const pvr_chunk_asset_view_t *asset,
    pvr_chunk_scene_asset_view_t *view) {
    pvr_chunk_scene_asset_view_t parsed;
    pvr_chunk_asset_view_t checked_asset;
    const void *table_data;
    const void *hierarchy_data;
    size_t table_bytes;
    size_t hierarchy_bytes;
    size_t index;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!asset || !view || !asset->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(
           asset->data, asset->size, &checked_asset) < 0)
        return -1;
    if(checked_asset.version != PVR_CHUNK_ASSET_DIRECTORY_VERSION) {
        errno = ENOTSUP;
        return -1;
    }
    if(load_unique_direct_section(
           &checked_asset, PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
           &table_data, &table_bytes) < 0 ||
       load_unique_direct_section(
           &checked_asset, PVR_CHUNK_ASSET_SECTION_HIERARCHY,
           &hierarchy_data, &hierarchy_bytes) < 0)
        return -1;

    memset(&parsed, 0, sizeof(parsed));
    if(pvr_chunk_model_table_open(
           table_data, table_bytes, &parsed.model_table) < 0 ||
       pvr_chunk_model_table_validate_asset(
           &parsed.model_table, &checked_asset) < 0 ||
       pvr_chunk_scene_hierarchy_open(
           hierarchy_data, hierarchy_bytes, &parsed.hierarchy) < 0)
        return -1;

    /* The admitted hierarchy makes fixed-width model fields safe to read. */
    for(index = 0; index < parsed.hierarchy.node_count; ++index) {
        const uint8_t *record =
            (const uint8_t *)parsed.hierarchy.nodes +
            index * parsed.hierarchy.node_stride;
        uint32_t model = read_le32(record + NODE_MODEL_OFFSET);

        if(model != PVR_CHUNK_SCENE_MODEL_NONE &&
           model >= parsed.model_table.model_count) {
            errno = EILSEQ;
            return -1;
        }
    }

    parsed.asset = checked_asset;
    parsed.model_count = parsed.model_table.model_count;
    parsed.node_count = parsed.hierarchy.node_count;
    *view = parsed;
    return 0;
}

int pvr_chunk_scene_asset_workspace_query(
    const pvr_chunk_scene_asset_view_t *view,
    pvr_chunk_scene_asset_workspace_requirements_t *requirements) {
    pvr_chunk_scene_asset_view_t checked;
    pvr_chunk_scene_asset_workspace_requirements_t result;
    size_t cursor = 0;
    size_t model;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!view || !requirements || !view->asset.data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_scene_asset_open(&view->asset, &checked) < 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.alignment = PVR_CHUNK_ASSET_ALIGNMENT;
    for(model = 0; model < checked.model_count; ++model) {
        pvr_chunk_model_table_record_t record;

        if(pvr_chunk_model_table_record_get(
               &checked.model_table, model, &record) < 0 ||
           workspace_add_stream(
               &checked, model, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
               record.vertex_ordinal, &cursor) < 0 ||
           workspace_add_stream(
               &checked, model, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
               record.polygon_ordinal, &cursor) < 0)
            return -1;
    }
    result.bytes = cursor;
    *requirements = result;
    return 0;
}

static int load_scene_stream(
    const pvr_chunk_scene_asset_view_t *view,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    const pvr_chunk_model_view_t *models, size_t model,
    uint32_t type, size_t ordinal, size_t *cursor,
    const void **decoded, size_t *decoded_bytes) {
    pvr_chunk_asset_section_workspace_requirements_t section_requirements;
    pvr_chunk_asset_section_t section;
    size_t section_index;
    size_t first_model;
    size_t offset = *cursor;
    void *section_workspace = NULL;

    if(stream_first_model(
           &view->model_table, model, type, ordinal, &first_model) < 0)
        return -1;
    if(first_model != model) {
        if(type == PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM) {
            *decoded = models[first_model].model.vertex_words;
            *decoded_bytes = models[first_model].model.vertex_word_count *
                             sizeof(uint32_t);
        }
        else {
            *decoded = models[first_model].model.polygon_words;
            *decoded_bytes = models[first_model].model.polygon_word_count *
                             sizeof(uint16_t);
        }
        return 0;
    }
    if(stream_section_index(
           &view->asset, type, ordinal, &section_index, &section) < 0 ||
       pvr_chunk_asset_section_workspace_query(
           &view->asset, section_index, &section_requirements) < 0)
        return -1;
    if(section_requirements.bytes) {
        if(align_size(*cursor, section_requirements.alignment, &offset) < 0 ||
           offset > workspace_bytes ||
           section_requirements.bytes > workspace_bytes - offset) {
            errno = ENOSPC;
            return -1;
        }
        section_workspace = (uint8_t *)workspace + offset;
    }
    if(pvr_chunk_asset_section_load(
           &view->asset, section_index, decoder, decoder_data,
           section_workspace, section_requirements.bytes, decoded) < 0)
        return -1;
    *decoded_bytes = section.decoded_bytes;
    if(section_requirements.bytes)
        *cursor = offset + section_requirements.bytes;
    return 0;
}

int pvr_chunk_scene_asset_load(
    const pvr_chunk_scene_asset_view_t *view,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *models, size_t model_capacity,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy) {
    pvr_chunk_scene_asset_view_t checked;
    pvr_chunk_scene_asset_workspace_requirements_t requirements;
    size_t model_bytes;
    size_t node_bytes;
    size_t cursor = 0;
    size_t model;
    int saved_errno;

    if(hierarchy)
        memset(hierarchy, 0, sizeof(*hierarchy));
    if(!view || !hierarchy || !view->asset.data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_scene_asset_open(&view->asset, &checked) < 0 ||
       pvr_chunk_scene_asset_workspace_query(
           &checked, &requirements) < 0)
        return -1;
    if((checked.model_count && !models) ||
       (checked.node_count && !nodes)) {
        errno = EINVAL;
        return -1;
    }
    if(model_capacity < checked.model_count ||
       node_capacity < checked.node_count ||
       workspace_bytes < requirements.bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.model_count > SIZE_MAX / sizeof(*models) ||
       checked.node_count > SIZE_MAX / sizeof(*nodes)) {
        errno = EOVERFLOW;
        return -1;
    }
    model_bytes = checked.model_count * sizeof(*models);
    node_bytes = checked.node_count * sizeof(*nodes);
    if((requirements.bytes &&
        (!workspace || ((uintptr_t)workspace &
                        (requirements.alignment - 1u)))) ||
       ranges_overlap(models, model_bytes,
                      checked.asset.data, checked.asset.size) ||
       ranges_overlap(nodes, node_bytes,
                      checked.asset.data, checked.asset.size) ||
       ranges_overlap(models, model_bytes, nodes, node_bytes) ||
       ranges_overlap(workspace, requirements.bytes,
                      checked.asset.data, checked.asset.size) ||
       ranges_overlap(workspace, requirements.bytes,
                      models, model_bytes) ||
       ranges_overlap(workspace, requirements.bytes,
                      nodes, node_bytes)) {
        errno = EINVAL;
        return -1;
    }

    memset(models, 0, model_bytes);
    if(node_bytes)
        memset(nodes, 0, node_bytes);
    for(model = 0; model < checked.model_count; ++model) {
        pvr_chunk_model_table_record_t record;
        pvr_chunk_model_t source;
        const void *vertex;
        const void *polygon;
        size_t vertex_bytes;
        size_t polygon_bytes;

        if(pvr_chunk_model_table_record_get(
               &checked.model_table, model, &record) < 0 ||
           load_scene_stream(
               &checked, decoder, decoder_data, workspace,
               workspace_bytes, models, model,
               PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
               record.vertex_ordinal, &cursor,
               &vertex, &vertex_bytes) < 0 ||
           load_scene_stream(
               &checked, decoder, decoder_data, workspace,
               workspace_bytes, models, model,
               PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
               record.polygon_ordinal, &cursor,
               &polygon, &polygon_bytes) < 0)
            goto fail;
        memset(&source, 0, sizeof(source));
        source.vertex_words = vertex;
        source.vertex_word_count = vertex_bytes / sizeof(uint32_t);
        source.polygon_words = polygon;
        source.polygon_word_count = polygon_bytes / sizeof(uint16_t);
        memcpy(source.center, record.center, sizeof(source.center));
        source.radius = record.radius;
        if(pvr_chunk_model_open(&source, &models[model]) < 0)
            goto fail;
        /* Canonical scene assets publish only runtime-ready model streams.
           Import-only execution controls must be resolved by the host before
           a hierarchy can retain the model view. */
        if(models[model].info.requirements) {
            errno = ENOTSUP;
            goto fail;
        }
    }
    if(cursor != requirements.bytes) {
        errno = EILSEQ;
        goto fail;
    }
    if(pvr_chunk_scene_hierarchy_bind_models(
           &checked.hierarchy, models, checked.model_count,
           nodes, checked.node_count, hierarchy) < 0)
        goto fail;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    memset(models, 0, model_bytes);
    if(node_bytes)
        memset(nodes, 0, node_bytes);
    memset(hierarchy, 0, sizeof(*hierarchy));
    errno = saved_errno;
    return -1;
}
