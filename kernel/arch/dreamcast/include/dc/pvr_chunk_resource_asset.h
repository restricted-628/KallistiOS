/* KallistiOS ##version##

   dc/pvr_chunk_resource_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_resource_asset.h
    \brief   Serialized texture requirements for compact assets.
    \ingroup pvr_chunk_binding

    The section is a pointer-free manifest over the stable texture identifiers
    already encoded by compact polygon streams. It adds early enumeration and
    admission without taking ownership of names, surfaces, VRAM, or lifetime.
*/

#ifndef __DC_PVR_CHUNK_RESOURCE_ASSET_H
#define __DC_PVR_CHUNK_RESOURCE_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model.h>

struct pvr_chunk_texture_table_view;
struct pvr_chunk_residency_binding;

/** \addtogroup pvr_chunk_binding
    @{
*/

/** \brief Little-endian bytes `PRT1` at the start of a resource section. */
#define PVR_CHUNK_RESOURCE_SECTION_MAGIC UINT32_C(0x31545250)

/** \brief Current serialized resource-section version. */
#define PVR_CHUNK_RESOURCE_SECTION_VERSION 1u

/** \brief Fixed serialized resource-section header size. */
#define PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES 48u

/** \brief Fixed pointer-free resource-entry size. */
#define PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES 8u

/** \brief Ways one texture identifier is consumed by the model. */
typedef enum pvr_chunk_resource_usage {
    PVR_CHUNK_RESOURCE_PRIMARY = 1u << 0,
    PVR_CHUNK_RESOURCE_SECONDARY = 1u << 1
} pvr_chunk_resource_usage_t;

/** \brief One decoded texture requirement. */
typedef struct pvr_chunk_resource_entry {
    uint16_t identifier;
    uint16_t usage; /**< Bitwise pvr_chunk_resource_usage_t. */
} pvr_chunk_resource_entry_t;

/** \brief Checked immutable view of one serialized resource manifest. */
typedef struct pvr_chunk_resource_section_view {
    const void *data;
    size_t size;
    const void *entries;
    size_t entry_count;
    uint16_t version;
} pvr_chunk_resource_section_view_t;

/** \brief Parse and completely validate one resource section.

    Entries must be strictly increasing, unique, in the compact 13-bit
    identifier range, and carry at least one known usage bit. Reserved bytes,
    framing, and both checksums are verified before the view changes.
    Source bytes must remain immutable while the view is used.
*/
int pvr_chunk_resource_section_open(
    const void *data, size_t size,
    pvr_chunk_resource_section_view_t *view);

/** \brief Decode one admitted requirement by index. */
int pvr_chunk_resource_section_entry_get(
    const pvr_chunk_resource_section_view_t *view, size_t index,
    pvr_chunk_resource_entry_t *entry);

/** \brief Find one requirement using bounded binary search. */
int pvr_chunk_resource_section_find(
    const pvr_chunk_resource_section_view_t *view, uint16_t identifier,
    pvr_chunk_resource_entry_t *entry);

/** \brief Verify that a manifest exactly describes one admitted model.

    Every texture record must resolve in the manifest, and every manifest
    usage bit must be exercised by the model. This is an explicit load-time
    integrity gate; it performs no allocation and does not bind a surface.
*/
int pvr_chunk_resource_section_validate_model(
    const pvr_chunk_resource_section_view_t *view,
    const pvr_chunk_model_view_t *model);

/** \brief Verify that every required identifier has a checked surface. */
int pvr_chunk_resource_section_validate_table(
    const pvr_chunk_resource_section_view_t *view,
    const struct pvr_chunk_texture_table_view *textures);

/** \brief Pin every required identifier in an existing residency adapter.

    Successfully acquired pins remain recorded if a later requirement fails,
    matching pvr_chunk_residency_binding_prepare_model(). The caller releases
    them with pvr_chunk_residency_binding_release().
*/
int pvr_chunk_resource_section_prepare_residency(
    const pvr_chunk_resource_section_view_t *view,
    struct pvr_chunk_residency_binding *binding);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_RESOURCE_ASSET_H */
