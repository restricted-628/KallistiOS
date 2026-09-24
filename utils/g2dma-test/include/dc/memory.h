#ifndef __DC_MEMORY_H
#define __DC_MEMORY_H

#include <stdint.h>

#define MEM_AREA_CACHE_MASK UINT32_C(0x1fffffff)
#define MEM_AREA_P0_BASE    UINT32_C(0x00000000)
#define MEM_AREA_P1_BASE    UINT32_C(0x80000000)
#define MEM_AREA_P2_BASE    UINT32_C(0xa0000000)
#define MEM_AREA_P3_BASE    UINT32_C(0xc0000000)

#endif
