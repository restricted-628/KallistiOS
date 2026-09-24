/* KallistiOS ##version##

   Private GAPS SRAM ownership helpers.
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H
#define __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H

#include <dc/gaps.h>
#include <stdbool.h>

typedef enum gaps_sram_dma_owner {
    GAPS_SRAM_DMA_OWNER_G1 = 1,
    GAPS_SRAM_DMA_OWNER_G2
} gaps_sram_dma_owner_t;

/* A claim prevents release and excludes other G1/G2 DMA over the entire SRAM
   window, even across different leases. Either role may authorize G1 or G2
   through one of its leases; the owner must separately coordinate NIC access.
   The address form is used by G2 DMA after normalizing its bus endpoint. */
int gaps_sram_dma_claim(gaps_sram_lease_t lease, size_t offset, size_t size,
                        gaps_sram_dma_owner_t owner,
                        uint32_t *physical_address);
int gaps_sram_dma_claim_address(uint32_t physical_address, size_t size,
                                gaps_sram_dma_owner_t owner,
                                gaps_sram_lease_t *lease);
void gaps_sram_dma_release(gaps_sram_lease_t lease,
                           gaps_sram_dma_owner_t owner);

/* Called with interrupts fenced by the native dcload syscall boundary.
   Prevent loader network I/O while a KOS driver owns or changes the bridge. */
bool gaps_native_loader_allowed(void);

#endif /* __KERNEL_ARCH_DREAMCAST_HARDWARE_GAPS_INTERNAL_H */
