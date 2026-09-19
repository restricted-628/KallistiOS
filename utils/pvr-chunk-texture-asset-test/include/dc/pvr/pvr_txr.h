/* KallistiOS ##version##

   Minimal host-test texture-surface declarations.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_DC_PVR_TXR_H
#define TEST_DC_PVR_TXR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *pvr_ptr_t;

typedef enum pvr_txr_surface_format {
    PVR_TXR_SURFACE_ARGB1555 = 0,
    PVR_TXR_SURFACE_RGB565,
    PVR_TXR_SURFACE_ARGB4444,
    PVR_TXR_SURFACE_YUV422,
    PVR_TXR_SURFACE_BUMP,
    PVR_TXR_SURFACE_PAL4BPP,
    PVR_TXR_SURFACE_PAL8BPP
} pvr_txr_surface_format_t;

typedef enum pvr_txr_surface_layout {
    PVR_TXR_SURFACE_TWIDDLED = 0,
    PVR_TXR_SURFACE_LINEAR,
    PVR_TXR_SURFACE_STRIDE,
    PVR_TXR_SURFACE_VQ
} pvr_txr_surface_layout_t;

typedef enum pvr_txr_transfer {
    PVR_TXR_TRANSFER_CPU = 0,
    PVR_TXR_TRANSFER_SQ,
    PVR_TXR_TRANSFER_DMA
} pvr_txr_transfer_t;

typedef struct pvr_txr_surface {
    pvr_ptr_t vram;
    size_t capacity;
    size_t byte_size;
    size_t codebook_size;
    size_t data_size;
    uint32_t width;
    uint32_t height;
    uint16_t mip_levels;
    pvr_txr_surface_format_t format;
    pvr_txr_surface_layout_t layout;
    bool mipmapped;
    bool owns_vram;
} pvr_txr_surface_t;

#endif
