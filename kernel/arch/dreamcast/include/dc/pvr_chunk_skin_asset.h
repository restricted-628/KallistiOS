/* KallistiOS ##version##

   dc/pvr_chunk_skin_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_skin_asset.h
    \brief   Serialized variable-influence skins for compact assets.
    \ingroup pvr_chunk_model

    The section preserves arbitrary influence counts as pointer-free,
    little-endian spans and weights. Checked data is materialized into the
    existing pvr_chunk_skin_general_t runtime representation using only
    caller-owned storage.
*/

#ifndef __DC_PVR_CHUNK_SKIN_ASSET_H
#define __DC_PVR_CHUNK_SKIN_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_skin.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PSG1` at the start of a general skin. */
#define PVR_CHUNK_SKIN_GENERAL_MAGIC UINT32_C(0x31475350)

/** \brief Current serialized general-skin version. */
#define PVR_CHUNK_SKIN_GENERAL_VERSION 1u

/** \brief Fixed serialized general-skin header size. */
#define PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES 48u

/** \brief Fixed serialized span size. */
#define PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES 8u

/** \brief Fixed serialized joint/weight size. */
#define PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES 4u

/** \brief Checked immutable view of a serialized general skin. */
typedef struct pvr_chunk_skin_general_section_view {
    const void *data;
    size_t size;
    const void *spans;
    size_t span_count;
    const void *weights;
    size_t weight_count;
    size_t joint_count;
    uint16_t version;
} pvr_chunk_skin_general_section_view_t;

/** \brief Parse and completely validate one general-skin section.

    Validation covers framing, checksums, reserved fields, strictly increasing
    vertex indices, gapless nonempty spans, joint bounds, nonzero weights, and
    an exact normalized sum for every vertex. Model coverage is deliberately
    left to pvr_chunk_skin_general_bind().
*/
int pvr_chunk_skin_general_section_open(
    const void *data, size_t size,
    pvr_chunk_skin_general_section_view_t *view);

/** \brief Decode one admitted span by index. */
int pvr_chunk_skin_general_section_span_get(
    const pvr_chunk_skin_general_section_view_t *view, size_t index,
    pvr_chunk_skin_span_t *span);

/** \brief Decode one admitted joint/weight pair by index. */
int pvr_chunk_skin_general_section_weight_get(
    const pvr_chunk_skin_general_section_view_t *view, size_t index,
    pvr_chunk_skin_weight_t *weight);

/** \brief Materialize an admitted section into existing runtime skin types.

    The complete section and all capacities, alignments, and overlaps are
    checked before either output array is modified. The resulting skin borrows
    the caller-owned arrays and can be passed directly to
    pvr_chunk_skin_general_query() or pvr_chunk_skin_general_bind().
    All output storage remains unchanged when the operation fails.
*/
int pvr_chunk_skin_general_section_materialize(
    const pvr_chunk_skin_general_section_view_t *view,
    pvr_chunk_skin_span_t *spans, size_t span_capacity,
    pvr_chunk_skin_weight_t *weights, size_t weight_capacity,
    pvr_chunk_skin_general_t *skin);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_SKIN_ASSET_H */
