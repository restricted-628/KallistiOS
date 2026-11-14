#ifndef SH4DER_GLOW_TEST_PVR_H
#define SH4DER_GLOW_TEST_PVR_H
#include "../../../sh4der-frame-test/include/dc/pvr.h"
#include "../../../sh4der-screen-test/include/dc/pvr.h"
typedef enum pvr_txr_transfer {
    PVR_TXR_TRANSFER_CPU, PVR_TXR_TRANSFER_SQ, PVR_TXR_TRANSFER_DMA
} pvr_txr_transfer_t;
int pvr_txr_surface_upload(const pvr_txr_surface_t *, const void *, size_t, pvr_txr_transfer_t);
void pvr_poly_cxt_txr(pvr_poly_cxt_t *, pvr_list_t, int, int, int, pvr_ptr_t, pvr_filter_mode_t);
#endif
