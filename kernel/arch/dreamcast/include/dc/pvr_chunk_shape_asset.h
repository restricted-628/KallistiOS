/* KallistiOS ##version##

   dc/pvr_chunk_shape_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_shape_asset.h
    \brief   Serialized sparse morph targets for compact assets.
    \ingroup pvr_chunk_model

    The section stores pointer-free target spans followed by finite position
    and normal deltas. Checked data is materialized into the existing
    pvr_chunk_shape_set_t runtime representation using caller-owned storage.
*/

#ifndef __DC_PVR_CHUNK_SHAPE_ASSET_H
#define __DC_PVR_CHUNK_SHAPE_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_shape.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PMS1` at the start of a shape section. */
#define PVR_CHUNK_SHAPE_SECTION_MAGIC UINT32_C(0x31534d50)

/** \brief Current serialized shape-section version. */
#define PVR_CHUNK_SHAPE_SECTION_VERSION 1u

/** \brief Fixed serialized shape-section header size. */
#define PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES 48u

/** \brief Fixed serialized target-span size. */
#define PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES 8u

/** \brief Fixed serialized sparse-delta size. */
#define PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES 28u

/** \brief Pointer-free span naming one target's packed deltas. */
typedef struct pvr_chunk_shape_section_target {
    uint32_t first_delta;
    uint32_t delta_count;
} pvr_chunk_shape_section_target_t;

/** \brief Checked immutable view of one serialized shape set. */
typedef struct pvr_chunk_shape_section_view {
    const void *data;
    size_t size;
    const void *targets;
    size_t target_count;
    const void *deltas;
    size_t delta_count;
    uint16_t version;
} pvr_chunk_shape_section_view_t;

/** \brief Parse and completely validate one sparse shape section.

    Validation covers framing, checksums, reserved fields, gapless nonempty
    target spans, finite deltas, and strictly increasing vertex indices inside
    each target. Whether an index exists in a particular model is deliberately
    left to pvr_chunk_shape_bind().
*/
int pvr_chunk_shape_section_open(
    const void *data, size_t size, pvr_chunk_shape_section_view_t *view);

/** \brief Decode one admitted pointer-free target span by index. */
int pvr_chunk_shape_section_target_get(
    const pvr_chunk_shape_section_view_t *view, size_t index,
    pvr_chunk_shape_section_target_t *target);

/** \brief Decode one admitted sparse morph delta by packed index. */
int pvr_chunk_shape_section_delta_get(
    const pvr_chunk_shape_section_view_t *view, size_t index,
    pvr_chunk_shape_delta_t *delta);

/** \brief Materialize an admitted section into existing runtime shape types.

    The complete section and all capacities, alignments, and overlaps are
    checked before either output array is modified. Target pointers borrow the
    caller-owned delta array. The resulting set can be passed directly to
    pvr_chunk_shape_bind(). All output storage remains unchanged on failure.
*/
int pvr_chunk_shape_section_materialize(
    const pvr_chunk_shape_section_view_t *view,
    pvr_chunk_shape_target_t *targets, size_t target_capacity,
    pvr_chunk_shape_delta_t *deltas, size_t delta_capacity,
    pvr_chunk_shape_set_t *shapes);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_SHAPE_ASSET_H */
