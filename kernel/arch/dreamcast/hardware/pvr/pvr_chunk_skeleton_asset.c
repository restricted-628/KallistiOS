/* KallistiOS ##version##

   dc/pvr/pvr_chunk_skeleton_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skeleton_asset.h>

#include <dc/matrix.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 28,
    HEADER_RESERVED_OFFSET = 32,
    HEADER_RESERVED_BYTES = 12,
    HEADER_CRC_OFFSET = 44,
    HEADER_CRC_BYTES = 44,
    JOINT_NODE_OFFSET = 0,
    JOINT_RESERVED_OFFSET = 4,
    JOINT_RESERVED_BYTES = 12,
    JOINT_MATRIX_OFFSET = 16,
    MATRIX_BYTES = 16 * sizeof(uint32_t)
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_float(const uint8_t *bytes) {
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

static int bytes_are_zero(const uint8_t *bytes, size_t size) {
    size_t index;

    for(index = 0; index < size; ++index) {
        if(bytes[index])
            return 0;
    }
    return 1;
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

static int matrix_is_finite(const matrix_t *matrix) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            if(!isfinite((*matrix)[column][row]))
                return 0;
        }
    }
    return 1;
}

static void decode_matrix(const uint8_t *bytes, matrix_t *matrix) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            (*matrix)[column][row] = read_float(
                bytes + (column * 4 + row) * sizeof(uint32_t));
        }
    }
}

static void decode_joint(const uint8_t *record,
                         pvr_chunk_skeleton_joint_t *joint) {
    joint->node_index = read_le32(record + JOINT_NODE_OFFSET);
    decode_matrix(record + JOINT_MATRIX_OFFSET, &joint->inverse_bind);
}

int pvr_chunk_skeleton_section_open(
    const void *data, size_t size,
    pvr_chunk_skeleton_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_skeleton_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t joint_count;
    uint32_t node_count;
    uint32_t payload_bytes;
    uint64_t expected_payload;
    size_t index;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_SKELETON_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_SKELETON_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES ||
       read_le16(bytes + 20) != PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES ||
       read_le16(bytes + 22) != MATRIX_BYTES ||
       !bytes_are_zero(bytes + HEADER_RESERVED_OFFSET,
                       HEADER_RESERVED_BYTES) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    joint_count = read_le32(bytes + 12);
    node_count = read_le32(bytes + 16);
    payload_bytes = read_le32(bytes + 24);
    expected_payload = (uint64_t)joint_count *
                       PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES;
    if(file_bytes != size || !joint_count || !node_count ||
       joint_count > node_count || expected_payload > UINT32_MAX ||
       payload_bytes != expected_payload ||
       payload_bytes > UINT32_MAX - PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES ||
       file_bytes != PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES +
                     payload_bytes ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES,
           payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.joints = bytes + PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES;
    parsed.joint_count = joint_count;
    parsed.node_count = node_count;
    parsed.version = PVR_CHUNK_SKELETON_SECTION_VERSION;

    for(index = 0; index < parsed.joint_count; ++index) {
        const uint8_t *record = (const uint8_t *)parsed.joints +
            index * PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES;
        matrix_t inverse_bind;
        uint32_t node_index = read_le32(record + JOINT_NODE_OFFSET);
        size_t previous;

        decode_matrix(record + JOINT_MATRIX_OFFSET, &inverse_bind);
        if(node_index >= parsed.node_count ||
           !bytes_are_zero(record + JOINT_RESERVED_OFFSET,
                           JOINT_RESERVED_BYTES) ||
           !matrix_is_finite(&inverse_bind)) {
            errno = EILSEQ;
            return -1;
        }
        for(previous = 0; previous < index; ++previous) {
            const uint8_t *other = (const uint8_t *)parsed.joints +
                previous * PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES;

            if(read_le32(other + JOINT_NODE_OFFSET) == node_index) {
                errno = EILSEQ;
                return -1;
            }
        }
    }

    *view = parsed;
    return 0;
}

int pvr_chunk_skeleton_section_joint_get(
    const pvr_chunk_skeleton_section_view_t *view, size_t index,
    pvr_chunk_skeleton_joint_t *joint) {
    pvr_chunk_skeleton_section_view_t checked;

    if(!view || !joint || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skeleton_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.joint_count) {
        errno = ENOENT;
        return -1;
    }
    decode_joint((const uint8_t *)checked.joints +
                     index * PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES,
                 joint);
    return 0;
}

int pvr_chunk_skeleton_section_materialize(
    const pvr_chunk_skeleton_section_view_t *view,
    pvr_chunk_skeleton_joint_t *joints, size_t joint_capacity,
    pvr_chunk_skeleton_t *skeleton) {
    pvr_chunk_skeleton_section_view_t checked;
    pvr_chunk_skeleton_t materialized;
    size_t joint_bytes;
    size_t index;

    if(!view || !joints || !skeleton || !view->data ||
       ((uintptr_t)joints &
        (_Alignof(pvr_chunk_skeleton_joint_t) - 1u)) ||
       ((uintptr_t)skeleton & (_Alignof(pvr_chunk_skeleton_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_skeleton_section_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(joint_capacity < checked.joint_count) {
        errno = ENOSPC;
        return -1;
    }
    if(checked.joint_count > SIZE_MAX / sizeof(*joints)) {
        errno = EOVERFLOW;
        return -1;
    }
    joint_bytes = checked.joint_count * sizeof(*joints);
    if(ranges_overlap(joints, joint_bytes, checked.data, checked.size) ||
       ranges_overlap(skeleton, sizeof(*skeleton), checked.data,
                      checked.size) ||
       ranges_overlap(skeleton, sizeof(*skeleton), joints, joint_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(index = 0; index < checked.joint_count; ++index) {
        decode_joint((const uint8_t *)checked.joints +
                         index * PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES,
                     &joints[index]);
    }
    materialized.joints = joints;
    materialized.joint_count = checked.joint_count;
    materialized.node_count = checked.node_count;
    *skeleton = materialized;
    return 0;
}

int pvr_chunk_skeleton_palette_build(
    const pvr_chunk_skeleton_t *skeleton,
    const matrix_t *world_matrices, size_t world_capacity,
    matrix_t *position_matrices, size_t position_capacity,
    pvr_normal_matrix_t *normal_matrices, size_t normal_capacity,
    pvr_skin_palette_t *palette) {
    pvr_skin_palette_t built;
    size_t joint_bytes;
    size_t world_bytes;
    size_t position_bytes;
    size_t normal_bytes;
    size_t index;

    if(palette)
        memset(palette, 0, sizeof(*palette));
    if(!skeleton || !skeleton->joints || !skeleton->joint_count ||
       !skeleton->node_count || !world_matrices || !position_matrices ||
       !normal_matrices || !palette ||
       ((uintptr_t)skeleton & (_Alignof(pvr_chunk_skeleton_t) - 1u)) ||
       ((uintptr_t)skeleton->joints &
        (_Alignof(pvr_chunk_skeleton_joint_t) - 1u)) ||
       ((uintptr_t)world_matrices & (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)position_matrices & (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)normal_matrices &
        (_Alignof(pvr_normal_matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(world_capacity < skeleton->node_count ||
       position_capacity < skeleton->joint_count ||
       normal_capacity < skeleton->joint_count) {
        errno = ENOSPC;
        return -1;
    }
    if(skeleton->joint_count >
           SIZE_MAX / sizeof(pvr_chunk_skeleton_joint_t) ||
       skeleton->node_count > SIZE_MAX / sizeof(*world_matrices) ||
       skeleton->joint_count > SIZE_MAX / sizeof(*position_matrices) ||
       skeleton->joint_count > SIZE_MAX / sizeof(*normal_matrices)) {
        errno = EOVERFLOW;
        return -1;
    }
    joint_bytes = skeleton->joint_count * sizeof(*skeleton->joints);
    world_bytes = skeleton->node_count * sizeof(*world_matrices);
    position_bytes = skeleton->joint_count * sizeof(*position_matrices);
    normal_bytes = skeleton->joint_count * sizeof(*normal_matrices);
    if(ranges_overlap(position_matrices, position_bytes,
                      normal_matrices, normal_bytes) ||
       ranges_overlap(position_matrices, position_bytes,
                      world_matrices, world_bytes) ||
       ranges_overlap(normal_matrices, normal_bytes,
                      world_matrices, world_bytes) ||
       ranges_overlap(position_matrices, position_bytes,
                      skeleton->joints, joint_bytes) ||
       ranges_overlap(normal_matrices, normal_bytes,
                      skeleton->joints, joint_bytes) ||
       ranges_overlap(palette, sizeof(*palette), position_matrices,
                      position_bytes) ||
       ranges_overlap(palette, sizeof(*palette), normal_matrices,
                      normal_bytes) ||
       ranges_overlap(palette, sizeof(*palette), world_matrices,
                      world_bytes) ||
       ranges_overlap(palette, sizeof(*palette), skeleton->joints,
                      joint_bytes)) {
        errno = EINVAL;
        return -1;
    }

    /* This first pass proves every inverse-transpose exists before any
       caller-owned matrix is changed. The second pass only publishes values
       that have already passed the identical arithmetic and validation. */
    for(index = 0; index < skeleton->joint_count; ++index) {
        const pvr_chunk_skeleton_joint_t *joint = &skeleton->joints[index];
        matrix_t position;
        pvr_normal_matrix_t normal;

        if(joint->node_index >= skeleton->node_count ||
           !matrix_is_finite(&joint->inverse_bind) ||
           !matrix_is_finite(&world_matrices[joint->node_index])) {
            errno = EDOM;
            return -1;
        }
        if(mat_compose(&position, &world_matrices[joint->node_index],
                       &joint->inverse_bind) < 0 ||
           pvr_normal_matrix_build(&normal, &position) < 0)
            return -1;
    }

    for(index = 0; index < skeleton->joint_count; ++index) {
        const pvr_chunk_skeleton_joint_t *joint = &skeleton->joints[index];

        if(mat_compose(&position_matrices[index],
                       &world_matrices[joint->node_index],
                       &joint->inverse_bind) < 0 ||
           pvr_normal_matrix_build(&normal_matrices[index],
                                   &position_matrices[index]) < 0)
            return -1;
    }

    built.position_matrices = position_matrices;
    built.normal_matrices = normal_matrices;
    built.joint_count = skeleton->joint_count;
    *palette = built;
    return 0;
}
