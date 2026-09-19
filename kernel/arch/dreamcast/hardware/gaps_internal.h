/* KallistiOS ##version##

   Private GAPS SRAM ownership helpers.
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H
#define __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H

#include <dc/gaps.h>

typedef enum gaps_sram_dma_owner {
    GAPS_SRAM_DMA_OWNER_G1 = 1,
    GAPS_SRAM_DMA_OWNER_G2
} gaps_sram_dma_owner_t;

/* A claim prevents release and rejects a second DMA engine on the same lease.
   The address form is used by G2 DMA after normalizing its bus endpoint. */
int gaps_sram_dma_claim(gaps_sram_lease_t lease, size_t offset, size_t size,
                        gaps_sram_dma_owner_t owner,
                        uint32_t *physical_address);
int gaps_sram_dma_claim_address(uint32_t physical_address, size_t size,
                                gaps_sram_dma_owner_t owner,
                                gaps_sram_lease_t *lease);
void gaps_sram_dma_release(gaps_sram_lease_t lease,
                           gaps_sram_dma_owner_t owner);

#endif /* __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H */
