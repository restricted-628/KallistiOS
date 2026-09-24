/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Main-RAM smoke test, not a cache-mode or physical-hardware proof.
*/
#include <kos.h>
#include <arch/mmu.h>
#include <dc/cache.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef INIT_MMU
KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_MMU);
#else
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

static alignas(32) volatile uint32_t buffer[4096];

static uint32_t pattern(unsigned i, unsigned pass) {
    return 0x61720a5u ^ (i * 2654435761u) ^ (pass * 0xf0f0f0f0u);
}

static int run_case(unsigned operation, unsigned pass) {
    volatile uint32_t *const p2 = (volatile uint32_t *)
        (((uintptr_t)buffer & 0x1fffffffu) | 0xa0000000u);
    volatile uint32_t *const pteh = (volatile uint32_t *)0xff000000u;
    irq_mask_t irq = irq_disable();
    uint32_t old_pteh = *pteh;
    *pteh = 7; /* No P0 mapping: scans must not translate physical tags. */
    dcache_purge_range((uintptr_t)buffer, sizeof(buffer));
    for(unsigned i = 0; i < 4096; ++i)
        p2[i] = 0;
    for(unsigned i = 0; i < 4096; ++i)
        buffer[i] = pattern(i, pass);

    switch(operation) {
        case 0: arch_dcache_wback_all_indexed(); break;
        case 1: arch_dcache_purge_all_indexed(); break;
        case 2: dcache_wback_all(); break;
        case 3: dcache_purge_all(); break;
    }
    int result = 0;
    for(unsigned i = 0; i < 4096; ++i) {
        if(p2[i] != pattern(i, pass)) {
            result = -1;
            break;
        }
    }
    if(*pteh != 7)
        result = -1;
    *pteh = old_pteh;
    irq_restore(irq);
    return result;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uint32_t ccr = *(volatile uint32_t *)0xff00001cu;
    if(ccr & (CCR_OIX | CCR_ORA)) {
        puts("CACHE-WHOLE: UNAVAILABLE requires normal cache mode");
        return EXIT_FAILURE;
    }
    for(unsigned mode = 0; mode < 2; ++mode) {
        if(mode)
            mmu_init_basic();
        for(unsigned operation = 0; operation < 4; ++operation) {
            for(unsigned pass = 0; pass < 2; ++pass) {
                if(run_case(operation, pass) < 0) {
                    printf("CACHE-WHOLE: FAIL mmu=%u operation=%u pass=%u\n",
                           mode, operation, pass);
                    if(mode) mmu_shutdown_basic();
                    return EXIT_FAILURE;
                }
            }
        }
        if(mode)
            mmu_shutdown_basic();
    }
    if((*(volatile uint32_t *)0xff00001cu ^ ccr) & (CCR_OIX | CCR_ORA))
        return EXIT_FAILURE;
    puts("CACHE-WHOLE: PASS cases=16 mmu=2 words=65536 modes=unchanged");
    return EXIT_SUCCESS;
}
