/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skeleton_affine.h>
#include "pvr_skin_internal.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(shz_mat3x4_t) == 12 * sizeof(float), "compact matrix");

typedef struct region {
    uintptr_t begin, end;
} region_t;

/* Only used at API boundaries, never within the joint composition loop. */
static int region_init(region_t *out, const void *pointer, size_t count,
                       size_t element, size_t alignment) {
    uintptr_t begin = (uintptr_t)pointer;
    if(!pointer || !count || (begin & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(count > SIZE_MAX / element || count * element > UINTPTR_MAX - begin) {
        errno = EOVERFLOW;
        return -1;
    }
    *out = (region_t){begin, begin + count * element};
    return 0;
}

static int overlap(region_t a, region_t b) {
    return a.begin < b.end && b.begin < a.end;
}

static int outputs_disjoint(const region_t output[2],
                            const region_t *input, size_t count) {
    if(overlap(output[0], output[1])) {
        errno = EINVAL;
        return -1;
    }
    for(size_t i = 0; i < count; ++i) {
        if(overlap(output[0], input[i]) || overlap(output[1], input[i])) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

static int affine_finite(const matrix_t *matrix) {
    if((*matrix)[0][3] != 0.0f || (*matrix)[1][3] != 0.0f ||
       (*matrix)[2][3] != 0.0f || (*matrix)[3][3] != 1.0f)
        return 0;
    for(size_t c = 0; c < 4; ++c)
        for(size_t r = 0; r < 3; ++r)
            if(!isfinite((*matrix)[c][r]))
                return 0;
    return 1;
}

static void pack(shz_mat3x4_t *out, const matrix_t *matrix) {
    for(size_t c = 0; c < 4; ++c)
        out->col[c] = shz_vec3_init((*matrix)[c][0], (*matrix)[c][1],
                                   (*matrix)[c][2]);
}

int pvr_chunk_hierarchy_affine_prepare(const pvr_chunk_hierarchy_t *hierarchy,
    pvr_chunk_hierarchy_affine_node_t *storage, size_t capacity,
    pvr_chunk_hierarchy_affine_t *prepared) {
    region_t input[2], output[2];
    if(region_init(&input[0], hierarchy, 1, sizeof(*hierarchy),
                   _Alignof(pvr_chunk_hierarchy_t)) < 0)
        return -1;
    if(capacity < hierarchy->node_count) {
        errno = ENOSPC;
        return -1;
    }
    if(region_init(&input[1], hierarchy->nodes, hierarchy->node_count,
                   sizeof(*hierarchy->nodes),
                   _Alignof(pvr_chunk_hierarchy_node_t)) < 0 ||
       region_init(&output[0], storage, hierarchy->node_count, sizeof(*storage),
                   _Alignof(pvr_chunk_hierarchy_affine_node_t)) < 0 ||
       region_init(&output[1], prepared, 1, sizeof(*prepared),
                   _Alignof(pvr_chunk_hierarchy_affine_t)) < 0 ||
       outputs_disjoint(output, input, 2) < 0)
        return -1;
    for(size_t i = 0; i < hierarchy->node_count; ++i) {
        const pvr_chunk_hierarchy_node_t *node = hierarchy->nodes + i;
        if((node->parent_index != PVR_CHUNK_NODE_NONE &&
            node->parent_index >= i) ||
           (node->flags & ~PVR_CHUNK_NODE_FLAGS_MASK)) {
            errno = EINVAL;
            return -1;
        }
        if(node->flags & PVR_CHUNK_NODE_PRUNE_CHILDREN) {
            errno = ENOTSUP;
            return -1;
        }
    }
    for(size_t i = 0; i < hierarchy->node_count; ++i)
        storage[i] = (pvr_chunk_hierarchy_affine_node_t){
            hierarchy->nodes[i].parent_index, hierarchy->nodes[i].flags};
    *prepared = (pvr_chunk_hierarchy_affine_t){storage, hierarchy->node_count};
    return 0;
}

static int compact_finite(const shz_mat3x4_t *matrix) {
    for(size_t i = 0; i < 12; ++i)
        if(!isfinite(matrix->elem[i]))
            return 0;
    return 1;
}

/* Match animation's TRS admission, but retain the quaternion norm for use
   below rather than validating and normalizing it in separate passes. */
static int local_affine(const anim_transform_t *source, uint32_t flags,
                        shz_mat3x4_t *out) {
    shz_quat_t q = shz_quat_init(source->rotation.w, source->rotation.x,
                                source->rotation.y, source->rotation.z);
    float norm;
    if(!isfinite(source->translation.x) || !isfinite(source->translation.y) ||
       !isfinite(source->translation.z) || !isfinite(source->scale.x) ||
       !isfinite(source->scale.y) || !isfinite(source->scale.z) ||
       !isfinite(q.w) || !isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z)) {
        errno = EINVAL;
        return -1;
    }
    norm = shz_quat_magnitude_sqr(q);
    if(!isfinite(norm) || norm <= FLT_MIN) {
        errno = EINVAL;
        return -1;
    }
    if(flags & PVR_CHUNK_NODE_SUPPRESS_ROTATION)
        q = shz_quat_init(1.0f, 0.0f, 0.0f, 0.0f);
    else
        q = shz_quat_scale(q, shz_inv_sqrtf_fsrra(norm));

    /* SH4ZAM's quaternion initializer supplies the rotation columns. Only
       this small local temporary is 4x4; no full world-matrix array or
       subsequent world-packing pass is required. */
    shz_mat4x4_t rotation;
    shz_mat4x4_init_rotation_quat(&rotation, q);
    const int unit_scale = flags & PVR_CHUNK_NODE_SUPPRESS_SCALE;
    out->col[0] = shz_vec3_scale(rotation.col[0].xyz,
                                unit_scale ? 1.0f : source->scale.x);
    out->col[1] = shz_vec3_scale(rotation.col[1].xyz,
                                unit_scale ? 1.0f : source->scale.y);
    out->col[2] = shz_vec3_scale(rotation.col[2].xyz,
                                unit_scale ? 1.0f : source->scale.z);
    out->col[3] = flags & PVR_CHUNK_NODE_SUPPRESS_TRANSLATION ?
        shz_vec3_init(0.0f, 0.0f, 0.0f) :
        shz_vec3_init(source->translation.x, source->translation.y,
                      source->translation.z);
    return 0;
}

int pvr_chunk_hierarchy_pose_build_affine(
    const pvr_chunk_hierarchy_affine_t *hierarchy,
    const anim_transform_t *local, size_t local_capacity,
    const shz_mat3x4_t *root, shz_mat3x4_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_pose_t *prepared) {
    region_t input[4], output[2];
    shz_mat4x4_t saved;
    const shz_mat3x4_t identity = {.elem = {
        1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
    if(region_init(&input[0], hierarchy, 1, sizeof(*hierarchy),
                   _Alignof(pvr_chunk_hierarchy_affine_t)) < 0)
        return -1;
    if(capacity < hierarchy->node_count || local_capacity < hierarchy->node_count) {
        errno = ENOSPC;
        return -1;
    }
    if(region_init(&input[1], hierarchy->nodes, hierarchy->node_count,
                   sizeof(*hierarchy->nodes),
                   _Alignof(pvr_chunk_hierarchy_affine_node_t)) < 0 ||
       region_init(&input[2], local, hierarchy->node_count, sizeof(*local),
                   _Alignof(anim_transform_t)) < 0 ||
       region_init(&output[0], storage, hierarchy->node_count, sizeof(*storage),
                   _Alignof(shz_mat3x4_t)) < 0 ||
       region_init(&output[1], prepared, 1, sizeof(*prepared),
                   _Alignof(pvr_chunk_skeleton_affine_pose_t)) < 0)
        return -1;
    if(root && region_init(&input[3], root, 1, sizeof(*root),
                            _Alignof(shz_mat3x4_t)) < 0)
        return -1;
    if(outputs_disjoint(output, input, root ? 4 : 3) < 0)
        return -1;
    if(root && !compact_finite(root)) {
        errno = EDOM;
        return -1;
    }

    *prepared = (pvr_chunk_skeleton_affine_pose_t){NULL, 0};
    shz_xmtrx_store_4x4(&saved);
    for(size_t i = 0; i < hierarchy->node_count; ++i) {
        shz_mat3x4_t transform;
        const pvr_chunk_hierarchy_affine_node_t *node = hierarchy->nodes + i;
        const shz_mat3x4_t *parent = node->parent_index == PVR_CHUNK_NODE_NONE ?
            (root ? root : &identity) : storage + node->parent_index;
        if(local_affine(local + i, node->flags, &transform) < 0) {
            shz_xmtrx_load_4x4(&saved);
            return -1;
        }
        shz_xmtrx_load_apply_store_3x4(storage + i, parent, &transform);
        if(!compact_finite(storage + i)) {
            shz_xmtrx_load_4x4(&saved);
            errno = ERANGE;
            return -1;
        }
    }
    shz_xmtrx_load_4x4(&saved);
    *prepared = (pvr_chunk_skeleton_affine_pose_t){storage, hierarchy->node_count};
    return 0;
}

int pvr_chunk_skeleton_affine_prepare(const pvr_chunk_skeleton_t *skeleton,
    pvr_chunk_skeleton_affine_joint_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_t *prepared) {
    region_t input[2], output[2];
    if(region_init(&input[0], skeleton, 1, sizeof(*skeleton),
                   _Alignof(pvr_chunk_skeleton_t)) < 0)
        return -1;
    if(!skeleton->node_count || !skeleton->joint_count) {
        errno = EINVAL;
        return -1;
    }
    if(capacity < skeleton->joint_count) {
        errno = ENOSPC;
        return -1;
    }
    if(region_init(&input[1], skeleton->joints, skeleton->joint_count,
                   sizeof(*skeleton->joints),
                   _Alignof(pvr_chunk_skeleton_joint_t)) < 0 ||
       region_init(&output[0], storage, skeleton->joint_count,
                   sizeof(*storage),
                   _Alignof(pvr_chunk_skeleton_affine_joint_t)) < 0 ||
       region_init(&output[1], prepared, 1, sizeof(*prepared),
                   _Alignof(pvr_chunk_skeleton_affine_t)) < 0 ||
       outputs_disjoint(output, input, 2) < 0)
        return -1;
    for(size_t i = 0; i < skeleton->joint_count; ++i) {
        const pvr_chunk_skeleton_joint_t *joint = skeleton->joints + i;
        if(joint->node_index >= skeleton->node_count ||
           !affine_finite(&joint->inverse_bind)) {
            errno = EDOM;
            return -1;
        }
    }
    for(size_t i = 0; i < skeleton->joint_count; ++i) {
        storage[i].node_index = skeleton->joints[i].node_index;
        pack(&storage[i].inverse_bind, &skeleton->joints[i].inverse_bind);
    }
    *prepared = (pvr_chunk_skeleton_affine_t){storage,
        skeleton->joint_count, skeleton->node_count};
    return 0;
}

int pvr_chunk_skeleton_pose_prepare_affine(const matrix_t *world,
    size_t node_count, shz_mat3x4_t *storage, size_t capacity,
    pvr_chunk_skeleton_affine_pose_t *prepared) {
    region_t input, output[2];
    if(capacity < node_count) {
        errno = ENOSPC;
        return -1;
    }
    if(region_init(&input, world, node_count, sizeof(*world),
                   _Alignof(matrix_t)) < 0 ||
       region_init(&output[0], storage, node_count, sizeof(*storage),
                   _Alignof(shz_mat3x4_t)) < 0 ||
       region_init(&output[1], prepared, 1, sizeof(*prepared),
                   _Alignof(pvr_chunk_skeleton_affine_pose_t)) < 0 ||
       outputs_disjoint(output, &input, 1) < 0)
        return -1;
    for(size_t i = 0; i < node_count; ++i) {
        if(!affine_finite(world + i)) {
            errno = EDOM;
            return -1;
        }
    }
    for(size_t i = 0; i < node_count; ++i)
        pack(storage + i, world + i);
    *prepared = (pvr_chunk_skeleton_affine_pose_t){storage, node_count};
    return 0;
}

static int compose(const pvr_chunk_skeleton_affine_joint_t *joint,
                   const shz_mat3x4_t *world, matrix_t *position,
                   pvr_normal_matrix_t *normal) {
    shz_xmtrx_load_apply_3x4(world + joint->node_index, &joint->inverse_bind);
    shz_xmtrx_store_unaligned_4x4((float *)*position);
    return pvr_normal_matrix_build(normal, position);
}

int pvr_chunk_skeleton_palette_build_affine(
    const pvr_chunk_skeleton_affine_t *skeleton,
    const pvr_chunk_skeleton_affine_pose_t *pose,
    pvr_skin_prepared_joint_t *storage, size_t capacity,
    pvr_skin_prepared_palette_t *prepared) {
    region_t input[4], output[2];
    shz_mat4x4_t saved;
    matrix_t position;
    pvr_normal_matrix_t normal;
    if(region_init(&input[0], skeleton, 1, sizeof(*skeleton),
                   _Alignof(pvr_chunk_skeleton_affine_t)) < 0 ||
       region_init(&input[1], pose, 1, sizeof(*pose),
                   _Alignof(pvr_chunk_skeleton_affine_pose_t)) < 0)
        return -1;
    if(pose->node_count != skeleton->node_count) {
        errno = EINVAL;
        return -1;
    }
    if(capacity < skeleton->joint_count) {
        errno = ENOSPC;
        return -1;
    }
    if(region_init(&input[2], skeleton->joints, skeleton->joint_count,
                   sizeof(*skeleton->joints),
                   _Alignof(pvr_chunk_skeleton_affine_joint_t)) < 0 ||
       region_init(&input[3], pose->world, pose->node_count, sizeof(*pose->world),
                   _Alignof(shz_mat3x4_t)) < 0 ||
       region_init(&output[0], storage, skeleton->joint_count, sizeof(*storage),
                   _Alignof(pvr_skin_prepared_joint_t)) < 0 ||
       region_init(&output[1], prepared, 1, sizeof(*prepared),
                   _Alignof(pvr_skin_prepared_palette_t)) < 0 ||
       outputs_disjoint(output, input, 4) < 0)
        return -1;

    shz_xmtrx_store_4x4(&saved);
    /* Preflight arithmetic once before publishing any output. Inputs remain
       immutable; the second pass repeats arithmetic, not admission checks. */
    for(size_t i = 0; i < skeleton->joint_count; ++i) {
        if(compose(skeleton->joints + i, pose->world, &position, &normal) < 0) {
            shz_xmtrx_load_4x4(&saved);
            return -1;
        }
    }
    for(size_t i = 0; i < skeleton->joint_count; ++i) {
        (void)compose(skeleton->joints + i, pose->world, &position, &normal);
        shz_kos_matrix_import(&storage[i].position, &position);
        memcpy(&storage[i].normal, &normal, sizeof(normal));
    }
    shz_xmtrx_load_4x4(&saved);
    *prepared = (pvr_skin_prepared_palette_t){storage, skeleton->joint_count,
                                            SKIN_PALETTE_VERSION};
    return 0;
}
