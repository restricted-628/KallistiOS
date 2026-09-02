/* KallistiOS ##version##

   dc/pvr/pvr_chunk_scene.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_scene.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_CRC_OFFSET = 28,
    HEADER_CRC_BYTES = 28,
    NODE_PARENT_OFFSET = 0,
    NODE_MODEL_OFFSET = 4,
    NODE_FLAGS_OFFSET = 8,
    NODE_RESERVED_OFFSET = 12,
    NODE_MATRIX_OFFSET = 16
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_le_float(const uint8_t *bytes) {
    uint32_t word = read_le32(bytes);
    float value;

    memcpy(&value, &word, sizeof(value));
    return value;
}

static uint32_t crc32_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    for(index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
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

static int decode_node(const uint8_t *record, size_t index, uint16_t version,
                       pvr_chunk_scene_node_t *node) {
    uint32_t parent = read_le32(record + NODE_PARENT_OFFSET);
    uint32_t model = read_le32(record + NODE_MODEL_OFFSET);
    uint32_t flags = read_le32(record + NODE_FLAGS_OFFSET);
    size_t component;

    if((version == PVR_CHUNK_SCENE_HIERARCHY_VERSION_1 && flags) ||
       (flags & ~PVR_CHUNK_NODE_FLAGS_MASK) ||
       read_le32(record + NODE_RESERVED_OFFSET) ||
       (parent != UINT32_MAX && parent >= index)) {
        errno = EILSEQ;
        return -1;
    }

    node->parent_index = parent == UINT32_MAX ? PVR_CHUNK_NODE_NONE : parent;
    node->model_ordinal = model == PVR_CHUNK_SCENE_MODEL_NONE ?
                          PVR_CHUNK_NODE_NONE : model;
    node->flags = flags;
    for(component = 0; component < 16; ++component) {
        float value = read_le_float(
            record + NODE_MATRIX_OFFSET + component * sizeof(uint32_t));

        if(!isfinite(value)) {
            errno = EILSEQ;
            return -1;
        }
        ((float *)node->local_transform)[component] = value;
    }
    return 0;
}

int pvr_chunk_scene_hierarchy_open(
    const void *data, size_t size,
    pvr_chunk_scene_hierarchy_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_scene_hierarchy_view_t parsed;
    pvr_chunk_scene_node_t node;
    uint32_t file_bytes;
    uint32_t node_count;
    uint16_t version;
    uint16_t node_stride;
    size_t node_bytes;
    size_t index;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_SCENE_HIERARCHY_MAGIC ||
       read_le16(bytes + 6) != PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES ||
       read_le16(bytes + 18) || read_le32(bytes + 24) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    version = read_le16(bytes + 4);
    if(version != PVR_CHUNK_SCENE_HIERARCHY_VERSION_1 &&
       version != PVR_CHUNK_SCENE_HIERARCHY_VERSION) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    node_count = read_le32(bytes + 12);
    node_stride = read_le16(bytes + 16);
    if(file_bytes != size ||
       file_bytes < PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES ||
       node_stride != PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES ||
       node_count > (SIZE_MAX - PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES) /
                        node_stride) {
        errno = EILSEQ;
        return -1;
    }
    node_bytes = (size_t)node_count * node_stride;
    if(node_bytes !=
           file_bytes - PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES ||
       read_le32(bytes + 20) != crc32_bytes(
           bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES, node_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    for(index = 0; index < node_count; ++index) {
        if(decode_node(bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES +
                           index * node_stride,
                       index, version, &node) < 0)
            return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.nodes = bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES;
    parsed.node_count = node_count;
    parsed.node_stride = node_stride;
    parsed.version = version;
    *view = parsed;
    return 0;
}

int pvr_chunk_scene_hierarchy_node_get(
    const pvr_chunk_scene_hierarchy_view_t *view, size_t index,
    pvr_chunk_scene_node_t *node) {
    pvr_chunk_scene_hierarchy_view_t checked;

    if(node)
        memset(node, 0, sizeof(*node));
    if(!view || !node || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_scene_hierarchy_open(view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.node_count) {
        errno = ENOENT;
        return -1;
    }
    return decode_node(
        (const uint8_t *)checked.nodes + index * checked.node_stride,
        index, checked.version, node);
}

static int hierarchy_bind(
    const pvr_chunk_scene_hierarchy_view_t *view,
    const pvr_chunk_model_view_t *const *model_pointers,
    const pvr_chunk_model_view_t *model_array, size_t model_count,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy) {
    pvr_chunk_scene_hierarchy_view_t checked;
    size_t output_bytes;
    size_t index;

    if(hierarchy)
        memset(hierarchy, 0, sizeof(*hierarchy));
    if(!view || !hierarchy || !view->data ||
       (model_count && !model_pointers && !model_array)) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_scene_hierarchy_open(view->data, view->size, &checked) < 0)
        return -1;
    if(checked.node_count && !nodes) {
        errno = EINVAL;
        return -1;
    }
    if(node_capacity < checked.node_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.node_count > SIZE_MAX / sizeof(*nodes)) {
        errno = EOVERFLOW;
        return -1;
    }
    output_bytes = checked.node_count * sizeof(*nodes);
    if(ranges_overlap(nodes, output_bytes, checked.data, checked.size)) {
        errno = EINVAL;
        return -1;
    }

    /* Validate every model reference before publishing or modifying output. */
    for(index = 0; index < checked.node_count; ++index) {
        pvr_chunk_scene_node_t node;

        if(decode_node((const uint8_t *)checked.nodes +
                           index * checked.node_stride,
                       index, checked.version, &node) < 0)
            return -1;
        if(node.model_ordinal != PVR_CHUNK_NODE_NONE &&
           (node.model_ordinal >= model_count ||
            (!model_array && !model_pointers[node.model_ordinal]))) {
            errno = EILSEQ;
            return -1;
        }
    }

    for(index = 0; index < checked.node_count; ++index) {
        pvr_chunk_scene_node_t node;

        if(decode_node((const uint8_t *)checked.nodes +
                           index * checked.node_stride,
                       index, checked.version, &node) < 0)
            return -1;
        nodes[index].model = node.model_ordinal == PVR_CHUNK_NODE_NONE ?
                             NULL : (model_array ?
                                 &model_array[node.model_ordinal] :
                                 model_pointers[node.model_ordinal]);
        memcpy(&nodes[index].local_transform, &node.local_transform,
               sizeof(matrix_t));
        nodes[index].parent_index = node.parent_index;
        nodes[index].user_data = NULL;
        nodes[index].flags = node.flags;
    }
    hierarchy->nodes = nodes;
    hierarchy->node_count = checked.node_count;
    return 0;
}

int pvr_chunk_scene_hierarchy_bind(
    const pvr_chunk_scene_hierarchy_view_t *view,
    const pvr_chunk_model_view_t *const *models, size_t model_count,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy) {
    return hierarchy_bind(view, models, NULL, model_count, nodes,
                          node_capacity, hierarchy);
}

int pvr_chunk_scene_hierarchy_bind_models(
    const pvr_chunk_scene_hierarchy_view_t *view,
    const pvr_chunk_model_view_t *models, size_t model_count,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy) {
    return hierarchy_bind(view, NULL, models, model_count, nodes,
                          node_capacity, hierarchy);
}
