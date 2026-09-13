/* KallistiOS ##version##

   dc/pvr_chunk_layer_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file dc/pvr_chunk_layer_asset.h
    \brief Pointer-free auxiliary layer associations for Compact draws.
    \ingroup pvr_chunk_binding
*/
#ifndef __DC_PVR_CHUNK_LAYER_ASSET_H
#define __DC_PVR_CHUNK_LAYER_ASSET_H

#include <dc/pvr_chunk_binding.h>

__BEGIN_DECLS
/** \addtogroup pvr_chunk_binding
    @{
*/
#define PVR_CHUNK_LAYER_SECTION_MAGIC UINT32_C(0x314c4d50) /**< PML1. */
#define PVR_CHUNK_LAYER_SECTION_VERSION 1u
#define PVR_CHUNK_LAYER_SECTION_HEADER_BYTES 32u
#define PVR_CHUNK_LAYER_SECTION_ENTRY_BYTES 64u

/** \brief One layer associated with a contiguous range of source strips.

    Model ordinals refer to the loader's ordered model-view array (PMT1 order
    for a multi-model asset), not texture or polygon-section ordinals. Strips
    are numbered before filtering, clipping or topology expansion; ordinary
    prepared caches retain that order. Ranges must be sorted by model/first
    strip and must not overlap. One layer per strip is supported in version 1.
*/
typedef struct pvr_chunk_layer_entry {
    uint32_t model;
    uint32_t first_strip;
    uint32_t strip_count;
    pvr_chunk_material_layer_t layer;
} pvr_chunk_layer_entry_t;

/** \brief Admitted immutable serialized view; initialize only with open().

    Keep source bytes and this view immutable and alive for every accessor.
    Accessors do not repeat CRC scans. Copy decoded entries into caller-owned
    preparation data before changing the source or filtering/reordering draws.
*/
typedef struct pvr_chunk_layer_section_view {
    const void *data;
    size_t size;
    size_t entry_count;
} pvr_chunk_layer_section_view_t;

/** \brief Compute encoded size, rejecting zero or unrepresentable counts. */
int pvr_chunk_layer_section_query(size_t count, size_t *bytes);

/** \brief Serialize sorted runtime entries into caller-owned storage.

    Framing, CRCs and reserved bytes are written explicitly in little endian;
    this never dumps native structs. Capacity, metadata, ordering and aliasing
    are checked before any write. Output must be disjoint from entries. Every
    failure preserves output; allocation and texture residency are not involved.
*/
int pvr_chunk_layer_section_write(const pvr_chunk_layer_entry_t *entries,
                                  size_t count, void *data, size_t capacity);

/** \brief Validate exact framing, CRCs, ranges, reserved fields and metadata.

    Version 1 uses the draw's canonical UVs plus the layer's affine transform.
    Independent UV selectors are rejected, not discarded. This validates the
    section, not a concrete model array or VRAM table. Failure preserves view;
    view must not overlap source bytes.
*/
int pvr_chunk_layer_section_open(const void *data, size_t size,
                                 pvr_chunk_layer_section_view_t *view);

/** \brief Decode an entry from an admitted immutable view.
    Output must not overlap the view or its source bytes. Failure preserves it.
*/
int pvr_chunk_layer_section_entry_get(
    const pvr_chunk_layer_section_view_t *view, size_t index,
    pvr_chunk_layer_entry_t *entry);

/** \brief Find a source strip's layer with logarithmic range lookup.
    Missing associations report ENOENT and preserve output.
*/
int pvr_chunk_layer_section_find(const pvr_chunk_layer_section_view_t *view,
                                 uint32_t model, uint32_t strip,
                                 pvr_chunk_layer_entry_t *entry);

/** \brief Check every association against a concrete ordered model array.

    Revalidates referenced models and checks source strip bounds. This is an
    explicit load-time gate, not a render-loop call. Texture existence and
    recipe/profile admission are checked later by resolve_layer(); callers
    must enumerate and pin these auxiliary identifiers separately from PRT1.
    Ordinary scene loading does not automatically consume this section or
    select a recipe. No importer may silently discard these associations.
*/
int pvr_chunk_layer_section_validate_models(
    const pvr_chunk_layer_section_view_t *view,
    const pvr_chunk_model_view_t *models, size_t model_count);

/** @} */
__END_DECLS
#endif /* __DC_PVR_CHUNK_LAYER_ASSET_H */
