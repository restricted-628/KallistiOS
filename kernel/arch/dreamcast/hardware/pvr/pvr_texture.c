/* KallistiOS ##version##

   pvr_texture.c
   Copyright (C) 2002, 2004 Megan Potter
   Copyright (C) 2024 Andress Barajas
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <errno.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/video.h>
#include <kos/dbglog.h>
#include <kos/regfield.h>
#include <string.h>
#include "pvr_internal.h"

/*

   Texture handling

   Helper functions for handling texture tasks of various kinds.

*/

void pvr_txr_set_stride(size_t texture_width) {
    uint32_t temp = texture_width / 32;

    /* The width must be a non-zero multiple of 32 that fits in the 5 bits
    of the register field. */
    assert(temp && __is_aligned(texture_width, 32) && !(temp & ~PVR_TXR_STRIDE_MULT));

    /* Pull the reg, mask out current stride, and rewrite with new */
    temp = FIELD_PREP(PVR_TXR_STRIDE_MULT, temp);
    temp |= PVR_GET(PVR_TEXTURE_MODULO) & ~PVR_TXR_STRIDE_MULT;
    PVR_SET(PVR_TEXTURE_MODULO, temp);

    return;
}

size_t pvr_txr_get_stride(void) {
    uint32_t reg = PVR_GET(PVR_TEXTURE_MODULO);
    return FIELD_GET(reg, PVR_TXR_STRIDE_MULT) * 32;
}

/* A nonblocking legacy image load must retain the same channel-2 ownership as
   a blocking surface upload. Releasing at submission would let scene-list DMA
   acquire the semaphore while the texture transfer is still in progress. */
static void legacy_texture_dma_unlock(void *data) {
    (void)data;
    sem_signal((semaphore_t *)&pvr_state.dma_lock);
}

static bool surface_storage_valid(const pvr_txr_surface_t *surface) {
    pvr_txr_level_info_t level;
    uintptr_t address;

    if(pvr_txr_surface_get_level(surface, 0, &level) < 0)
        return false;

    if(!surface->vram) {
        errno = ENODEV;
        return false;
    }

    address = (uintptr_t)surface->vram;
    if(address < PVR_RAM_INT_BASE || address >= PVR_RAM_INT_TOP) {
        errno = EFAULT;
        return false;
    }

    /* Descriptors are caller-owned, so revalidate the complete binding rather
       than assuming it was left unchanged after allocation or bind. */
    if(surface->capacity < surface->byte_size ||
            surface->capacity > PVR_RAM_INT_TOP - address) {
        errno = ENOSPC;
        return false;
    }

    return true;
}

static int surface_allocate(pvr_txr_surface_t *surface) {
    pvr_ptr_t allocation;
    pvr_ptr_t texture_address;

    allocation = pvr_mem_malloc(surface->byte_size);
    if(!allocation) {
        memset(surface, 0, sizeof(*surface));
        errno = ENOMEM;
        return -1;
    }

    surface->vram = allocation;
    surface->capacity = surface->byte_size;
    surface->owns_vram = true;
    if(pvr_txr_surface_get_texture_address(surface, &texture_address) < 0) {
        int saved_errno = errno;

        pvr_mem_free(allocation);
        memset(surface, 0, sizeof(*surface));
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int pvr_txr_surface_alloc(pvr_txr_surface_t *surface, uint32_t width,
                          uint32_t height, pvr_txr_surface_format_t format,
                          pvr_txr_surface_layout_t layout, bool mipmapped) {
    if(pvr_txr_surface_init(surface, width, height, format, layout,
                            mipmapped) < 0)
        return -1;

    return surface_allocate(surface);
}

int pvr_txr_surface_alloc_vq(pvr_txr_surface_t *surface, uint32_t width,
                             uint32_t height,
                             pvr_txr_surface_format_t format,
                             uint16_t codebook_entries, bool mipmapped) {
    if(pvr_txr_surface_init_vq(surface, width, height, format,
                               codebook_entries, mipmapped) < 0)
        return -1;

    return surface_allocate(surface);
}

static int surface_bind_storage(pvr_txr_surface_t *surface, pvr_ptr_t vram,
                                size_t capacity) {
    uintptr_t address = (uintptr_t)vram;
    uintptr_t vram_top = PVR_RAM_INT_TOP;
    pvr_ptr_t texture_address;

    if(!vram || (address & 7u)) {
        memset(surface, 0, sizeof(*surface));
        errno = EINVAL;
        return -1;
    }

    /* Texture pointers use the 64-bit CPU-visible VRAM alias. Rejecting main
       memory here prevents a later DMA upload from silently remapping it as a
       VRAM destination through its low address bits. */
    if(address < PVR_RAM_INT_BASE || address >= vram_top) {
        memset(surface, 0, sizeof(*surface));
        errno = EFAULT;
        return -1;
    }

    if(capacity > vram_top - address || capacity < surface->byte_size) {
        memset(surface, 0, sizeof(*surface));
        errno = ENOSPC;
        return -1;
    }

    surface->vram = vram;
    surface->capacity = capacity;
    if(pvr_txr_surface_get_texture_address(surface, &texture_address) < 0) {
        int saved_errno = errno;

        memset(surface, 0, sizeof(*surface));
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int pvr_txr_surface_bind(pvr_txr_surface_t *surface, pvr_ptr_t vram,
                         size_t capacity, uint32_t width, uint32_t height,
                         pvr_txr_surface_format_t format,
                         pvr_txr_surface_layout_t layout, bool mipmapped) {
    if(pvr_txr_surface_init(surface, width, height, format, layout,
                            mipmapped) < 0)
        return -1;

    return surface_bind_storage(surface, vram, capacity);
}

int pvr_txr_surface_bind_vq(pvr_txr_surface_t *surface, pvr_ptr_t vram,
                            size_t capacity, uint32_t width, uint32_t height,
                            pvr_txr_surface_format_t format,
                            uint16_t codebook_entries, bool mipmapped) {
    if(pvr_txr_surface_init_vq(surface, width, height, format,
                               codebook_entries, mipmapped) < 0)
        return -1;

    return surface_bind_storage(surface, vram, capacity);
}

void pvr_txr_surface_release(pvr_txr_surface_t *surface) {
    if(!surface)
        return;

    if(surface->owns_vram && surface->vram)
        pvr_mem_free(surface->vram);

    memset(surface, 0, sizeof(*surface));
}

int pvr_txr_surface_begin_render(const pvr_txr_surface_t *surface,
                                 uint32_t render_width,
                                 uint32_t render_height) {
    pvr_txr_surface_format_t expected_format;

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(pvr_state.scene_active) {
        errno = EBUSY;
        return -1;
    }

    if(!surface_storage_valid(surface))
        return -1;

    if(!render_width || !render_height || render_width > surface->width ||
            render_height > surface->height) {
        errno = EINVAL;
        return -1;
    }

    if(surface->mipmapped ||
            (surface->layout != PVR_TXR_SURFACE_LINEAR &&
             surface->layout != PVR_TXR_SURFACE_STRIDE)) {
        errno = ENOTSUP;
        return -1;
    }

    if(vid_mode->pm == PM_RGB555)
        expected_format = PVR_TXR_SURFACE_ARGB1555;
    else if(vid_mode->pm == PM_RGB565)
        expected_format = PVR_TXR_SURFACE_RGB565;
    else {
        errno = ENOTSUP;
        return -1;
    }

    if(surface->format != expected_format) {
        errno = ENOTSUP;
        return -1;
    }

    return pvr_scene_begin_rtt(surface->vram, render_width, render_height,
                               surface->width);
}

uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *surface) {
    pvr_txr_level_info_t level;
    uint32_t format;

    if(pvr_txr_surface_get_level(surface, 0, &level) < 0)
        return UINT32_MAX;

    switch(surface->format) {
        case PVR_TXR_SURFACE_ARGB1555:
            format = PVR_TXRFMT_ARGB1555;
            break;
        case PVR_TXR_SURFACE_RGB565:
            format = PVR_TXRFMT_RGB565;
            break;
        case PVR_TXR_SURFACE_ARGB4444:
            format = PVR_TXRFMT_ARGB4444;
            break;
        case PVR_TXR_SURFACE_YUV422:
            format = PVR_TXRFMT_YUV422;
            break;
        case PVR_TXR_SURFACE_BUMP:
            format = PVR_TXRFMT_BUMP;
            break;
        case PVR_TXR_SURFACE_PAL4BPP:
            format = PVR_TXRFMT_PAL4BPP;
            break;
        case PVR_TXR_SURFACE_PAL8BPP:
            format = PVR_TXRFMT_PAL8BPP;
            break;
        default:
            errno = EINVAL;
            return UINT32_MAX;
    }

    if(surface->layout == PVR_TXR_SURFACE_LINEAR)
        format |= PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_POW2_STRIDE;
    else if(surface->layout == PVR_TXR_SURFACE_STRIDE)
        format |= PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE;
    else if(surface->layout == PVR_TXR_SURFACE_VQ)
        format |= PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED;
    else
        format |= PVR_TXRFMT_VQ_DISABLE | PVR_TXRFMT_TWIDDLED;

    return format;
}

int pvr_txr_surface_upload_part(const pvr_txr_surface_t *surface,
                                size_t offset, const void *src,
                                size_t byte_size,
                                pvr_txr_transfer_t transfer) {
    uint8_t *destination;
    int result;

    if(!src || !byte_size) {
        errno = EINVAL;
        return -1;
    }

    if(!surface_storage_valid(surface))
        return -1;

    if(offset > surface->byte_size
       || byte_size > surface->byte_size - offset) {
        errno = EINVAL;
        return -1;
    }

    destination = (uint8_t *)surface->vram + offset;

    if(transfer == PVR_TXR_TRANSFER_CPU) {
        memcpy(destination, src, byte_size);
        return 0;
    }

    if(transfer != PVR_TXR_TRANSFER_SQ
       && transfer != PVR_TXR_TRANSFER_DMA) {
        errno = EINVAL;
        return -1;
    }

    if(((uintptr_t)src & 31u) || ((uintptr_t)destination & 31u)
       || (offset & 31u) || (byte_size & 31u)) {
        errno = EINVAL;
        return -1;
    }

    if(transfer == PVR_TXR_TRANSFER_DMA && !pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(transfer == PVR_TXR_TRANSFER_SQ) {
        pvr_sq_load(destination, src, byte_size, PVR_DMA_VRAM64);
        return 0;
    }

    /* Scene-list DMA and texture DMA share one hardware channel. Keep the
       established lock for the entire blocking transfer. */
    sem_wait((semaphore_t *)&pvr_state.dma_lock);
    result = pvr_txr_load_dma(src, destination, byte_size, true, NULL, NULL);
    sem_signal((semaphore_t *)&pvr_state.dma_lock);
    return result;
}

int pvr_txr_surface_upload(const pvr_txr_surface_t *surface, const void *src,
                           size_t byte_size, pvr_txr_transfer_t transfer) {
    if(!surface || byte_size != surface->byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part(surface, 0, src, byte_size, transfer);
}

int pvr_txr_surface_upload_level(const pvr_txr_surface_t *surface,
                                 uint32_t level, const void *src,
                                 size_t byte_size,
                                 pvr_txr_transfer_t transfer) {
    pvr_txr_level_info_t info;

    if(pvr_txr_surface_get_level(surface, level, &info) < 0)
        return -1;

    if(byte_size != info.byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part(surface, info.offset, src, byte_size,
                                       transfer);
}

int pvr_txr_surface_upload_codebook(const pvr_txr_surface_t *surface,
                                    const void *src, size_t byte_size,
                                    pvr_txr_transfer_t transfer) {
    if(!surface || surface->layout != PVR_TXR_SURFACE_VQ
       || byte_size != surface->codebook_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_upload_part(surface, 0, src, byte_size, transfer);
}

int pvr_txr_surface_readback_part(const pvr_txr_surface_t *surface,
                                  size_t offset, void *dst,
                                  size_t byte_size) {
    const uint8_t *source;

    if(!dst || !byte_size) {
        errno = EINVAL;
        return -1;
    }

    if(!surface_storage_valid(surface))
        return -1;

    if(offset > surface->byte_size ||
            byte_size > surface->byte_size - offset) {
        errno = EINVAL;
        return -1;
    }

    source = (const uint8_t *)surface->vram + offset;
    memmove(dst, source, byte_size);
    return 0;
}

int pvr_txr_surface_readback(const pvr_txr_surface_t *surface, void *dst,
                             size_t byte_size) {
    if(!surface || byte_size != surface->byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_readback_part(surface, 0, dst, byte_size);
}

int pvr_txr_surface_readback_level(const pvr_txr_surface_t *surface,
                                   uint32_t level, void *dst,
                                   size_t byte_size) {
    pvr_txr_level_info_t info;

    if(pvr_txr_surface_get_level(surface, level, &info) < 0)
        return -1;

    if(byte_size != info.byte_size) {
        errno = EINVAL;
        return -1;
    }

    return pvr_txr_surface_readback_part(surface, info.offset, dst,
                                          byte_size);
}

/* Load raw texture data from an SH-4 buffer into PVR RAM */
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count) {
    count = __align_up(count, 4);
    pvr_sq_load((uint32_t *)dst, (const uint32_t *)src, count, PVR_DMA_VRAM64);
}

/* Linear/iterative twiddling algorithm from Marcus' tatest */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

#define MIN(a, b) ( (a)<(b)? (a):(b) )

static size_t twiddled_word_16(uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height) {
    uint32_t minimum = MIN(width, height);
    uint32_t mask = minimum - 1u;

    return TWIDOUT(x & mask, y & mask)
         + (x / minimum + y / minimum) * minimum * minimum;
}

static size_t twiddled_word_8(uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    uint32_t minimum = MIN(width, height);
    uint32_t mask = minimum - 1u;

    return TWIDOUT((y & mask) / 2u, x & mask)
         + (x / minimum + y / minimum) * minimum * minimum / 2u;
}

static size_t twiddled_word_4(uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    uint32_t minimum = MIN(width, height);
    uint32_t mask = minimum - 1u;

    return TWIDOUT((x & mask) / 2u, (y & mask) / 2u)
         + (x / minimum + y / minimum) * minimum * minimum / 4u;
}

int pvr_txr_surface_upload_rect(const pvr_txr_surface_t *surface,
                                uint32_t level, uint32_t dst_x,
                                uint32_t dst_y, uint32_t width,
                                uint32_t height, const void *src,
                                size_t src_stride) {
    pvr_txr_level_info_t info;
    uint32_t bits_per_pixel;
    size_t source_row_size;
    uint8_t *destination;
    uint32_t x;
    uint32_t y;

    if(!src || !width || !height || !surface_storage_valid(surface)) {
        if(!src || !width || !height)
            errno = EINVAL;
        return -1;
    }

    if(surface->layout == PVR_TXR_SURFACE_VQ) {
        errno = ENOTSUP;
        return -1;
    }

    if(pvr_txr_surface_get_level(surface, level, &info) < 0)
        return -1;

    if(dst_x > info.width || width > info.width - dst_x
       || dst_y > info.height || height > info.height - dst_y) {
        errno = ERANGE;
        return -1;
    }

    bits_per_pixel = surface->format == PVR_TXR_SURFACE_PAL4BPP ? 4u
                   : surface->format == PVR_TXR_SURFACE_PAL8BPP ? 8u
                   : 16u;
    source_row_size = ((size_t)width * bits_per_pixel + 7u) / 8u;

    if(!src_stride)
        src_stride = source_row_size;

    if(src_stride < source_row_size
       || (height > 1
           && src_stride > (SIZE_MAX - source_row_size) / (height - 1u))) {
        errno = EINVAL;
        return -1;
    }

    destination = (uint8_t *)surface->vram + info.offset;

    if(surface->layout == PVR_TXR_SURFACE_LINEAR
       || surface->layout == PVR_TXR_SURFACE_STRIDE) {
        size_t destination_stride = (size_t)info.width * 2u;

        for(y = 0; y < height; ++y) {
            memcpy(destination + (size_t)(dst_y + y) * destination_stride
                   + (size_t)dst_x * 2u,
                   (const uint8_t *)src + (size_t)y * src_stride,
                   source_row_size);
        }

        return 0;
    }

    if(surface->format == PVR_TXR_SURFACE_YUV422) {
        errno = ENOTSUP;
        return -1;
    }

    if(bits_per_pixel == 16u) {
        uint16_t *output = (uint16_t *)destination;

        for(y = 0; y < height; ++y) {
            const uint8_t *input = (const uint8_t *)src
                                 + (size_t)y * src_stride;

            for(x = 0; x < width; ++x) {
                uint16_t pixel;

                memcpy(&pixel, input + (size_t)x * 2u, sizeof(pixel));
                output[twiddled_word_16(dst_x + x, dst_y + y,
                                        info.width, info.height)] = pixel;
            }
        }
    }
    else if(bits_per_pixel == 8u) {
        /* Packed mip levels may begin at an odd byte offset. Byte writes avoid
           turning a valid layout into an unaligned 16-bit access. */
        for(y = 0; y < height; ++y) {
            const uint8_t *input = (const uint8_t *)src
                                 + (size_t)y * src_stride;

            for(x = 0; x < width; ++x) {
                uint32_t output_y = dst_y + y;
                size_t word = twiddled_word_8(dst_x + x, output_y,
                                              info.width, info.height);

                destination[word * 2u + (output_y & 1u)] = input[x];
            }
        }
    }
    else {
        /* The same odd-offset rule applies to the smallest 4-bit mip levels;
           preserve the neighboring nibble with a byte-sized read/modify/write. */
        for(y = 0; y < height; ++y) {
            const uint8_t *input = (const uint8_t *)src
                                 + (size_t)y * src_stride;

            for(x = 0; x < width; ++x) {
                uint32_t output_x = dst_x + x;
                uint32_t output_y = dst_y + y;
                size_t word = twiddled_word_4(output_x, output_y,
                                              info.width, info.height);
                uint32_t shift = (output_x & 1u) * 8u
                               + (output_y & 1u) * 4u;
                uint32_t byte_offset = word * 2u + shift / 8u;
                uint32_t nibble_shift = shift & 7u;
                uint8_t pixel = (input[x / 2u] >> ((x & 1u) * 4u)) & 0x0fu;
                uint8_t value = destination[byte_offset];

                value = (uint8_t)((value & ~(0x0fu << nibble_shift))
                                  | (pixel << nibble_shift));
                destination[byte_offset] = value;
            }
        }
    }

    return 0;
}

static int load_ex_surface_format(uint32_t flags,
                                  pvr_txr_surface_format_t *format,
                                  uint32_t *bits_per_pixel) {
    switch(flags & PVR_TXRLOAD_FMT_MASK) {
        case PVR_TXRLOAD_4BPP:
            *format = PVR_TXR_SURFACE_PAL4BPP;
            *bits_per_pixel = 4;
            return 0;
        case PVR_TXRLOAD_8BPP:
            *format = PVR_TXR_SURFACE_PAL8BPP;
            *bits_per_pixel = 8;
            return 0;
        case PVR_TXRLOAD_16BPP:
            /* The three 16-bit color encodings have identical storage. */
            *format = PVR_TXR_SURFACE_RGB565;
            *bits_per_pixel = 16;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int pvr_txr_load_ex_checked(const void *src, pvr_ptr_t dst, uint32_t w,
                            uint32_t h, uint32_t flags) {
    static const uint32_t valid_flags = PVR_TXRLOAD_FMT_MASK
                                      | PVR_TXRLOAD_VQ_LOAD
                                      | PVR_TXRLOAD_INVERT_Y
                                      | PVR_TXRLOAD_FMT_VQ
                                      | PVR_TXRLOAD_FMT_TWIDDLED
                                      | PVR_TXRLOAD_DMA
                                      | PVR_TXRLOAD_NONBLOCK
                                      | PVR_TXRLOAD_SQ;
    pvr_txr_surface_t surface;
    pvr_txr_surface_format_t format;
    pvr_txr_surface_layout_t layout;
    pvr_txr_transfer_t transfer = PVR_TXR_TRANSFER_CPU;
    uint32_t bits_per_pixel;
    size_t row_size;
    size_t required_size;
    uint32_t y;

    if(!src || !dst || (flags & ~valid_flags)) {
        errno = EINVAL;
        return -1;
    }

    if(load_ex_surface_format(flags, &format, &bits_per_pixel) < 0)
        return -1;

    if(flags & PVR_TXRLOAD_VQ_LOAD) {
        errno = ENOTSUP;
        return -1;
    }

    if((flags & PVR_TXRLOAD_DMA) && (flags & PVR_TXRLOAD_SQ)) {
        errno = EINVAL;
        return -1;
    }

    if(flags & PVR_TXRLOAD_NONBLOCK) {
        errno = ENOTSUP;
        return -1;
    }

    layout = flags & PVR_TXRLOAD_FMT_VQ ? PVR_TXR_SURFACE_VQ
           : PVR_TXR_SURFACE_TWIDDLED;

    if(pvr_txr_surface_init(&surface, w, h, format, layout, false) < 0)
        return -1;

    required_size = surface.byte_size;
    if(pvr_txr_surface_bind(&surface, dst, required_size, w, h, format,
                            layout, false) < 0)
        return -1;

    if((flags & PVR_TXRLOAD_FMT_TWIDDLED)
       || (flags & PVR_TXRLOAD_FMT_VQ)) {
        if(flags & PVR_TXRLOAD_INVERT_Y) {
            errno = ENOTSUP;
            return -1;
        }

        if(flags & PVR_TXRLOAD_DMA)
            transfer = PVR_TXR_TRANSFER_DMA;
        else if(flags & PVR_TXRLOAD_SQ)
            transfer = PVR_TXR_TRANSFER_SQ;

        return pvr_txr_surface_upload(&surface, src, surface.byte_size,
                                      transfer);
    }

    if(flags & (PVR_TXRLOAD_DMA | PVR_TXRLOAD_SQ)) {
        errno = ENOTSUP;
        return -1;
    }

    row_size = ((size_t)w * bits_per_pixel + 7u) / 8u;
    if(!(flags & PVR_TXRLOAD_INVERT_Y)) {
        return pvr_txr_surface_upload_rect(&surface, 0, 0, 0, w, h, src,
                                           row_size);
    }

    /* Inversion changes the destination row before its Morton address is
       calculated; no temporary full-texture image is required. */
    for(y = 0; y < h; ++y) {
        const uint8_t *row = (const uint8_t *)src + (size_t)y * row_size;

        if(pvr_txr_surface_upload_rect(&surface, 0, 0, h - 1u - y, w, 1,
                                       row, row_size) < 0)
            return -1;
    }

    return 0;
}

void pvr_txr_load_ex(const void *src, pvr_ptr_t dst, uint32_t w, uint32_t h,
                     uint32_t flags) {
    int result = pvr_txr_load_ex_checked(src, dst, w, h, flags);

    assert_msg(result == 0,
               "pvr_txr_load_ex: invalid or unsupported texture request");
}

/* Load a KOS Platform Independent Image (subject to restraint checking) */
void pvr_txr_load_kimg(const kos_img_t *img, pvr_ptr_t dst, uint32_t flags) {
    uint32_t fmt, w, h;

    /* First check and make sure it's a format we can use */
    fmt = KOS_IMG_FMT_I(img->fmt) & KOS_IMG_FMT_MASK;
    assert_msg(fmt == KOS_IMG_FMT_RGB565 || fmt == KOS_IMG_FMT_ARGB4444
               || fmt == KOS_IMG_FMT_ARGB1555
               || fmt == KOS_IMG_FMT_PAL4BPP
               || fmt == KOS_IMG_FMT_PAL8BPP, "Unsupported format in input kos_img_t");

    /* Second, check to make sure it's a proper power of 2 we can use */
    w = img->w;
    h = img->h;
    assert_msg(w == 8 || w == 16 || w == 32 || w == 64 || w == 128
               || w == 256 || w == 512 || w == 1024, "Non power-of-2 image width in input kos_img_t");
    assert_msg(h == 8 || h == 16 || h == 32 || h == 64 || h == 128
               || h == 256 || h == 512 || h == 1024, "Non power-of-2 image height in input kos_img_t");

    /* Convert it to a PVR image type */
    switch(fmt) {
        case KOS_IMG_FMT_RGB565:
        case KOS_IMG_FMT_ARGB4444:
        case KOS_IMG_FMT_ARGB1555:
            fmt = PVR_TXRLOAD_16BPP;
            break;
        case KOS_IMG_FMT_PAL4BPP:
            fmt = PVR_TXRLOAD_4BPP;
            break;
        case KOS_IMG_FMT_PAL8BPP:
            fmt = PVR_TXRLOAD_8BPP;
            break;
    }

    /* Make sure the format part of the flags is clean */
    flags = (flags & ~PVR_TXRLOAD_FMT_MASK) | fmt;

    /* Call down */
    if((flags & PVR_TXRLOAD_FMT_VQ) || (flags & PVR_TXRLOAD_FMT_TWIDDLED) ||
            (KOS_IMG_FMT_D(img->fmt) & PVR_TXRLOAD_FMT_VQ) ||
            (KOS_IMG_FMT_D(img->fmt) & PVR_TXRLOAD_FMT_TWIDDLED)) {
        if(flags & PVR_TXRLOAD_INVERT_Y)
            assert_msg(0, "Inverted, non-twiddled loading not supported yet");
        else {
            /* We only enable DMA here for now since it sort of changes things
               to have to allocate an intermediary buffer. */
            if(flags & PVR_TXRLOAD_DMA) {
                int result;

                sem_wait((semaphore_t *)&pvr_state.dma_lock);
                if(flags & PVR_TXRLOAD_NONBLOCK) {
                    result = pvr_txr_load_dma(img->data, dst,
                                              img->byte_count, false,
                                              legacy_texture_dma_unlock,
                                              NULL);
                    if(result < 0)
                        sem_signal((semaphore_t *)&pvr_state.dma_lock);
                }
                else {
                    pvr_txr_load_dma(img->data, dst, img->byte_count,
                                     true, NULL, NULL);
                    sem_signal((semaphore_t *)&pvr_state.dma_lock);
                }
            }
            else if(flags & PVR_TXRLOAD_SQ) {
                pvr_txr_load(img->data, dst, img->byte_count);
            }
            else {
                memcpy(dst, img->data, img->byte_count);
            }
        }
    }
    else {
        pvr_txr_load_ex(img->data, dst, w, h, flags);
    }
}
