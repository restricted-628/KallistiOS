/* KallistiOS ##version##

   dc/pvr_chunk_volume_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_volume_asset.h
    \brief   Serialized collision and modifier volumes for compact assets.
    \ingroup pvr_chunk_model

    A volume section stores exact compact volume records in a pointer-free
    directory. Records continue to use pvr_chunk_volume_iterator_t, so
    collision queries and modifier rendering share topology, winding, and
    per-triangle user-word semantics instead of maintaining parallel formats.
*/

#ifndef __DC_PVR_CHUNK_VOLUME_ASSET_H
#define __DC_PVR_CHUNK_VOLUME_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PVL1` at the start of a volume section. */
#define PVR_CHUNK_VOLUME_SECTION_MAGIC UINT32_C(0x314c5650)

/** \brief Current serialized volume-section version. */
#define PVR_CHUNK_VOLUME_SECTION_VERSION 1u

/** \brief Fixed serialized volume-section header size. */
#define PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES 48u

/** \brief Fixed pointer-free record-span size. */
#define PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES 8u

/** \brief One pointer-free span in the section's packed 16-bit word stream. */
typedef struct pvr_chunk_volume_section_record {
    uint32_t first_word;
    uint32_t word_count;
} pvr_chunk_volume_section_record_t;

/** \brief Checked immutable view of one serialized volume set. */
typedef struct pvr_chunk_volume_section_view {
    const void *data;
    size_t size;
    const void *records;
    size_t record_count;
    const uint16_t *words;
    size_t word_count;
    size_t triangle_count;
    uint16_t version;
} pvr_chunk_volume_section_view_t;

/** \brief Caller-owned iterator across every record in a volume section.

    Applications should treat every field as private. Returned triangles use
    the same borrowed user-word views and record-boundary flag as
    pvr_chunk_volume_iterator_next().
*/
typedef struct pvr_chunk_volume_section_iterator {
    pvr_chunk_volume_section_view_t view;
    pvr_chunk_volume_iterator_t volume;
    size_t record_index;
    int active;
} pvr_chunk_volume_section_iterator_t;

/** \brief Parse and completely validate one serialized volume section.

    Validation covers framing, checksums, reserved fields, gapless record
    spans, exact compact-record sizes, and every triangle/quad/strip payload.
    The source must be at least 16-bit aligned and remain immutable while the
    view or any record borrowed from it is in use.
*/
int pvr_chunk_volume_section_open(
    const void *data, size_t size, pvr_chunk_volume_section_view_t *view);

/** \brief Return one admitted compact volume record by section index. */
int pvr_chunk_volume_section_record_get(
    const pvr_chunk_volume_section_view_t *view, size_t index,
    pvr_chunk_record_t *record);

/** \brief Initialize a flattened iterator over an admitted volume section. */
int pvr_chunk_volume_section_iterator_init(
    pvr_chunk_volume_section_iterator_t *iterator,
    const pvr_chunk_volume_section_view_t *view);

/** \brief Return the next triangle through the existing volume decoder.

    \retval 1  A triangle was returned.
    \retval 0  Every section record was already consumed.
    \retval -1 Invalid iterator state or concurrently modified source bytes.
*/
int pvr_chunk_volume_section_iterator_next(
    pvr_chunk_volume_section_iterator_t *iterator,
    pvr_chunk_volume_triangle_t *triangle);

/** \brief Verify that every section index resolves in one admitted model.

    This is the model-binding gate for a standalone collision section. It
    re-admits both inputs before checking all expanded triangles. No storage
    is allocated and neither input is modified.
*/
int pvr_chunk_volume_section_validate_model(
    const pvr_chunk_volume_section_view_t *view,
    const pvr_chunk_model_view_t *model);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_VOLUME_ASSET_H */
