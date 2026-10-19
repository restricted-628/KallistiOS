/* KallistiOS ##version##

   dc/pvr_chunk_cache_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_cache_asset.h
    \brief   Portable cooked-cache sections for compact PVR assets.
    \ingroup pvr_chunk_render

    A cooked-cache section stores the admitted, stream-independent geometry
    used by one existing compact-model cache family. The serialized form is
    little-endian and pointer-free; materialization reconstructs the normal
    caller-owned cache objects consumed by the existing emitters.
*/

#ifndef __DC_PVR_CHUNK_CACHE_ASSET_H
#define __DC_PVR_CHUNK_CACHE_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_cache.h>

/** \addtogroup pvr_chunk_render
    @{
*/

/** \brief Little-endian bytes `PCC1` at the start of a cooked cache. */
#define PVR_CHUNK_CACHE_SECTION_MAGIC UINT32_C(0x31434350)

/** \brief Current serialized cooked-cache version. */
#define PVR_CHUNK_CACHE_SECTION_VERSION 1u

/** \brief Fixed serialized cooked-cache header size. */
#define PVR_CHUNK_CACHE_SECTION_HEADER_BYTES 128u

/** \brief Serialized bytes in one ordinary/two-volume strip descriptor. */
#define PVR_CHUNK_CACHE_SECTION_STRIP_BYTES 160u

/** \brief Serialized bytes in one modifier-triangle descriptor. */
#define PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES 32u

/** \brief Cooked-cache payload family. */
typedef enum pvr_chunk_cache_section_kind {
    PVR_CHUNK_CACHE_SECTION_ORDINARY = 1,
    PVR_CHUNK_CACHE_SECTION_TWO_VOLUME = 2,
    PVR_CHUNK_CACHE_SECTION_MODIFIER = 3
} pvr_chunk_cache_section_kind_t;

/** \brief Checked immutable view of one pointer-free cooked cache. */
typedef struct pvr_chunk_cache_section_view {
    const void *data;
    size_t size;
    const void *descriptors;
    const void *vertices;
    const void *deform_vertices;
    const void *source_indices;
    const void *user_words;
    size_t primary_count;   /**< Strips or modifier volumes. */
    size_t secondary_count; /**< Vertices or modifier triangles. */
    size_t tertiary_count;  /**< Maximum strip vertices or corners. */
    size_t user_word_count;
    float center[3];
    float radius;
    pvr_chunk_cache_section_kind_t kind;
    pvr_geometry_vertex_format_t format;
    uint16_t version;
} pvr_chunk_cache_section_view_t;

/** \brief Caller-owned materialization requirement. */
typedef struct pvr_chunk_cache_section_requirements {
    size_t alignment;
    size_t bytes;
    pvr_chunk_cache_section_kind_t kind;
} pvr_chunk_cache_section_requirements_t;

/** \brief Parse and completely validate one cooked-cache section.

    Framing, canonical offsets, reserved fields, checksums, counts, state,
    packet commands, finite geometry, strip coverage, and modifier topology
    are checked before \p view changes. The source remains caller-owned and
    immutable while the view is used.
*/
int pvr_chunk_cache_section_open(
    const void *data, size_t size, pvr_chunk_cache_section_view_t *view);

/** \brief Size ordinary native storage without admitting the PCC1 payload.

    Checks the fixed header, its CRC, canonical framing and size arithmetic;
    does not scan payload CRC, strips, packets or deformation data. Success is
    only an allocation requirement, not permission to draw. Apply the caller's
    allocation budget before allocating requirements->bytes at its alignment.
    \p size is the complete accessible section length, not buffer capacity or
    padded transport length. Input remains live and immutable for this call.
    Requirements must be naturally aligned and disjoint from input, and remain
    unchanged on failure. No allocation or input borrowing survives return.
*/
int pvr_chunk_cache_section_ordinary_query(
    const void *data, size_t size,
    pvr_chunk_cache_section_requirements_t *requirements);

/** \brief Admit, decode and publish an ordinary draw cache in one operation.

    Accepts a complete, CPU-readable, uncompressed PCC1 section. Storage/codec
    success and producer/callback handoff must precede entry; partial output,
    cancellation or terminal status alone does not establish readiness. Input
    stays live and immutable through validation and the last decode read.

    Independently checks the current header and fully validates its payload
    once, then constructs native layout and an admitted draw snapshot without
    repeating native semantic scans. No query result or caller-created marker
    confers admission. A different complete input after ordinary_query is
    allowed; capacity is recalculated. Existing outer-container and codec
    checks remain the producer's responsibility, even if their byte spans or
    algorithms overlap. PCC1 header/payload CRCs do not replace those checks.

    Storage must be 32-byte aligned and draw naturally aligned. All supplied
    source/storage/draw ranges must be representable and mutually disjoint.
    All fallible work precedes the first destination write; failure leaves
    storage and draw unchanged. No allocation occurs. Success retains no
    input pointer; other section/container borrowers may still retain input.

    The published snapshot and its native storage must remain alive and
    immutable through the last draw/callback. Reload is a fresh admission.
    Wait for all old readers before in-place reload; success invalidates old
    snapshots of that storage. Replacement storage may publish on success,
    with old storage retained until old users finish. The API neither pins
    buffers nor retires consumers. A checked cache can be copied from draw->cache.
*/
int pvr_chunk_cache_section_load_ordinary(
    const void *data, size_t size, void *storage, size_t storage_bytes,
    pvr_chunk_cache_draw_t *draw);

/** \brief Query exact 32-byte-aligned cache storage for one checked section. */
int pvr_chunk_cache_section_workspace_query(
    const pvr_chunk_cache_section_view_t *view,
    pvr_chunk_cache_section_requirements_t *requirements);

/** \brief Materialize an ordinary cache into caller-owned storage. */
int pvr_chunk_cache_section_materialize_ordinary(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes, pvr_chunk_model_cache_t *cache);

/** \brief Materialize a two-volume cache into caller-owned storage. */
int pvr_chunk_cache_section_materialize_two_volume(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes,
    pvr_chunk_two_volume_cache_t *cache);

/** \brief Materialize a modifier cache into caller-owned storage. */
int pvr_chunk_cache_section_materialize_modifier(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes,
    pvr_chunk_modifier_cache_t *cache);

/** \brief Query serialized bytes for one validated ordinary cache. */
int pvr_chunk_model_cache_section_query(
    const pvr_chunk_model_cache_t *cache, size_t *bytes);

/** \brief Serialize one validated ordinary cache in pointer-free form.

    The destination may be unaligned but must not overlap the cache descriptor
    or its caller-owned native storage. The completed bytes are reopened and
    fully validated before success is returned.
*/
int pvr_chunk_model_cache_section_serialize(
    const pvr_chunk_model_cache_t *cache, void *destination,
    size_t destination_bytes);

/** \brief Query serialized bytes for one validated two-volume cache. */
int pvr_chunk_two_volume_cache_section_query(
    const pvr_chunk_two_volume_cache_t *cache, size_t *bytes);

/** \brief Serialize one validated two-volume cache in pointer-free form.

    The destination follows the same non-overlap and self-validation contract
    as pvr_chunk_model_cache_section_serialize().
*/
int pvr_chunk_two_volume_cache_section_serialize(
    const pvr_chunk_two_volume_cache_t *cache, void *destination,
    size_t destination_bytes);

/** \brief Query serialized bytes for one validated modifier cache. */
int pvr_chunk_modifier_cache_section_query(
    const pvr_chunk_modifier_cache_t *cache, size_t *bytes);

/** \brief Serialize one validated modifier cache in pointer-free form.

    The destination follows the same non-overlap and self-validation contract
    as pvr_chunk_model_cache_section_serialize().
*/
int pvr_chunk_modifier_cache_section_serialize(
    const pvr_chunk_modifier_cache_t *cache, void *destination,
    size_t destination_bytes);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_CACHE_ASSET_H */
