/* KallistiOS ##version##

   dc/pvr_chunk_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_asset.h
    \brief   Versioned containers for independently stored compact-model data.
    \ingroup pvr_chunk_model

    A compact-model asset keeps vertex and polygon streams independently
    addressable. PCM1 stores those two streams in a small fixed header. PCM2
    adds a checked directory for optional resources, deformation, hierarchy,
    animation, collision, cache, or application sections. Every section may
    be stored raw or through an optional codec. Container parsing and raw
    loading require no allocation and no compression library. Decoding is
    supplied explicitly by the caller.
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

/** \brief Little-endian bytes `PCM1` at the start of a version-one asset. */
#define PVR_CHUNK_ASSET_MAGIC UINT32_C(0x314d4350)

/** \brief Little-endian bytes `PCM2` at the start of a directory asset. */
#define PVR_CHUNK_ASSET_DIRECTORY_MAGIC UINT32_C(0x324d4350)

/** \brief Current compact-model asset format version. */
#define PVR_CHUNK_ASSET_VERSION 1u

/** \brief Size of the version-one fixed header in bytes. */
#define PVR_CHUNK_ASSET_HEADER_BYTES 96u

/** \brief Version of the extensible section-directory container. */
#define PVR_CHUNK_ASSET_DIRECTORY_VERSION 2u

/** \brief Fixed header bytes in a section-directory container. */
#define PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES 64u

/** \brief Bytes in one section-directory descriptor. */
#define PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES 32u

/** \brief Required alignment of stored sections and decode workspace. */
#define PVR_CHUNK_ASSET_ALIGNMENT 32u

/** \brief Storage codec assigned independently to each model stream. */
typedef enum pvr_chunk_asset_codec {
    PVR_CHUNK_ASSET_CODEC_RAW = 0,
    PVR_CHUNK_ASSET_CODEC_LZ4_FRAME = 1
} pvr_chunk_asset_codec_t;

/** \brief Stable semantic identifiers for section-directory entries.

    Vertex and polygon streams are required exactly once. Other section types
    are optional and may appear more than once. Unknown nonzero identifiers
    remain queryable so newer host tools do not make older loaders reject an
    otherwise usable model.
*/
typedef enum pvr_chunk_asset_section_type {
    PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM = 1,
    PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM = 2,
    PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE = 3,
    PVR_CHUNK_ASSET_SECTION_VOLUME_DATA = 4,
    PVR_CHUNK_ASSET_SECTION_SKIN4 = 5,
    PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL = 6,
    PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS = 7,
    PVR_CHUNK_ASSET_SECTION_HIERARCHY = 8,
    PVR_CHUNK_ASSET_SECTION_ANIMATION = 9,
    PVR_CHUNK_ASSET_SECTION_COOKED_CACHE = 10,
    PVR_CHUNK_ASSET_SECTION_SKELETON = 11
} pvr_chunk_asset_section_type_t;

/** \brief First section identifier reserved for application-defined data. */
#define PVR_CHUNK_ASSET_SECTION_APPLICATION UINT32_C(0x80000000)

/** \brief Checked view of one stored model stream. */
typedef struct pvr_chunk_asset_section {
    const void *stored_data;
    size_t stored_bytes;
    size_t decoded_bytes;
    uint32_t decoded_crc32;
    uint32_t dictionary_id;
    pvr_chunk_asset_codec_t codec;
    uint32_t type;       /**< pvr_chunk_asset_section_type_t or extension. */
    uint32_t flags;      /**< Must be zero in the current directory revision. */
    size_t alignment;    /**< Required decoded-data alignment, power of two. */
} pvr_chunk_asset_section_t;

/** \brief Immutable view of a checked compact-model asset. */
typedef struct pvr_chunk_asset_view {
    const void *data;
    size_t size;
    pvr_chunk_asset_section_t vertex;
    pvr_chunk_asset_section_t polygon;
    float center[3];
    float radius;
    const void *section_directory; /**< NULL for synthetic PCM1 sections. */
    size_t section_directory_bytes;
    size_t section_count;          /**< Includes required model streams. */
    uint16_t version;              /**< 1 for PCM1 or 2 for PCM2. */
    uint16_t header_bytes;
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

/** \brief Workspace needed to materialize one directory section. */
typedef struct pvr_chunk_asset_section_workspace_requirements {
    size_t alignment;
    size_t bytes;
    int copies;
} pvr_chunk_asset_section_workspace_requirements_t;

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

/** \brief Return one checked section by directory order.

    PCM1 exposes its two fixed streams as synthetic entries zero and one.
    PCM2 entries are ordered by increasing stored-data offset. The returned
    section borrows immutable bytes from the asset.

    \retval 0 Success.
    \retval -1 Failure with errno set to EINVAL, EILSEQ, ENOTSUP, or ENOENT.
*/
int pvr_chunk_asset_section_get(const pvr_chunk_asset_view_t *view,
                                size_t index,
                                pvr_chunk_asset_section_t *section);

/** \brief Find the Nth checked section with one semantic type.

    \a ordinal is zero-based. This permits several animation, morph, resource,
    or application sections without adding type-specific pointers to the
    fixed asset view.
*/
int pvr_chunk_asset_section_find(const pvr_chunk_asset_view_t *view,
                                 uint32_t type, size_t ordinal,
                                 pvr_chunk_asset_section_t *section);

/** \brief Query workspace for one checked section by directory index.

    Raw naturally aligned bytes are borrowed with a zero-byte requirement.
    Compressed or unaligned bytes require one 32-byte-aligned decoded span.
*/
int pvr_chunk_asset_section_workspace_query(
    const pvr_chunk_asset_view_t *view, size_t index,
    pvr_chunk_asset_section_workspace_requirements_t *requirements);

/** \brief Materialize and CRC-check one checked section.

    On success, \a decoded either borrows immutable raw asset bytes or points
    to the supplied workspace. No allocation or section-specific parsing is
    performed. Workspace may be NULL only when the corresponding query reports
    zero bytes, and may not overlap the source asset.
*/
int pvr_chunk_asset_section_load(
    const pvr_chunk_asset_view_t *view, size_t index,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes, const void **decoded);

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
    thread, fiber, or service is created. Workspace may not overlap the source
    asset.
*/
int pvr_chunk_asset_load(const pvr_chunk_asset_view_t *view,
                         pvr_chunk_asset_decoder_t decoder,
                         void *decoder_data, void *workspace,
                         size_t workspace_bytes,
                         pvr_chunk_model_view_t *model_view);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_ASSET_H */
