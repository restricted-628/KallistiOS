/* KallistiOS ##version##

   dc/pvr_chunk_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_asset.h
    \brief   Versioned containers for independently stored compact-model data.
    \ingroup pvr_chunk_model

    A compact-model asset keeps vertex and polygon streams independently
    addressable. Either stream may be stored raw or through an optional codec.
    Container parsing and raw loading require no allocation and no compression
    library. Decoding is supplied explicitly by the caller.
*/

#ifndef __DC_PVR_CHUNK_ASSET_H
#define __DC_PVR_CHUNK_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PCM1` at the start of every asset. */
#define PVR_CHUNK_ASSET_MAGIC UINT32_C(0x314d4350)

/** \brief Current compact-model asset format version. */
#define PVR_CHUNK_ASSET_VERSION 1u

/** \brief Size of the version-one fixed header in bytes. */
#define PVR_CHUNK_ASSET_HEADER_BYTES 96u

/** \brief Required alignment of stored sections and decode workspace. */
#define PVR_CHUNK_ASSET_ALIGNMENT 32u

/** \brief Storage codec assigned independently to each model stream. */
typedef enum pvr_chunk_asset_codec {
    PVR_CHUNK_ASSET_CODEC_RAW = 0,
    PVR_CHUNK_ASSET_CODEC_LZ4_FRAME = 1
} pvr_chunk_asset_codec_t;

/** \brief Checked view of one stored model stream. */
typedef struct pvr_chunk_asset_section {
    const void *stored_data;
    size_t stored_bytes;
    size_t decoded_bytes;
    uint32_t decoded_crc32;
    uint32_t dictionary_id;
    pvr_chunk_asset_codec_t codec;
} pvr_chunk_asset_section_t;

/** \brief Immutable view of a checked compact-model asset. */
typedef struct pvr_chunk_asset_view {
    const void *data;
    size_t size;
    pvr_chunk_asset_section_t vertex;
    pvr_chunk_asset_section_t polygon;
    float center[3];
    float radius;
} pvr_chunk_asset_view_t;

/** \brief Caller-owned decode workspace requirement. */
typedef struct pvr_chunk_asset_workspace_requirements {
    size_t alignment;
    size_t vertex_offset;
    size_t polygon_offset;
    size_t bytes;
    int copies_vertex;
    int copies_polygon;
} pvr_chunk_asset_workspace_requirements_t;

/** \brief Decode one non-raw section into an exact caller-owned buffer.

    The callback must produce exactly section->decoded_bytes bytes or fail.
    It runs synchronously in the context that called pvr_chunk_asset_load().
    A codec implementation may use section->dictionary_id to select external
    dictionary data. Raw sections never invoke this callback.
*/
typedef int (*pvr_chunk_asset_decoder_t)(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes, void *data);

/** \brief Parse and structurally validate a bounded compact-model asset.

    This checks framing, arithmetic, section separation, fixed-header CRC,
    codec fields, finite bounds, and natural stream sizes. It does not decode
    compressed data or validate compact-model records.

    \retval 0 Success.
    \retval -1 Failure with errno set to EINVAL, EILSEQ, or ENOTSUP.
*/
int pvr_chunk_asset_open(const void *data, size_t size,
                         pvr_chunk_asset_view_t *view);

/** \brief Query exact workspace required to make both streams usable.

    A raw section is borrowed directly when its address has natural alignment.
    Compressed or unaligned sections are placed in 32-byte-aligned workspace.
*/
int pvr_chunk_asset_workspace_query(
    const pvr_chunk_asset_view_t *view,
    pvr_chunk_asset_workspace_requirements_t *requirements);

/** \brief Materialize and fully admit a compact model from an asset.

    Workspace may be NULL only when the query reports zero bytes. It must be
    32-byte aligned otherwise. The decoded bytes are CRC-checked before the
    normal compact-model validator publishes \a model_view. No hidden memory,
    thread, fiber, or service is created.
*/
int pvr_chunk_asset_load(const pvr_chunk_asset_view_t *view,
                         pvr_chunk_asset_decoder_t decoder,
                         void *decoder_data, void *workspace,
                         size_t workspace_bytes,
                         pvr_chunk_model_view_t *model_view);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_ASSET_H */
