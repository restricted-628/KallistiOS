/* KallistiOS ##version##

   dc/pvr_chunk_texture_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_texture_asset.h
    \brief   Serialized PVR-ready texture images for compact assets.
    \ingroup pvr_chunk_binding

    The texture-image section pairs stable compact texture identifiers with
    checked, already encoded PVR storage bytes. Parsing never allocates or
    uploads. Applications retain control over VRAM ownership, residency, and
    transfer scheduling.
*/

#ifndef __DC_PVR_CHUNK_TEXTURE_ASSET_H
#define __DC_PVR_CHUNK_TEXTURE_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dc/pvr/pvr_txr.h>

/** \addtogroup pvr_chunk_binding
    @{
*/

/** \brief Little-endian bytes `PTX1` at the start of a texture section. */
#define PVR_CHUNK_TEXTURE_SECTION_MAGIC UINT32_C(0x31585450)

/** \brief Current serialized texture-section version. */
#define PVR_CHUNK_TEXTURE_SECTION_VERSION 1u

/** \brief Fixed serialized texture-section header size. */
#define PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES 64u

/** \brief Fixed pointer-free texture-entry size. */
#define PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES 32u

/** \brief Serialized texture-image properties. */
typedef enum pvr_chunk_texture_image_flag {
    PVR_CHUNK_TEXTURE_IMAGE_MIPMAPPED = 1u << 0
} pvr_chunk_texture_image_flag_t;

/** \brief One decoded PVR-ready texture image. */
typedef struct pvr_chunk_texture_image {
    uint16_t identifier;
    uint16_t codebook_entries;
    uint32_t width;
    uint32_t height;
    pvr_txr_surface_format_t format;
    pvr_txr_surface_layout_t layout;
    bool mipmapped;
    const void *data;
    size_t data_size;
} pvr_chunk_texture_image_t;

/** \brief Checked immutable view of one serialized texture-image section. */
typedef struct pvr_chunk_texture_section_view {
    const void *data;
    size_t size;
    const void *entries;
    size_t entry_count;
    uint16_t version;
} pvr_chunk_texture_section_view_t;

/** \brief Parse and completely validate one texture-image section.

    Entries must be strictly increasing and unique. Every image is bounded,
    naturally aligned within the section, non-overlapping, individually
    checksummed, and exactly described by the checked PVR surface layout.
    Source bytes must remain immutable while the view is used.
*/
int pvr_chunk_texture_section_open(
    const void *data, size_t size,
    pvr_chunk_texture_section_view_t *view);

/** \brief Decode one admitted image by index. */
int pvr_chunk_texture_section_entry_get(
    const pvr_chunk_texture_section_view_t *view, size_t index,
    pvr_chunk_texture_image_t *image);

/** \brief Find one admitted image using bounded binary search. */
int pvr_chunk_texture_section_find(
    const pvr_chunk_texture_section_view_t *view, uint16_t identifier,
    pvr_chunk_texture_image_t *image);

/** \brief Initialize an unbound checked surface from an admitted image.

    The returned descriptor owns no VRAM. It can be allocated, bound to a
    reservation, or used as a fixed-residency prototype through the ordinary
    checked texture-surface APIs.
*/
int pvr_chunk_texture_image_surface_init(
    const pvr_chunk_texture_image_t *image, pvr_txr_surface_t *surface);

/** \brief Upload one admitted image to a compatible bound surface.

    Descriptor metadata and byte size must match exactly. This performs no
    allocation and delegates transfer ordering to pvr_txr_surface_upload().
*/
int pvr_chunk_texture_image_upload(
    const pvr_chunk_texture_image_t *image,
    const pvr_txr_surface_t *surface, pvr_txr_transfer_t transfer);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_TEXTURE_ASSET_H */
