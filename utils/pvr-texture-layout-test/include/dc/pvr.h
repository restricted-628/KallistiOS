#ifndef __DC_PVR_H
#define __DC_PVR_H

typedef void *pvr_ptr_t;

#define PVR_RAM_SIZE     UINT32_C(0x00800000)
#define PVR_RAM_INT_BASE UINT32_C(0xa4000000)
#define PVR_RAM_INT_TOP  (PVR_RAM_INT_BASE + PVR_RAM_SIZE)

#include <dc/pvr/pvr_txr.h>

#endif
