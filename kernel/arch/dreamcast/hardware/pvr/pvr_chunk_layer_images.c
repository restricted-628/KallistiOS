/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_chunk_layer_asset.h>
#include <dc/pvr_chunk_texture_asset.h>
#include <errno.h>

int pvr_chunk_layer_section_validate_images(
    const pvr_chunk_layer_section_view_t *layers,
    const pvr_chunk_texture_section_view_t *images) {
    pvr_chunk_layer_section_view_t checked_layers;
    pvr_chunk_texture_section_view_t checked_images;

    if(!layers || !images) {
        errno = EINVAL;
        return -1;
    }
    /* PRT1 remains the exact stream-use manifest. PML1 is an additional set
       of texture consumers, not a reason to reinterpret its usage bits. Check
       packaged images before the application acquires VRAM or uploads data. */
    if(pvr_chunk_layer_section_open(layers->data, layers->size,
                                    &checked_layers) < 0 ||
       pvr_chunk_texture_section_open(images->data, images->size,
                                      &checked_images) < 0)
        return -1;
    for(size_t i = 0; i < checked_layers.entry_count; ++i) {
        pvr_chunk_layer_entry_t layer;
        pvr_chunk_texture_image_t image;
        if(pvr_chunk_layer_section_entry_get(&checked_layers, i, &layer) < 0 ||
           pvr_chunk_texture_section_find(&checked_images,
               layer.layer.texture.identifier, &image) < 0)
            return -1;
    }
    return 0;
}
