/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Destructive IC-array probe: invalidates the entire instruction cache.
   No instructions are fetched from the synthetic entries. */
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

extern void icache_array_probe(uintptr_t start, size_t length,
                               uint32_t flags, uint32_t *result);
static uint8_t buffer[8192 + 128] __attribute__((aligned(8192)));
static uint32_t result[256];
static volatile uint32_t *const cache_control = (volatile uint32_t *)0xff00001c;

int main(void) {
    unsigned cases = 0;
    static const unsigned offsets[] = {0, 31, 4095};
    volatile uint8_t *dirty = buffer;
    for(unsigned i = 0; i < sizeof(buffer); ++i)
        dirty[i] = (uint8_t)i;

    for(unsigned iix = 0; iix < 2; ++iix) {
        for(unsigned sync = 0; sync < 2; ++sync) {
            /* The high alias is used only for IC invalidation: never issue
               SDRAM/cache writeback through the mod-sensitive A25 mirror. */
            for(unsigned high = 0; high < (sync ? 1u : 2u); ++high) {
                for(unsigned bit12 = 0; bit12 < 2; ++bit12) {
                    for(unsigned j = 0; j < 3; ++j) {
                        uintptr_t start = (uintptr_t)buffer + bit12 * 4096 + offsets[j];
                        bool touched[256] = {false};
                        if(high) start |= 1u << 25;
                        uintptr_t last = (start + 64) & ~(uintptr_t)31;
                        for(uintptr_t a = start & ~(uintptr_t)31; a <= last; a += 32) {
                            unsigned index = (a >> 5) & 255;
                            if(iix) index = ((a >> 18) & 128) | ((a >> 5) & 127);
                            touched[index] = true;
                        }
                        uint32_t ccr = *cache_control;
                        icache_array_probe(start, 65, (iix ? CCR_IIX : 0) | sync, result);
                        if(*cache_control != ccr) {
                            puts("ICACHE-IIX: FAIL CCR restoration");
                            return EXIT_FAILURE;
                        }
                        for(unsigned entry = 0; entry < 256; ++entry) {
                            if((result[entry] & 1u) != !touched[entry]) {
                                printf("ICACHE-IIX: FAIL case=%u iix=%u sync=%u entry=%u valid=%lu expected=%u\n",
                                       cases, iix, sync, entry, result[entry] & 1u, !touched[entry]);
                                return EXIT_FAILURE;
                            }
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    printf("ICACHE-IIX: PASS cases=%u entries=256 restore=1\n", cases);
    return EXIT_SUCCESS;
}
