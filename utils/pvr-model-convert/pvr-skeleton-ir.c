/* KallistiOS ##version##

   Host-side compact skeleton serializer.
   Copyright (C) 2026 Joseph Black
*/

#include "pvr-scene-ir.h"

#include <dc/pvr_chunk_skeleton_asset.h>

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

int pvr_scene_ir_serialize_skeleton(
    const pvr_chunk_skeleton_t *skeleton,
    uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_skeleton_section_view_t checked;
    uint8_t *bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t joint_index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!skeleton || !bytes_out || !size_out || !skeleton->joints ||
       !skeleton->joint_count || !skeleton->node_count ||
       skeleton->joint_count > skeleton->node_count ||
       skeleton->joint_count >
           (UINT32_MAX - PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES) /
               PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES ||
       skeleton->node_count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    payload_bytes = skeleton->joint_count *
                    PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES;
    file_bytes = PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES + payload_bytes;
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(joint_index = 0; joint_index < skeleton->joint_count;
        ++joint_index) {
        const pvr_chunk_skeleton_joint_t *joint =
            &skeleton->joints[joint_index];
        uint8_t *record = bytes +
            PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES + joint_index *
                PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES;
        size_t previous;
        size_t column;
        size_t row;

        if(joint->node_index >= skeleton->node_count ||
           joint->node_index > UINT32_MAX) {
            errno = EINVAL;
            goto fail;
        }
        for(previous = 0; previous < joint_index; ++previous) {
            if(skeleton->joints[previous].node_index == joint->node_index) {
                errno = EINVAL;
                goto fail;
            }
        }
        store_le32(record, (uint32_t)joint->node_index);
        for(column = 0; column < 4; ++column) {
            for(row = 0; row < 4; ++row) {
                float value = joint->inverse_bind[column][row];

                if(!isfinite(value)) {
                    errno = EDOM;
                    goto fail;
                }
                store_float(record + 16 +
                                (column * 4 + row) * sizeof(uint32_t),
                            value);
            }
        }
    }

    store_le32(bytes, PVR_CHUNK_SKELETON_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_SKELETON_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)skeleton->joint_count);
    store_le32(bytes + 16, (uint32_t)skeleton->node_count);
    store_le16(bytes + 20, PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES);
    store_le16(bytes + 22, 16u * sizeof(uint32_t));
    store_le32(bytes + 24, (uint32_t)payload_bytes);
    store_le32(bytes + 28, crc32_bytes(
        bytes + PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
    if(pvr_chunk_skeleton_section_open(bytes, file_bytes, &checked) < 0)
        goto fail;

    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail: {
        int saved_errno = errno ? errno : EIO;

        free(bytes);
        errno = saved_errno;
        return -1;
    }
}
