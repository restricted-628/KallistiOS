/* KallistiOS ##version##

   hardware/dma_memory.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __DC_DMA_MEMORY_INTERNAL_H
#define __DC_DMA_MEMORY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Physical main-RAM admission, after the caller checks MMU/cache aliases.
   A retail machine can have 32 MiB of main RAM without gaining any VRAM or
   sound RAM. Use HW_MEMSIZE, not hardware_sys_mode(), for ram_size.

   GD-ROM transport also accepts the A25/index mirror at 0x0e000000. Keep
   canonical and mirrored spans separate: a transfer must not wrap from the
   end of real RAM back to its beginning. G2 retains canonical-only policy.
   This helper neither masks addresses nor changes SDRAM controller settings. */
static inline bool dc_dma_main_ram_contains(uintptr_t physical, size_t size,
                                            uint32_t ram_size,
                                            bool allow_index_mirror) {
    const uintptr_t base = UINT32_C(0x0c000000);
    const uintptr_t mirror = UINT32_C(0x0e000000);

    if(!size || (ram_size != UINT32_C(0x01000000)
                 && ram_size != UINT32_C(0x02000000)))
        return false;
    if(physical >= base && physical < base + ram_size
       && size <= base + ram_size - physical)
        return true;
    return allow_index_mirror
        && physical >= mirror && physical < mirror + ram_size
        && size <= mirror + ram_size - physical;
}

#endif /* __DC_DMA_MEMORY_INTERNAL_H */
