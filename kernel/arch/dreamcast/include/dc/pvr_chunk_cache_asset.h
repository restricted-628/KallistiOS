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
