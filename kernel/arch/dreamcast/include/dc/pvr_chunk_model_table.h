/* KallistiOS ##version##

   dc/pvr_chunk_model_table.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_model_table.h
    \brief   Per-model metadata for multi-model compact assets.
    \ingroup pvr_chunk_model

    A model-table section assigns exact local bounds and optional PCM2 section
    ordinals to every paired vertex/polygon model. Records remain pointer-free;
    callers choose which optional sections to materialize.
*/

#ifndef __DC_PVR_CHUNK_MODEL_TABLE_H
#define __DC_PVR_CHUNK_MODEL_TABLE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PMT1` at the start of a model table. */
#define PVR_CHUNK_MODEL_TABLE_MAGIC UINT32_C(0x31544d50)

/** \brief Current serialized model-table version. */
#define PVR_CHUNK_MODEL_TABLE_VERSION 1u

/** \brief Fixed model-table header size. */
#define PVR_CHUNK_MODEL_TABLE_HEADER_BYTES 32u

/** \brief Fixed serialized model-record size. */
#define PVR_CHUNK_MODEL_TABLE_RECORD_BYTES 64u

/** \brief Stable marker for an absent optional section. */
#define PVR_CHUNK_MODEL_SECTION_NONE UINT32_MAX

/** \brief Decoded metadata for one model ordinal. */
typedef struct pvr_chunk_model_table_record {
    size_t vertex_ordinal;
    size_t polygon_ordinal;
    size_t resource_ordinal;
    size_t volume_ordinal;
    size_t skin4_ordinal;
    size_t skin_general_ordinal;
    size_t skeleton_ordinal;
    size_t morph_ordinal;
    size_t cooked_cache_ordinal;
    float center[3];
    float radius;
} pvr_chunk_model_table_record_t;

/** \brief Checked immutable view of one serialized model table. */
typedef struct pvr_chunk_model_table_view {
    const void *data;
    size_t size;
    const void *records;
    size_t model_count;
    size_t record_stride;
    uint16_t version;
} pvr_chunk_model_table_view_t;

/** \brief Parse and completely validate a serialized model table.

    Header and payload CRCs, exact sizes, reserved fields, canonical paired
    stream ordinals, and finite nonnegative bounds are verified before
    \p view changes. Optional ordinals are checked structurally here and
    against a concrete PCM2 directory by
    pvr_chunk_model_table_validate_asset().
*/
int pvr_chunk_model_table_open(
    const void *data, size_t size, pvr_chunk_model_table_view_t *view);

/** \brief Decode one admitted model record by ordinal. */
int pvr_chunk_model_table_record_get(
    const pvr_chunk_model_table_view_t *view, size_t model_ordinal,
    pvr_chunk_model_table_record_t *record);

/** \brief Validate every record and optional ordinal against one asset.

    The table count must equal the asset's paired model count. Every optional
    non-NONE ordinal must resolve to a section of the corresponding type.
*/
int pvr_chunk_model_table_validate_asset(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset);

/** \brief Query workspace for one table-selected model. */
int pvr_chunk_model_table_workspace_query(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset, size_t model_ordinal,
    pvr_chunk_asset_workspace_requirements_t *requirements);

/** \brief Load one table-selected model with its exact local bounds.

    Stream decoding and ownership follow pvr_chunk_asset_model_load(). The
    fully validated table bounds replace the container-wide conservative
    bounds in the resulting immutable model view.
*/
int pvr_chunk_model_table_load(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset, size_t model_ordinal,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *model_view);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_MODEL_TABLE_H */
