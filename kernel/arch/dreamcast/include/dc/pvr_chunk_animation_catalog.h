/* KallistiOS ##version##

   dc/pvr_chunk_animation_catalog.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_animation_catalog.h
    \brief   Logical animation catalogs for compact assets.
    \ingroup pvr_chunk_model

    A catalog keeps logical clips independent from their serialized channel
    families. Each pointer-free record names optional PAT1 transform and PMW1
    morph-section ordinals, so mixed and single-family clips require no empty
    placeholder sections.
*/

#ifndef __DC_PVR_CHUNK_ANIMATION_CATALOG_H
#define __DC_PVR_CHUNK_ANIMATION_CATALOG_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PAC1` at the start of a catalog. */
#define PVR_CHUNK_ANIMATION_CATALOG_MAGIC UINT32_C(0x31434150)

/** \brief Current serialized catalog version. */
#define PVR_CHUNK_ANIMATION_CATALOG_VERSION 1u

/** \brief Fixed serialized catalog header size. */
#define PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES 64u

/** \brief Fixed serialized logical-clip record size. */
#define PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES 32u

/** \brief Serialized ordinal for an absent channel-family section. */
#define PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE UINT32_MAX

/** \brief One decoded logical animation clip.

    \a name borrows an exact, non-NUL-terminated byte span from the catalog.
    An unnamed clip has name_bytes zero and remains addressable by index.
*/
typedef struct pvr_chunk_animation_catalog_clip {
    uint32_t transform_ordinal;
    uint32_t morph_ordinal;
    const char *name;
    size_t name_bytes;
    float start_time;
    float end_time;
} pvr_chunk_animation_catalog_clip_t;

/** \brief Immutable checked view of one serialized animation catalog. */
typedef struct pvr_chunk_animation_catalog_view {
    const void *data;
    size_t size;
    const void *records;
    const char *strings;
    size_t clip_count;
    size_t string_bytes;
} pvr_chunk_animation_catalog_view_t;

/** \brief Parse and completely validate one pointer-free catalog.

    Validation covers framing, checksums, canonical packed names, unique
    nonempty names, finite ordered time ranges, flags, and the requirement that
    every logical clip reference at least one channel-family section.
*/
int pvr_chunk_animation_catalog_open(
    const void *data, size_t size,
    pvr_chunk_animation_catalog_view_t *view);

/** \brief Decode one admitted logical clip by source order. */
int pvr_chunk_animation_catalog_clip_get(
    const pvr_chunk_animation_catalog_view_t *view, size_t index,
    pvr_chunk_animation_catalog_clip_t *clip);

/** \brief Find one uniquely named admitted clip by exact byte span.

    \a name_bytes must be nonzero. On success, \a index receives the clip's
    source-order index when non-NULL.
*/
int pvr_chunk_animation_catalog_find(
    const pvr_chunk_animation_catalog_view_t *view,
    const char *name, size_t name_bytes, size_t *index,
    pvr_chunk_animation_catalog_clip_t *clip);

/** \brief Verify every catalog ordinal resolves in a checked PCM2 asset.

    This validates directory relationships without decoding the referenced
    PAT1 or PMW1 data, so compressed animation sections remain supported.
*/
int pvr_chunk_animation_catalog_validate_asset(
    const pvr_chunk_animation_catalog_view_t *view,
    const pvr_chunk_asset_view_t *asset);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_ANIMATION_CATALOG_H */
