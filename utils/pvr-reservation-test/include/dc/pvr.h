/* KallistiOS ##version##

   Host-test PVR declarations for contiguous reservations.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_RESERVATION_DC_PVR_H
#define TEST_RESERVATION_DC_PVR_H

#include "../../../pvr-material-test/include/dc/pvr.h"
#include <dc/pvr/pvr_mem.h>

#define PVR_RAM_SIZE     UINT32_C(0x00800000)
#define PVR_RAM_INT_BASE UINT32_C(0xa4000000)
#define PVR_RAM_INT_TOP  (PVR_RAM_INT_BASE + PVR_RAM_SIZE)

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

typedef struct pvr_txr_level_info {
    uint32_t width;
    uint32_t height;
    size_t offset;
    size_t byte_size;
} pvr_txr_level_info_t;

typedef enum pvr_txr_yuv_format {
    PVR_TXR_YUV420 = 0,
    PVR_TXR_YUV422
} pvr_txr_yuv_format_t;

int pvr_txr_surface_init(pvr_txr_surface_t *surface, uint32_t width,
                         uint32_t height,
                         pvr_txr_surface_format_t format,
                         pvr_txr_surface_layout_t layout, bool mipmapped);
int pvr_txr_surface_init_vq(pvr_txr_surface_t *surface, uint32_t width,
                            uint32_t height,
                            pvr_txr_surface_format_t format,
                            uint16_t codebook_entries, bool mipmapped);
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *surface,
                                        pvr_ptr_t *address);
int pvr_txr_surface_get_level(const pvr_txr_surface_t *surface,
                              uint32_t level, pvr_txr_level_info_t *info);
int pvr_txr_surface_plan_reservation(const pvr_txr_surface_t *surfaces,
                                     size_t surface_count, size_t alignment,
                                     size_t *offsets, size_t *total_bytes);
int pvr_txr_surface_bind_reservation(
    pvr_txr_surface_t *surface,
    const pvr_mem_reservation_t *reservation, size_t offset);
void pvr_txr_surface_release(pvr_txr_surface_t *surface);

#endif /* TEST_RESERVATION_DC_PVR_H */
