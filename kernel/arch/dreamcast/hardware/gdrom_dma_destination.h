/* KallistiOS ##version##

   Private direct-GD destination admission. Callers validate nonzero aligned
   transfer sizes before using these predicates. This header preserves the
   current transport policy; decoded hardware windows are not ownership grants.
   Copyright (C) 2026 Joseph Black
*/
#ifndef __GDROM_DMA_DESTINATION_H
#define __GDROM_DMA_DESTINATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dma_memory.h"

typedef enum gdrom_dma_destination {
    GDROM_DMA_DESTINATION_INVALID = 0,
    GDROM_DMA_DESTINATION_SYSTEM_RAM,
    GDROM_DMA_DESTINATION_VRAM,
    GDROM_DMA_DESTINATION_GAPS_SRAM,
    GDROM_DMA_DESTINATION_AICA
} gdrom_dma_destination_t;

static bool dma_range_contains(uint32_t address, size_t size,
                               uint32_t lower, uint32_t upper) {
    return address >= lower && address < upper && size <= upper - address;
}

static bool dma_destination_alias_valid(uintptr_t address) {
    uintptr_t area = address & ~MEM_AREA_CACHE_MASK;

    if(area == MEM_AREA_P1_BASE || area == MEM_AREA_P2_BASE)
        return true;
    if(area == MEM_AREA_P0_BASE || area == MEM_AREA_P3_BASE)
        return !mmu_enabled();
    return false;
}

static gdrom_dma_destination_t dma_destination_classify(
        uintptr_t virtual_address, size_t size, bool gaps_authorized,
        uint32_t *bus_address) {
    uint32_t physical = (uint32_t)(virtual_address & MEM_AREA_CACHE_MASK);
    uint32_t vram_size = hardware_sys_mode(NULL) == HW_TYPE_RETAIL
        ? 0x00800000u : 0x01000000u;

    if(!size || !dma_destination_alias_valid(virtual_address))
        return GDROM_DMA_DESTINATION_INVALID;

    if(bus_address)
        *bus_address = physical;

    /* Main RAM size is independent of retail/development VRAM capacity.
       Preserve the existing index-mirror policy without truncating the
       canonical upper bank on 32 MiB systems. */
    if(dc_dma_main_ram_contains(physical, size, HW_MEMSIZE, true))
        return GDROM_DMA_DESTINATION_SYSTEM_RAM;

    if(dma_range_contains(physical, size,
                          0x04000000u, 0x04000000u + vram_size)
            || dma_range_contains(physical, size,
                                  0x05000000u,
                                  0x05000000u + vram_size))
        return GDROM_DMA_DESTINATION_VRAM;

    /* Normalize only the documented 8 MiB images. Bit 24 distinguishes
       the 32-bit and 64-bit views and must survive the conversion. */
    if(dma_range_contains(physical, size, 0x06000000u, 0x06800000u)
            || dma_range_contains(physical, size, 0x07000000u, 0x07800000u)) {
        if(bus_address)
            *bus_address = physical - 0x02000000u;
        return GDROM_DMA_DESTINATION_VRAM;
    }

    if(gaps_authorized
            && dma_range_contains(physical, size, GAPS_SRAM_PHYS_BASE,
                                   GAPS_SRAM_PHYS_BASE + GAPS_SRAM_SIZE))
        return GDROM_DMA_DESTINATION_GAPS_SRAM;

    return GDROM_DMA_DESTINATION_INVALID;
}

static bool dma_destination_cacheable(uintptr_t virtual_address,
                                      gdrom_dma_destination_t destination) {
    return destination == GDROM_DMA_DESTINATION_SYSTEM_RAM
        && (virtual_address & ~MEM_AREA_CACHE_MASK) != MEM_AREA_P2_BASE;
}

#endif /* __GDROM_DMA_DESTINATION_H */
