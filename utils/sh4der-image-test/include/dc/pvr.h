#ifndef SH4DER_IMAGE_TEST_PVR_H
#define SH4DER_IMAGE_TEST_PVR_H
#include "../../../sh4der-glow-test/include/dc/pvr.h"
#define PVR_TXRFMT_X32_STRIDE (1u << 25)
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *);
#endif
