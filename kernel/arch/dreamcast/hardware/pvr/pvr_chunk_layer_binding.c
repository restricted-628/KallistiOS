/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_layer_asset.h>
#include <dc/pvr_chunk_binding.h>
#include <errno.h>

int pvr_chunk_layer_section_validate_table(
    const pvr_chunk_layer_section_view_t *view,
    const pvr_chunk_texture_table_view_t *textures) {
    pvr_chunk_layer_section_view_t checked;
    pvr_chunk_texture_table_view_t admitted;
    if(!view || !textures) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_layer_section_open(view->data, view->size, &checked) < 0 ||
       pvr_chunk_texture_table_open(&textures->table, &admitted) < 0)
        return -1;
    for(size_t i = 0; i < checked.entry_count; ++i) {
        pvr_chunk_layer_entry_t entry;
        const pvr_chunk_texture_binding_t *texture;
        if(pvr_chunk_layer_section_entry_get(&checked, i, &entry) < 0 ||
           pvr_chunk_texture_table_find(&admitted,
               entry.layer.texture.identifier, &texture) < 0)
            return -1;
    }
    return 0;
}

int pvr_chunk_layer_section_prepare_residency(
    const pvr_chunk_layer_section_view_t *view,
    pvr_chunk_residency_binding_t *binding) {
    pvr_chunk_layer_section_view_t checked;
    if(!view || !binding) {
        errno = EINVAL;
        return -1;
    }
    /* Validate the whole section before ownership changes; the adapter keeps
       successful pins on later acquisition failure, just as for base models. */
    if(pvr_chunk_layer_section_open(view->data, view->size, &checked) < 0)
        return -1;
    for(size_t i = 0; i < checked.entry_count; ++i) {
        pvr_chunk_layer_entry_t entry;
        if(pvr_chunk_layer_section_entry_get(&checked, i, &entry) < 0 ||
           pvr_chunk_residency_binding_prepare_identifier(
               binding, entry.layer.texture.identifier) < 0)
            return -1;
    }
    return 0;
}
