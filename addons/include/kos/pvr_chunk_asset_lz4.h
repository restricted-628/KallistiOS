/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/pvr_chunk_asset_lz4.h
    \brief   LZ4 Frame decoder for compact-model asset sections.
*/

#ifndef __KOS_PVR_CHUNK_ASSET_LZ4_H
#define __KOS_PVR_CHUNK_ASSET_LZ4_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Optional dictionary supplied to the LZ4 Frame decoder. */
typedef struct pvr_chunk_asset_lz4_dictionary {
    const void *data;
    size_t size;
    uint32_t id;
} pvr_chunk_asset_lz4_dictionary_t;

/** \brief Opaque incremental LZ4 Frame decode state. */
typedef struct pvr_chunk_asset_lz4_state pvr_chunk_asset_lz4_state_t;

/** \brief Coherent progress for one incremental section decode. */
typedef struct pvr_chunk_asset_lz4_progress {
    size_t source_bytes;
    size_t source_total;
    size_t output_bytes;
    size_t output_total;
    int complete;
} pvr_chunk_asset_lz4_progress_t;

/** \brief Incremental decoder needs more calls. */
#define PVR_CHUNK_ASSET_LZ4_MORE 0

/** \brief Incremental decoder completed and verified the section. */
#define PVR_CHUNK_ASSET_LZ4_COMPLETE 1

/** \brief Create incremental state for one complete resident LZ4 Frame.

    Source, destination, and optional dictionary storage are borrowed until
    pvr_chunk_asset_lz4_state_destroy(). Source and destination must not
    overlap. Creating state allocates only the upstream LZ4 Frame context.
*/
pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary);

/** \brief Decode up to one caller-selected output budget.

    A positive output budget bounds bytes published during this call. LZ4 may
    internally decode at most one frame block while satisfying a small output
    buffer, so CPU work is additionally bounded by the frame's block size.
    The converter emits 64 KiB independent blocks.

    \retval PVR_CHUNK_ASSET_LZ4_MORE More calls are required.
    \retval PVR_CHUNK_ASSET_LZ4_COMPLETE Output size and CRC are verified.
    \retval -1 Decode failed, with errno set.
*/
int pvr_chunk_asset_lz4_state_step(pvr_chunk_asset_lz4_state_t *state,
                                   size_t output_budget);

/** \brief Copy progress from an incremental decoder. */
int pvr_chunk_asset_lz4_state_get_progress(
    const pvr_chunk_asset_lz4_state_t *state,
    pvr_chunk_asset_lz4_progress_t *progress);

/** \brief Destroy incremental state without touching borrowed storage. */
void pvr_chunk_asset_lz4_state_destroy(
    pvr_chunk_asset_lz4_state_t *state);

/** \brief Decode exactly one LZ4 Frame compact-model section.

    Pass this function directly as pvr_chunk_asset_decoder_t. The callback
    context may be NULL for a frame without a dictionary, or point to one
    pvr_chunk_asset_lz4_dictionary_t. Frame checksums remain enabled.
*/
int pvr_chunk_asset_lz4_decode(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes, void *dictionary);

/** @} */

__END_DECLS

#endif /* __KOS_PVR_CHUNK_ASSET_LZ4_H */
