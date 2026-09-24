/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Independent translated-workspace/cache lifecycle test. No SDK assets.
*/
#include <kos.h>
#include <arch/mmu.h>
#include <dc/cache.h>
#include <stdlib.h>
#include <stdio.h>

static uintptr_t phys(const void *p) {
    return (uintptr_t)p & MEM_AREA_CACHE_MASK;
}

static volatile uint32_t *uncached(const void *p) {
    return (volatile uint32_t *)(phys(p) | MEM_AREA_P2_BASE);
}

/* First access must survive a data TLB miss, including interrupted PR. */
__attribute__((noinline)) static void fill(volatile uint32_t *p, uint32_t seed) {
    for(unsigned i = 0; i < PAGESIZE / sizeof(*p); ++i)
        p[i] = seed ^ i;
}

static bool check(const void *p, uint32_t seed) {
    volatile uint32_t *q = uncached(p);
    for(unsigned i = 0; i < PAGESIZE / sizeof(*q); ++i)
        if(q[i] != (seed ^ i))
            return false;
    return true;
}

static bool map(mmucontext_t *c, uintptr_t va, void *p) {
    return mmu_page_map_ex(c, va >> PAGESIZE_BITS, phys(p) >> PAGESIZE_BITS,
                           1, MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true) == 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t ccr = *(volatile uint32_t *)0xff00001c;
    if(!mmu_enabled() || mmu_cxt_current || (ccr & (CCR_ORA | CCR_OIX))) {
        puts("MMU-OIX: FAIL startup ownership");
        return EXIT_FAILURE;
    }
    /* On 32 MiB systems reserve enough heap to test a page genuinely above
       the 16 MiB boundary, without borrowing any unallocated physical RAM. */
    size_t upper_offset = HW_MEMSIZE == HW_MEM_32 ? HW_MEM_16 : 0;
    uint8_t *storage = aligned_alloc(PAGESIZE, upper_offset + 2 * PAGESIZE);
    mmucontext_t *c = NULL;
    bool passed = false;
    unsigned completed = 0;
    if(!storage)
        goto out;

    for(unsigned mode = 0; mode < 2; ++mode) {
        dcache_toggle_ocindex(mode != 0);
        for(unsigned bank = 0; bank < (upper_offset ? 2u : 1u); ++bank) {
            uint8_t *a = storage + (bank ? upper_offset : 0);
            uint8_t *b = a + PAGESIZE;
            uintptr_t pa = phys(a);
            /* Deliberately different normal-mode color, plus virtual A25=1.
               Both physical pages retain A25=0, including upper RAM. */
            uintptr_t va = 0x0e000000u | ((pa ^ 0x1000u) & 0x3000u);
            volatile uint32_t *v = (volatile uint32_t *)va;
            if(pa < 0x0c000000u || pa + 2 * PAGESIZE > 0x0c000000u + HW_MEMSIZE ||
               (pa & 0x02000000u) || (bank && pa < 0x0d000000u))
                goto out;
            fill((uint32_t *)a, 0x12340000u);
            fill((uint32_t *)b, 0xabcd0000u);
            dcache_purge_range((uintptr_t)a, 2 * PAGESIZE);
            c = mmu_context_create(3);
            if(!c || !map(c, va, a))
                goto out;
            mmu_use_table(c);
            mmu_switch_context(c);
            if(v[17] != (0x12340000u ^ 17)) {
                puts("MMU-OIX: UNAVAILABLE general translation; no cache result");
                goto out;
            }
            fill(v, 0x44440000u);
            dcache_wback_all();
            if(!check(a, 0x44440000u))
                goto out;
            fill(v, 0x55550000u);
            dcache_purge_all();
            if(!check(a, 0x55550000u) || v[17] != (0x55550000u ^ 17))
                goto out;
            fill(v, 0x56780000u);
            if(mmu_page_unmap(c, va >> PAGESIZE_BITS, 1) < 0 || !check(a, 0x56780000u))
                goto out;
            if(!map(c, va, a))
                goto out;
            fill(v, 0x11110000u);
            if(mmu_page_set_cache(c, va >> PAGESIZE_BITS, 1, MMU_NO_CACHE) < 0 ||
               !check(a, 0x11110000u))
                goto out;
            if(!map(c, va, a))
                goto out;
            fill(v, 0x22220000u);
            if(!map(c, va, b) || !check(a, 0x22220000u) ||
               v[17] != (0xabcd0000u ^ 17))
                goto out;
            fill(v, 0x33330000u);
            /* Prove retirement does not depend on current page tables. */
            mmu_use_table(NULL);
            mmu_context_destroy(c);
            c = NULL;
            if(!check(b, 0x33330000u))
                goto out;
            ++completed;
            printf("MMU-OIX: case oix=%u bank=%u pa=%08lx PASS\n",
                   mode, bank, (unsigned long)pa);
        }
    }
    passed = true;
out:
    if(c)
        mmu_context_destroy(c);
    cache_write_ccr(CCR_OIX, ccr & CCR_OIX);
    free(storage);
    printf("MMU-OIX: %s cases=%u ram=%lu\n", passed ? "PASS" : "FAIL",
           completed, (unsigned long)HW_MEMSIZE);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
