/* KallistiOS ##version##

   pvr_chunk_hierarchy.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
}

static int matrix_finite(const matrix_t *matrix) {
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

static int model_view_present(const pvr_chunk_model_view_t *view) {
    const pvr_chunk_model_t *model;

    if(!view)
        return 1;

    model = &view->model;
    return model->vertex_words && model->vertex_word_count &&
           model->polygon_words && model->polygon_word_count &&
           isfinite(model->center[0]) && isfinite(model->center[1]) &&
           isfinite(model->center[2]) && isfinite(model->radius) &&
           model->radius >= 0.0f;
}

static int hierarchy_validate(const pvr_chunk_hierarchy_t *hierarchy,
                              const matrix_t *local_transforms,
                              size_t local_capacity,
                              const anim_transform_t *local_poses,
                              size_t pose_capacity,
                              const matrix_t *root_transform,
                              matrix_t *world_matrices,
                              size_t world_capacity) {
    size_t i;

    if(!hierarchy) {
        errno = EINVAL;
        return -1;
    }

    if(!hierarchy->node_count)
        return 0;

    if(!hierarchy->nodes || !world_matrices ||
       ((uintptr_t)hierarchy->nodes &
        (_Alignof(pvr_chunk_hierarchy_node_t) - 1u)) ||
       !matrix_aligned(world_matrices)) {
        errno = EINVAL;
        return -1;
    }

    if(world_capacity < hierarchy->node_count) {
        errno = ENOSPC;
        return -1;
    }

    if(local_transforms && local_poses) {
        errno = EINVAL;
        return -1;
    }

    if(local_transforms) {
        uintptr_t local_begin;
        uintptr_t local_end;
        uintptr_t world_begin;
        uintptr_t world_end;
        size_t bytes;

        if(!matrix_aligned(local_transforms)) {
            errno = EINVAL;
            return -1;
        }

        if(local_capacity < hierarchy->node_count) {
            errno = ENOSPC;
            return -1;
        }

        if(hierarchy->node_count > SIZE_MAX / sizeof(matrix_t)) {
            errno = EOVERFLOW;
            return -1;
        }

        bytes = hierarchy->node_count * sizeof(matrix_t);
        local_begin = (uintptr_t)local_transforms;
        world_begin = (uintptr_t)world_matrices;
        if(local_begin > UINTPTR_MAX - bytes ||
           world_begin > UINTPTR_MAX - bytes) {
            errno = EOVERFLOW;
            return -1;
        }

        local_end = local_begin + bytes;
        world_end = world_begin + bytes;
        if(local_begin != world_begin && local_begin < world_end &&
           world_begin < local_end) {
            errno = EINVAL;
            return -1;
        }
    }

    if(local_poses) {
        uintptr_t pose_begin;
        uintptr_t pose_end;
        uintptr_t world_begin;
        uintptr_t world_end;
        size_t pose_bytes;
        size_t world_bytes;

        if(((uintptr_t)local_poses & (_Alignof(anim_transform_t) - 1u))) {
            errno = EINVAL;
            return -1;
        }
        if(pose_capacity < hierarchy->node_count) {
            errno = ENOSPC;
            return -1;
        }
        if(hierarchy->node_count > SIZE_MAX / sizeof(*local_poses) ||
           hierarchy->node_count > SIZE_MAX / sizeof(*world_matrices)) {
            errno = EOVERFLOW;
            return -1;
        }
        pose_bytes = hierarchy->node_count * sizeof(*local_poses);
        world_bytes = hierarchy->node_count * sizeof(*world_matrices);
        pose_begin = (uintptr_t)local_poses;
        world_begin = (uintptr_t)world_matrices;
        if(pose_begin > UINTPTR_MAX - pose_bytes ||
           world_begin > UINTPTR_MAX - world_bytes) {
            errno = EOVERFLOW;
            return -1;
        }
        pose_end = pose_begin + pose_bytes;
        world_end = world_begin + world_bytes;
        if(pose_begin < world_end && world_begin < pose_end) {
            errno = EINVAL;
            return -1;
        }
    }

    if(root_transform &&
       (!matrix_aligned(root_transform) || !matrix_finite(root_transform))) {
        errno = EDOM;
        return -1;
    }

    /* Requiring every parent to precede its child gives the traversal a
       topological order by construction. Cycles and forward references are
       rejected without recursion, allocation, or a visited bitmap. */
    for(i = 0; i < hierarchy->node_count; ++i) {
        const pvr_chunk_hierarchy_node_t *node = hierarchy->nodes + i;
        matrix_t checked_pose;

        if((node->parent_index != PVR_CHUNK_NODE_NONE &&
            node->parent_index >= i) ||
           (node->flags & ~PVR_CHUNK_NODE_FLAGS_MASK) ||
           !matrix_finite(local_transforms ? local_transforms + i :
                                             &node->local_transform) ||
           !model_view_present(node->model)) {
            errno = EILSEQ;
            return -1;
        }
        if(local_poses && anim_transform_matrix_build(
               local_poses + i, &checked_pose) < 0) {
            errno = EILSEQ;
            return -1;
        }
    }

    return 0;
}

/* Parent-before-child ordering makes this ancestry walk bounded. Walking the
   chain avoids per-traversal state and remains correct when a subtree's nodes
   are interleaved with nodes from another subtree. */
static int node_has_pruned_ancestor(
        const pvr_chunk_hierarchy_t *hierarchy, size_t node_index) {
    size_t parent = hierarchy->nodes[node_index].parent_index;

    while(parent != PVR_CHUNK_NODE_NONE) {
        if(hierarchy->nodes[parent].flags &
           PVR_CHUNK_NODE_PRUNE_CHILDREN)
            return 1;
        parent = hierarchy->nodes[parent].parent_index;
    }
    return 0;
}

static int local_pose_matrix(const anim_transform_t *source,
                             uint32_t flags, matrix_t *matrix) {
    anim_transform_t selected = *source;

    if(flags & PVR_CHUNK_NODE_SUPPRESS_TRANSLATION) {
        selected.translation.x = 0.0f;
        selected.translation.y = 0.0f;
        selected.translation.z = 0.0f;
    }
    if(flags & PVR_CHUNK_NODE_SUPPRESS_ROTATION) {
        selected.rotation.w = 1.0f;
        selected.rotation.x = 0.0f;
        selected.rotation.y = 0.0f;
        selected.rotation.z = 0.0f;
    }
    if(flags & PVR_CHUNK_NODE_SUPPRESS_SCALE) {
        selected.scale.x = 1.0f;
        selected.scale.y = 1.0f;
        selected.scale.z = 1.0f;
    }
    return anim_transform_matrix_build(&selected, matrix);
}

static int hierarchy_traverse(
        const pvr_chunk_hierarchy_t *hierarchy,
        const matrix_t *local_transforms, size_t local_capacity,
        const anim_transform_t *local_poses, size_t pose_capacity,
        const matrix_t *root_transform,
        matrix_t *world_matrices, size_t world_capacity,
        pvr_chunk_hierarchy_visit_t visit, void *data,
        pvr_chunk_hierarchy_result_t *result) {
    matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_hierarchy_result_t progress = { 0 };
    size_t i;

    if(result)
        *result = progress;

    if(hierarchy_validate(hierarchy, local_transforms, local_capacity,
                          local_poses, pose_capacity,
                          root_transform, world_matrices, world_capacity) < 0)
        return -1;

    if(!hierarchy->node_count)
        return 0;

    for(i = 0; i < hierarchy->node_count; ++i) {
        const pvr_chunk_hierarchy_node_t *node = hierarchy->nodes + i;
        matrix_t pose_matrix;
        const matrix_t *local;
        const matrix_t *parent = node->parent_index == PVR_CHUNK_NODE_NONE ?
                                 (root_transform ? root_transform : &identity) :
                                 world_matrices + node->parent_index;
        matrix_t composed;
        int rv;

        if(node_has_pruned_ancestor(hierarchy, i))
            continue;

        if(local_poses) {
            if(local_pose_matrix(local_poses + i, node->flags,
                                 &pose_matrix) < 0) {
                errno = ERANGE;
                return -1;
            }
            local = &pose_matrix;
        }
        else
            local = local_transforms ? local_transforms + i :
                                       &node->local_transform;

        if(mat_compose(&composed, parent, local) < 0 ||
           !matrix_finite(&composed)) {
            errno = ERANGE;
            return -1;
        }

        memcpy(world_matrices + i, &composed, sizeof(composed));
        ++progress.visited_nodes;
        if(result)
            *result = progress;

        if(!visit || (node->flags & PVR_CHUNK_NODE_HIDDEN))
            continue;

        errno = 0;
        rv = visit(i, node, world_matrices + i, data);
        if(rv < 0) {
            if(!errno)
                errno = ECANCELED;
            return -1;
        }
        if(rv > 0)
            return 1;
    }

    return 0;
}

int pvr_chunk_hierarchy_traverse(
        const pvr_chunk_hierarchy_t *hierarchy,
        const matrix_t *root_transform,
        matrix_t *world_matrices, size_t world_capacity,
        pvr_chunk_hierarchy_visit_t visit, void *data,
        pvr_chunk_hierarchy_result_t *result) {
    return hierarchy_traverse(hierarchy, NULL, 0, NULL, 0,
                              root_transform,
                              world_matrices, world_capacity, visit, data,
                              result);
}

int pvr_chunk_hierarchy_traverse_transforms(
        const pvr_chunk_hierarchy_t *hierarchy,
        const matrix_t *local_transforms, size_t local_capacity,
        const matrix_t *root_transform,
        matrix_t *world_matrices, size_t world_capacity,
        pvr_chunk_hierarchy_visit_t visit, void *data,
        pvr_chunk_hierarchy_result_t *result) {
    if(!local_transforms && hierarchy && hierarchy->node_count) {
        if(result)
            result->visited_nodes = 0;
        errno = EINVAL;
        return -1;
    }

    return hierarchy_traverse(hierarchy, local_transforms, local_capacity,
                              NULL, 0,
                              root_transform, world_matrices, world_capacity,
                              visit, data, result);
}

int pvr_chunk_hierarchy_traverse_poses(
        const pvr_chunk_hierarchy_t *hierarchy,
        const anim_transform_t *local_poses, size_t local_capacity,
        const matrix_t *root_transform,
        matrix_t *world_matrices, size_t world_capacity,
        pvr_chunk_hierarchy_visit_t visit, void *data,
        pvr_chunk_hierarchy_result_t *result) {
    if(!local_poses && hierarchy && hierarchy->node_count) {
        if(result)
            result->visited_nodes = 0;
        errno = EINVAL;
        return -1;
    }

    return hierarchy_traverse(hierarchy, NULL, 0, local_poses,
                              local_capacity, root_transform,
                              world_matrices, world_capacity, visit, data,
                              result);
}
