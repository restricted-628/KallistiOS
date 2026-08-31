/* KallistiOS ##version##

   Host-side canonical scene representation for compact assets.
   Copyright (C) 2026 Joseph Black
*/

#include "pvr-scene-ir.h"

#include <dc/pvr_chunk_scene.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static void store_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void store_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    store_le32(bytes, word);
}

void pvr_scene_ir_free(pvr_scene_ir_t *scene) {
    if(!scene)
        return;
    free(scene->nodes);
    memset(scene, 0, sizeof(*scene));
}

int pvr_scene_ir_add_node(pvr_scene_ir_t *scene, uint32_t parent_index,
                          uint32_t model_ordinal,
                          const float local_transform[16]) {
    pvr_scene_ir_node_t *resized;
    size_t component;
    size_t capacity;

    if(!scene || !local_transform || scene->node_count >= UINT32_MAX ||
       (parent_index != UINT32_MAX && parent_index >= scene->node_count)) {
        errno = EINVAL;
        return -1;
    }
    for(component = 0; component < 16; ++component) {
        if(!isfinite(local_transform[component])) {
            errno = EDOM;
            return -1;
        }
    }
    if(scene->node_count == scene->node_capacity) {
        capacity = scene->node_capacity ? scene->node_capacity * 2u : 8u;
        if(capacity < scene->node_capacity ||
           capacity > SIZE_MAX / sizeof(*scene->nodes)) {
            errno = EOVERFLOW;
            return -1;
        }
        resized = realloc(scene->nodes, capacity * sizeof(*scene->nodes));
        if(!resized) {
            errno = ENOMEM;
            return -1;
        }
        scene->nodes = resized;
        scene->node_capacity = capacity;
    }

    scene->nodes[scene->node_count].parent_index = parent_index;
    scene->nodes[scene->node_count].model_ordinal = model_ordinal;
    memcpy(scene->nodes[scene->node_count].local_transform,
           local_transform, 16u * sizeof(float));
    ++scene->node_count;
    return 0;
}

int pvr_scene_ir_add_root_model(pvr_scene_ir_t *scene,
                                uint32_t model_ordinal) {
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return pvr_scene_ir_add_node(scene, UINT32_MAX, model_ordinal, identity);
}

int pvr_scene_ir_validate(const pvr_scene_ir_t *scene) {
    size_t index;

    if(!scene || (scene->node_count && !scene->nodes) ||
       scene->node_count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < scene->node_count; ++index) {
        const pvr_scene_ir_node_t *node = scene->nodes + index;
        size_t component;

        if(node->parent_index != UINT32_MAX && node->parent_index >= index) {
            errno = EILSEQ;
            return -1;
        }
        for(component = 0; component < 16; ++component) {
            if(!isfinite(node->local_transform[component])) {
                errno = EILSEQ;
                return -1;
            }
        }
    }
    return 0;
}

int pvr_scene_ir_serialize_hierarchy(const pvr_scene_ir_t *scene,
                                     uint8_t **bytes_out,
                                     size_t *size_out) {
    uint8_t *bytes;
    size_t node_bytes;
    size_t file_bytes;
    size_t index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!bytes_out || !size_out) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_scene_ir_validate(scene) < 0)
        return -1;
    if(scene->node_count >
       (SIZE_MAX - PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES) /
           PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES) {
        errno = EOVERFLOW;
        return -1;
    }
    node_bytes = scene->node_count * PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES;
    file_bytes = PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES + node_bytes;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(index = 0; index < scene->node_count; ++index) {
        const pvr_scene_ir_node_t *node = scene->nodes + index;
        uint8_t *record = bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES +
                          index * PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES;
        size_t component;

        store_le32(record, node->parent_index);
        store_le32(record + 4, node->model_ordinal);
        for(component = 0; component < 16; ++component)
            store_float(record + 16 + component * sizeof(uint32_t),
                        node->local_transform[component]);
    }

    store_le32(bytes, PVR_CHUNK_SCENE_HIERARCHY_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_SCENE_HIERARCHY_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)scene->node_count);
    store_le16(bytes + 16, PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES);
    store_le32(bytes + 20, crc32_bytes(
        bytes + PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES, node_bytes));
    store_le32(bytes + 28, crc32_bytes(bytes, 28));

    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;
}
