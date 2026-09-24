/* KallistiOS ##version##

   dc/pvr/pvr_chunk_resource_binding.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_binding.h>
#include <dc/pvr_chunk_resource_asset.h>

#include <errno.h>
#include <stdint.h>

static uint16_t entry_identifier(
        const pvr_chunk_resource_section_view_t *view, size_t index) {
    const uint8_t *entry = (const uint8_t *)view->entries +
        index * PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES;

    return (uint16_t)entry[0] | (uint16_t)((uint16_t)entry[1] << 8);
}

int pvr_chunk_resource_section_validate_table(
        const pvr_chunk_resource_section_view_t *view,
        const pvr_chunk_texture_table_view_t *textures) {
    pvr_chunk_texture_table_view_t admitted;
    pvr_chunk_resource_section_view_t checked;
    size_t index;

    if(!view || !textures || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_resource_section_open(view->data, view->size, &checked) < 0 ||
       pvr_chunk_texture_table_open(&textures->table, &admitted) < 0)
        return -1;
    for(index = 0; index < checked.entry_count; ++index) {
        const pvr_chunk_texture_binding_t *texture;

        if(pvr_chunk_texture_table_find(
               &admitted, entry_identifier(&checked, index),
               &texture) < 0)
            return -1;
    }
    return 0;
}

int pvr_chunk_resource_section_prepare_residency(
        const pvr_chunk_resource_section_view_t *view,
        pvr_chunk_residency_binding_t *binding) {
    pvr_chunk_resource_section_view_t checked;
    size_t index;

    if(!view || !binding || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_resource_section_open(view->data, view->size, &checked) < 0)
        return -1;
    for(index = 0; index < checked.entry_count; ++index) {
        if(pvr_chunk_residency_binding_prepare_identifier(
               binding, entry_identifier(&checked, index)) < 0)
            return -1;
    }
    return 0;
}
