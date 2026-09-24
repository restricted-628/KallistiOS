/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Standalone destructive OCRAM ownership test. No other scratchpad users. */
#include <kos.h>
#include <dc/cache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef INIT_MMU
KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_MMU);
#else
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

static volatile uint32_t ram[1024] __attribute__((aligned(32)));
static volatile uint32_t *const ccr = (volatile uint32_t *)0xff00001cu;

static uint32_t pattern(unsigned index, unsigned pass) {
    return 0x619a4007u ^ index * 2654435761u ^ pass * 0x1020305u;
}

static volatile uint32_t *bank(unsigned index, uint32_t mode) {
    return (volatile uint32_t *)(uintptr_t)(index == 0 ? 0x7c001000u :
                                           mode & CCR_OIX ? 0x7e001000u : 0x7c002000u);
}

static int transition(uint32_t mask, uint32_t value, unsigned pass,
                      bool check_scratch) {
    volatile uint32_t *p2 = (volatile uint32_t *)
        (((uintptr_t)ram & 0x1fffffffu) | 0xa0000000u);
    uint32_t expected = ((*ccr & ~mask) | value) & ~(CCR_OCI | CCR_ICI);
    /* Ordinary backed RAM only: no high-A25 SDRAM alias in this probe. */
    dcache_purge_range((uintptr_t)ram, sizeof(ram));
    for(unsigned i = 0; i < 1024; ++i)
        p2[i] = 0;
    for(unsigned i = 0; i < 1024; ++i)
        ram[i] = pattern(i, pass);
    cache_write_ccr(mask, value);
    if(*ccr != expected)
        return 1;
    for(unsigned i = 0; i < 1024; ++i) {
        if(p2[i] != pattern(i, pass))
            return 2;
    }
    if(check_scratch) {
        for(unsigned b = 0; b < 2; ++b) {
            volatile uint32_t *p = bank(b, expected);
            for(unsigned i = 0; i < 1024; ++i) {
                if(p[i] != pattern(i, b + 77))
                    return 3;
            }
        }
    }
    return 0;
}

int main(void) {
    const uint32_t original = *ccr;
    if(original & (CCR_ORA | CCR_OIX | CCR_IIX) || !(original & CCR_OCE)) {
        puts("CACHE-TRANSITION: UNAVAILABLE requires normal cache mode");
        return EXIT_FAILURE;
    }
    irq_mask_t irq = irq_disable();
    unsigned cases = 0;
    int failure = transition(CCR_ORA, CCR_ORA, cases++, false);
    if(!failure) {
        for(unsigned b = 0; b < 2; ++b) {
            volatile uint32_t *p = bank(b, *ccr);
            for(unsigned i = 0; i < 1024; ++i)
                p[i] = pattern(i, b + 77);
        }
        const struct { uint32_t mask, value; } changes[] = {
            {0, 0}, {CCR_IIX, CCR_IIX | CCR_ICI},
            {CCR_IIX, CCR_ICI}, {CCR_OIX, CCR_OIX},
            {0, 0}, {CCR_OIX, 0}
        };
        for(unsigned i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i) {
            failure = transition(changes[i].mask, changes[i].value, cases++, true);
            if(failure)
                break;
        }
    }
    if(!failure)
        failure = transition(CCR_ORA, 0, cases++, false);
    /* Relinquish scratchpad and restore all original mode bits on failures too. */
    cache_write_ccr(UINT32_MAX, original | CCR_ICI);
    if(*ccr != original)
        failure = 4;
    irq_restore(irq);
    if(failure) {
        printf("CACHE-TRANSITION: FAIL case=%u reason=%d\n", cases - 1, failure);
        return EXIT_FAILURE;
    }
    printf("CACHE-TRANSITION: PASS cases=%u banks=2 restore=1\n", cases);
    return EXIT_SUCCESS;
}
