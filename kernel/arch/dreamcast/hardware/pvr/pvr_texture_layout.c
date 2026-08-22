/* KallistiOS ##version##

   pvr_texture_layout.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <string.h>

#include <dc/pvr.h>

#define PVR_TXR_VQ_CODEBOOK_BYTES 2048u

/* Byte offsets for 16-bit, uncompressed mip levels. The smallest level is
   stored first and the leading six bytes are reserved by the hardware layout. */
static const size_t mip_offset_16bpp[] = {
    0x00006, 0x00008, 0x00010, 0x00030, 0x000b0, 0x002b0,
    0x00ab0, 0x02ab0, 0x0aab0, 0x2aab0, 0xaaab0
};

static bool power_of_two_texture_dimension(uint32_t value) {
    return value >= 8 && value <= 1024 && !(value & (value - 1));
}

static bool format_valid(pvr_txr_surface_format_t format) {
    return format >= PVR_TXR_SURFACE_ARGB1555
        && format <= PVR_TXR_SURFACE_PAL8BPP;
}

static bool layout_valid(pvr_txr_surface_layout_t layout) {
    return layout >= PVR_TXR_SURFACE_TWIDDLED
        && layout <= PVR_TXR_SURFACE_VQ;
}

static uint32_t format_bits_per_pixel(pvr_txr_surface_format_t format) {
    if(format == PVR_TXR_SURFACE_PAL4BPP)
        return 4;

    if(format == PVR_TXR_SURFACE_PAL8BPP)
        return 8;

    return 16;
}

static uint32_t integer_log2(uint32_t value) {
    return 31u - (uint32_t)__builtin_clz(value);
}

static size_t packed_level_size(uint32_t width, uint32_t height,
                                pvr_txr_surface_format_t format,
                                pvr_txr_surface_layout_t layout) {
    size_t bits = (size_t)width * height * format_bits_per_pixel(format);

    if(layout == PVR_TXR_SURFACE_VQ)
        return (bits + 63u) / 64u;

    return (bits + 7u) / 8u;
}

static size_t mip_level_offset(uint32_t exponent,
                               pvr_txr_surface_format_t format,
                               pvr_txr_surface_layout_t layout) {
    size_t offset = mip_offset_16bpp[exponent];

    if(layout == PVR_TXR_SURFACE_VQ)
        return offset / 8u;

    if(format == PVR_TXR_SURFACE_PAL4BPP)
        return offset / 4u;

    if(format == PVR_TXR_SURFACE_PAL8BPP)
        return offset / 2u;

    return offset;
}

int pvr_txr_surface_init(pvr_txr_surface_t *surface, uint32_t width,
                         uint32_t height, pvr_txr_surface_format_t format,
                         pvr_txr_surface_layout_t layout, bool mipmapped) {
    uint32_t exponent;
    size_t top_size;

    if(!surface) {
        errno = EINVAL;
        return -1;
    }

    memset(surface, 0, sizeof(*surface));

    if(!format_valid(format) || !layout_valid(layout)) {
        errno = EINVAL;
        return -1;
    }

    if(layout == PVR_TXR_SURFACE_STRIDE) {
        if(width < 32 || width > 992 || (width & 31u)
           || !power_of_two_texture_dimension(height)) {
            errno = EINVAL;
            return -1;
        }
    }
    else if(!power_of_two_texture_dimension(width)
            || !power_of_two_texture_dimension(height)) {
        errno = EINVAL;
        return -1;
    }

    if((format == PVR_TXR_SURFACE_PAL4BPP
        || format == PVR_TXR_SURFACE_PAL8BPP)
       && layout != PVR_TXR_SURFACE_TWIDDLED) {
        errno = ENOTSUP;
        return -1;
    }

    if(layout == PVR_TXR_SURFACE_VQ
       && format > PVR_TXR_SURFACE_BUMP) {
        errno = ENOTSUP;
        return -1;
    }

    if(mipmapped && width != height) {
        errno = EINVAL;
        return -1;
    }

    if(mipmapped && layout != PVR_TXR_SURFACE_TWIDDLED
       && layout != PVR_TXR_SURFACE_VQ) {
        errno = ENOTSUP;
        return -1;
    }

    top_size = packed_level_size(width, height, format, layout);
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->layout = layout;
    surface->mipmapped = mipmapped;
    surface->mip_levels = mipmapped ? integer_log2(width) + 1u : 1u;
    surface->codebook_size = layout == PVR_TXR_SURFACE_VQ
                           ? PVR_TXR_VQ_CODEBOOK_BYTES : 0;

    if(mipmapped) {
        exponent = integer_log2(width);
        surface->data_size = mip_level_offset(exponent, format, layout)
                           + top_size;
    }
    else {
        surface->data_size = top_size;
    }

    surface->byte_size = surface->codebook_size + surface->data_size;
    return 0;
}

static bool surface_metadata_valid(const pvr_txr_surface_t *surface) {
    pvr_txr_surface_t expected;

    if(!surface
       || pvr_txr_surface_init(&expected, surface->width, surface->height,
                               surface->format, surface->layout,
                               surface->mipmapped) < 0)
        return false;

    if(surface->byte_size != expected.byte_size
       || surface->codebook_size != expected.codebook_size
       || surface->data_size != expected.data_size
       || surface->mip_levels != expected.mip_levels) {
        errno = EINVAL;
        return false;
    }

    return true;
}

int pvr_txr_surface_get_level(const pvr_txr_surface_t *surface,
                              uint32_t level, pvr_txr_level_info_t *info) {
    uint32_t exponent;

    if(!info || !surface_metadata_valid(surface)) {
        if(!info)
            errno = EINVAL;
        return -1;
    }

    if(level >= surface->mip_levels) {
        errno = ERANGE;
        return -1;
    }

    info->width = surface->width >> level;
    info->height = surface->height >> level;
    info->byte_size = packed_level_size(info->width, info->height,
                                         surface->format, surface->layout);

    if(surface->mipmapped) {
        exponent = integer_log2(surface->width) - level;
        info->offset = surface->codebook_size
                     + mip_level_offset(exponent, surface->format,
                                        surface->layout);
    }
    else {
        info->offset = surface->codebook_size;
    }

    return 0;
}
