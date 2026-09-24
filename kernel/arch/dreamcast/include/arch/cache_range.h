/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Internal arithmetic shared by the SH-4 cache helpers. No cache instructions
   or MMIO appear here; host tests can validate the actual range decisions.
*/
#ifndef __ARCH_CACHE_RANGE_H
#define __ARCH_CACHE_RANGE_H

#include <dc/memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cache-control instructions are no-ops when their operand names P2. Convert
   that direct, uncached alias back to the equivalent cacheable P1 address.
   P0/P3 addresses are left intact because they may carry MMU translations. */
static inline uintptr_t arch_cacheable_alias(uintptr_t address) {
    if((address & ~MEM_AREA_CACHE_MASK) == MEM_AREA_P2_BASE)
        return (address & MEM_AREA_CACHE_MASK) | MEM_AREA_P1_BASE;

    return address;
}

static inline bool arch_cache_range(uintptr_t start, size_t count,
                                    uintptr_t *first, uintptr_t *last) {
    if(!count || count - 1 > UINTPTR_MAX - start)
        return false;

    /* A single request must not cross a 512 MiB address-area boundary.
       In particular, normalizing the start of P2 must not reinterpret bytes
       belonging to a different translation/cache area at the other end. */
    if((start ^ (start + count - 1)) & ~(uintptr_t)MEM_AREA_CACHE_MASK)
        return false;

    *first = start & ~(uintptr_t)0x1f;
    *last = (start + count - 1) & ~(uintptr_t)0x1f;
    return true;
}

#endif /* __ARCH_CACHE_RANGE_H */
