/* KallistiOS ##version##

   dc/pvr_chunk_scene.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_scene.h
    \brief   Serialized scene topology for compact-model assets.
    \ingroup pvr_chunk_model

    The section format stores a flat parent-before-child hierarchy. It carries
    stable model ordinals and matrices rather than target pointers, so host
    tools can emit it directly and target code can bind it into the existing
    pvr_chunk_hierarchy_t evaluator using caller-owned storage.
*/

#ifndef __DC_PVR_CHUNK_SCENE_H
#define __DC_PVR_CHUNK_SCENE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_asset.h>
#include <dc/pvr_chunk_model_table.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PCH1` at the start of a hierarchy section. */
#define PVR_CHUNK_SCENE_HIERARCHY_MAGIC UINT32_C(0x31484350)

/** \brief Legacy hierarchy version with a reserved zero flags word. */
#define PVR_CHUNK_SCENE_HIERARCHY_VERSION_1 1u

/** \brief Current hierarchy version with explicit node policy flags. */
#define PVR_CHUNK_SCENE_HIERARCHY_VERSION 2u

/** \brief Fixed hierarchy header size in bytes. */
#define PVR_CHUNK_SCENE_HIERARCHY_HEADER_BYTES 32u

/** \brief Fixed serialized-node size in bytes. */
#define PVR_CHUNK_SCENE_HIERARCHY_NODE_BYTES 80u

/** \brief Serialized ordinal for a transform-only hierarchy node. */
#define PVR_CHUNK_SCENE_MODEL_NONE UINT32_MAX

/** \brief Checked immutable view of a serialized hierarchy section. */
typedef struct pvr_chunk_scene_hierarchy_view {
    const void *data;
    size_t size;
    const void *nodes;
    size_t node_count;
    size_t node_stride;
    uint16_t version;
} pvr_chunk_scene_hierarchy_view_t;

/** \brief Host-independent value decoded from one hierarchy record. */
typedef struct pvr_chunk_scene_node {
    size_t parent_index;   /**< PVR_CHUNK_NODE_NONE for a root. */
    size_t model_ordinal;  /**< PVR_CHUNK_NODE_NONE for transform-only. */
    matrix_t local_transform;
    uint32_t flags;        /**< PVR_CHUNK_NODE_* evaluation policy. */
} pvr_chunk_scene_node_t;

/** \brief Coherent checked view of one canonical PCM2 scene asset.

    The view joins the container, its unique model table, and its unique
    hierarchy section. Metadata remains borrowed from the immutable asset;
    model streams are materialized separately into caller-owned storage.
*/
typedef struct pvr_chunk_scene_asset_view {
    pvr_chunk_asset_view_t asset;
    pvr_chunk_model_table_view_t model_table;
    pvr_chunk_scene_hierarchy_view_t hierarchy;
    size_t model_count;
    size_t node_count;
} pvr_chunk_scene_asset_view_t;

/** \brief Persistent decode workspace for unique streams in one scene asset. */
typedef struct pvr_chunk_scene_asset_workspace_requirements {
    size_t alignment;
    size_t bytes;
} pvr_chunk_scene_asset_workspace_requirements_t;

/** \brief Parse and completely validate a serialized hierarchy section.

    Nodes must be in parent-before-child order, every matrix component must be
    finite, and both the header and node array must match their checksums.
    Section version 1 requires a zero flags word; version 2 admits only
    documented node-policy bits. The entire section is admitted before \a view
    is published.

    \retval 0 Success.
    \retval -1 Failure with errno set to EINVAL or EILSEQ.
*/
int pvr_chunk_scene_hierarchy_open(
    const void *data, size_t size,
    pvr_chunk_scene_hierarchy_view_t *view);

/** \brief Decode one admitted hierarchy node by index.

    \retval 0 Success.
    \retval -1 Failure with errno set to EINVAL, EILSEQ, or ENOENT.
*/
int pvr_chunk_scene_hierarchy_node_get(
    const pvr_chunk_scene_hierarchy_view_t *view, size_t index,
    pvr_chunk_scene_node_t *node);

/** \brief Bind admitted scene nodes into the runtime hierarchy evaluator.

    Model ordinals select entries in \a models. A transform-only node uses no
    model. The caller supplies one output node per serialized node and retains
    the model views, node storage, and source section for as long as the
    resulting hierarchy is used. No allocation, rendering, or scene mutation
    occurs.

    \retval 0 Success.
    \retval -1 Failure with errno set to EINVAL, EILSEQ, ENOSPC, or
               EOVERFLOW.
*/
int pvr_chunk_scene_hierarchy_bind(
    const pvr_chunk_scene_hierarchy_view_t *view,
    const pvr_chunk_model_view_t *const *models, size_t model_count,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy);

/** \brief Bind scene nodes directly to a contiguous model-view array.

    This form avoids constructing an intermediate pointer table after loading
    every model in a scene asset. All validation, ownership, and publication
    rules match pvr_chunk_scene_hierarchy_bind().
*/
int pvr_chunk_scene_hierarchy_bind_models(
    const pvr_chunk_scene_hierarchy_view_t *view,
    const pvr_chunk_model_view_t *models, size_t model_count,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy);

/** \brief Open and cross-validate a canonical PCM2 scene asset.

    The asset must contain exactly one raw, naturally aligned model-table
    section and one raw, naturally aligned hierarchy section. Both metadata
    payloads are CRC-checked, and every hierarchy model ordinal is checked
    against the table before \p view is published. Model streams may use any
    codec supported by the caller's later decoder.

    Keeping scene metadata directly readable makes opening allocation-free.
    Noncanonical assets may still materialize and use the individual section
    APIs directly.
*/
int pvr_chunk_scene_asset_open(
    const pvr_chunk_asset_view_t *asset,
    pvr_chunk_scene_asset_view_t *view);

/** \brief Query persistent decode storage for all scene models.

    The returned span combines the exact requirements of each uniquely
    referenced vertex or polygon stream with nonoverlapping aligned slices.
    Models selecting the same compressed or unaligned stream share one decoded
    copy. The span excludes the caller-owned model-view and hierarchy-node
    arrays.
*/
int pvr_chunk_scene_asset_workspace_query(
    const pvr_chunk_scene_asset_view_t *view,
    pvr_chunk_scene_asset_workspace_requirements_t *requirements);

/** \brief Load every scene model and bind its hierarchy coherently.

    The caller supplies one model view per model and one node per hierarchy
    record. Decoded stream bytes remain in \p workspace for as long as any
    published model or hierarchy is used; model views selecting the same
    stream point to the same decoded bytes. A zero-byte workspace requirement
    permits NULL workspace. Every loaded model must be runtime-ready; streams
    that still require host canonicalization are rejected with ENOTSUP. On
    failure, no hierarchy is published and both output arrays are cleared if a
    model decode or hierarchy bind fails after loading begins; decoder writes
    to workspace may remain. Argument, capacity, and workspace preflight
    failures do not modify the arrays.

    No allocation, clock, renderer, list, or scene lifecycle is introduced.
*/
int pvr_chunk_scene_asset_load(
    const pvr_chunk_scene_asset_view_t *view,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *models, size_t model_capacity,
    pvr_chunk_hierarchy_node_t *nodes, size_t node_capacity,
    pvr_chunk_hierarchy_t *hierarchy);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_SCENE_H */
