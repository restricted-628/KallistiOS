/* KallistiOS ##version##

   dc/pvr/pvr_chunk_texture_binding.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_texture_asset.h>

#include <errno.h>
#include <string.h>

int pvr_chunk_texture_image_surface_init(
        const pvr_chunk_texture_image_t *image,
        pvr_txr_surface_t *surface) {
    pvr_txr_surface_t candidate;
    int rv;

    if(surface)
        memset(surface, 0, sizeof(*surface));
    if(!image || !surface || !image->data || !image->data_size) {
        errno = EINVAL;
        return -1;
    }
    if(image->layout == PVR_TXR_SURFACE_VQ)
        rv = pvr_txr_surface_init_vq(
            &candidate, image->width, image->height, image->format,
            image->codebook_entries, image->mipmapped);
    else {
        if(image->codebook_entries) {
            errno = EINVAL;
            return -1;
        }
        rv = pvr_txr_surface_init(
            &candidate, image->width, image->height, image->format,
            image->layout, image->mipmapped);
    }
    if(rv < 0)
        return -1;
    if(candidate.byte_size != image->data_size) {
        errno = EILSEQ;
        return -1;
    }
    *surface = candidate;
    return 0;
}

int pvr_chunk_texture_image_upload(
        const pvr_chunk_texture_image_t *image,
        const pvr_txr_surface_t *surface, pvr_txr_transfer_t transfer) {
    pvr_txr_surface_t expected;

    if(!surface || pvr_chunk_texture_image_surface_init(
           image, &expected) < 0)
        return -1;
    if(!surface->vram || surface->capacity < surface->byte_size ||
       surface->byte_size != expected.byte_size ||
       surface->codebook_size != expected.codebook_size ||
       surface->data_size != expected.data_size ||
       surface->width != expected.width ||
       surface->height != expected.height ||
       surface->mip_levels != expected.mip_levels ||
       surface->format != expected.format ||
       surface->layout != expected.layout ||
       surface->mipmapped != expected.mipmapped) {
        errno = EINVAL;
        return -1;
    }
    return pvr_txr_surface_upload(surface, image->data,
                                  image->data_size, transfer);
}
