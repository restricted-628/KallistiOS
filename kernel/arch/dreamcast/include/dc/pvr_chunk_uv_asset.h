/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/pvr_chunk_uv_asset.h
    \brief Serialized independent UV sources and material-layer associations.
    \ingroup pvr_chunk_render
*/
#ifndef __DC_PVR_CHUNK_UV_ASSET_H
#define __DC_PVR_CHUNK_UV_ASSET_H
#include <dc/pvr_chunk_uv.h>
#include <dc/pvr_chunk_layer_asset.h>
__BEGIN_DECLS
/** \addtogroup pvr_chunk_render
    @{
*/
#define PVR_CHUNK_UV_SECTION_MAGIC UINT32_C(0x31565550) /**< PUV1. */
#define PVR_CHUNK_UV_SECTION_VERSION 1u
#define PVR_CHUNK_UV_SECTION_HEADER_BYTES 40u

/** \brief Host/runtime input to explicit serialization, never a wire struct.
    One full ordinary model's source-reference UV sequence. Several layer
    bindings can share this source; different UV sets use different sources.
*/
typedef struct pvr_chunk_uv_asset_source {
    uint32_t model; /**< Ordered model-view ordinal, as in PML1/PMT1. */
    const pvr_chunk_uv_t *uv;
    size_t uv_count;
} pvr_chunk_uv_asset_source_t;

/** \brief PML1 entry ordinal selects a source in the PUV1 source table.
    Bindings must be sorted by layer, with no duplicates. Unbound PML1 entries
    retain canonical UVs. A bound entry's affine rows map the selected UVs.
*/
typedef struct pvr_chunk_uv_asset_binding {
    uint32_t layer, source;
} pvr_chunk_uv_asset_binding_t;

/** \brief Immutable admitted byte view. Source bytes must stay alive.
    Indexed access/decode does not rescan CRCs. Construct only with open().
*/
typedef struct pvr_chunk_uv_section_view {
    const void *data;
    size_t size, source_count, binding_count, uv_count;
} pvr_chunk_uv_section_view_t;

/** \brief Decoded source identity and full-model coordinate count. */
typedef struct pvr_chunk_uv_asset_info {
    uint32_t model;
    size_t uv_count;
} pvr_chunk_uv_asset_info_t;

/** \brief Query exact wire size; all counts must be nonzero and fit uint32. */
int pvr_chunk_uv_section_query(size_t sources, size_t bindings, size_t uvs,
                               size_t *bytes);

/** \brief Write explicit little-endian framing, tables, binary32 UVs and CRCs.
    Validates all counts, bindings, finite coordinates, capacity and aliases
    before writing. Destination must not overlap input arrays or any UV array.
    Failure preserves destination. No allocation or texture ownership.
*/
int pvr_chunk_uv_section_write(const pvr_chunk_uv_asset_source_t *sources,
    size_t source_count, const pvr_chunk_uv_asset_binding_t *bindings,
    size_t binding_count, void *data, size_t capacity);

/** \brief Validate exact framing, CRCs, ordered bindings and UV payload.
    Allows unaligned bytes. Failure preserves view; it cannot overlap input.
    This is structural admission, not model/layer semantic validation.
*/
int pvr_chunk_uv_section_open(const void *data, size_t size,
                              pvr_chunk_uv_section_view_t *view);

/** \brief Get one source's model and coordinate count; failure preserves info. */
int pvr_chunk_uv_section_source_get(const pvr_chunk_uv_section_view_t *view,
    size_t source, pvr_chunk_uv_asset_info_t *info);

/** \brief Find a layer binding in logarithmic time; ENOENT means canonical.
    Failure preserves source. No rescan or allocation.
*/
int pvr_chunk_uv_section_find(const pvr_chunk_uv_section_view_t *view,
                             uint32_t layer, uint32_t *source);

/** \brief Decode one complete source into caller-owned runtime UV pairs.
    Requires capacity >= the source's UV-pair count. Output cannot overlap
    view/bytes;
    failures preserve output. Bind with pvr_chunk_uv_source_init() afterward.
*/
int pvr_chunk_uv_section_decode(const pvr_chunk_uv_section_view_t *view,
    size_t source, pvr_chunk_uv_t *uv, size_t capacity);

/** \brief Load-time gate against PML1 and ordered model views.
    Revalidates sections, PML1 ranges and every source's ordinary-model count;
    each binding's model must equal its layer's model. Texture/recipe admission
    remains separate. Required PCM2 feature acknowledgment alone is not this
    semantic check. No output, allocation, pinning or automatic scene loading.
*/
int pvr_chunk_uv_section_validate_layers(const pvr_chunk_uv_section_view_t *view,
    const pvr_chunk_layer_section_view_t *layers,
    const pvr_chunk_model_view_t *models, size_t model_count);
/** @} */
__END_DECLS
#endif
