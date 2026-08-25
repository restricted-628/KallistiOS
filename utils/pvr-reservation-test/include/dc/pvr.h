/* KallistiOS ##version##

   Host-test PVR declarations for contiguous reservations.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_RESERVATION_DC_PVR_H
#define TEST_RESERVATION_DC_PVR_H

#include "../../../pvr-chunk-binding-test/include/dc/pvr.h"
#include <dc/pvr/pvr_mem.h>

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
int pvr_txr_surface_plan_reservation(const pvr_txr_surface_t *surfaces,
                                     size_t surface_count, size_t alignment,
                                     size_t *offsets, size_t *total_bytes);
int pvr_txr_surface_bind_reservation(
    pvr_txr_surface_t *surface,
    const pvr_mem_reservation_t *reservation, size_t offset);
void pvr_txr_surface_release(pvr_txr_surface_t *surface);

#endif /* TEST_RESERVATION_DC_PVR_H */
