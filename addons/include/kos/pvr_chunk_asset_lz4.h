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
