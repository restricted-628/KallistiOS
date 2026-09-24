/* KallistiOS ##version##

   cache-safety.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/memory.h>

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static alignas(32) volatile uint32_t cache_line[8];

int main(int argc, char **argv) {
    volatile uint32_t *uncached =
        (volatile uint32_t *)(MEM_AREA_P2_BASE |
                              ((uintptr_t)cache_line & MEM_AREA_CACHE_MASK));

    (void)argc;
    (void)argv;

    dcache_purge_range((uintptr_t)cache_line, sizeof(cache_line));
    uncached[0] = 0;
    cache_line[0] = 0x13579bdfu;

    /* OCBWB is a no-op when issued against P2 itself. The cache API must
       canonicalize this alias before executing the instruction. */
    dcache_wback_range((uintptr_t)uncached, sizeof(cache_line));
    if(uncached[0] != 0x13579bdfu) {
        printf("Cache alias write-back failed: %08lx\n",
               (unsigned long)uncached[0]);
        return EXIT_FAILURE;
    }

    cache_line[0] = 0x2468ace0u;
    dcache_purge_range((uintptr_t)uncached, sizeof(cache_line));
    if(uncached[0] != 0x2468ace0u) {
        printf("Cache alias purge failed: %08lx\n",
               (unsigned long)uncached[0]);
        return EXIT_FAILURE;
    }

    /* Instruction synchronization also performs a data-cache write-back. Its
       P2 input must be canonicalized before OCBWB, just like the data helpers. */
    uncached[1] = 0;
    cache_line[1] = 0x0badc0deu;
    icache_sync_range((uintptr_t)&uncached[1], sizeof(uncached[1]));
    if(uncached[1] != 0x0badc0deu) {
        printf("I-cache sync alias write-back failed: %08lx\n",
               (unsigned long)uncached[1]);
        return EXIT_FAILURE;
    }
    icache_inval_range((uintptr_t)&uncached[1], sizeof(uncached[1]));

    /* These ranges have no representable exclusive endpoint. They must be
       rejected rather than wrapping and touching an unrelated address. */
    dcache_inval_range(UINTPTR_MAX - 15u, 32u);
    dcache_wback_range(UINTPTR_MAX - 15u, 32u);
    dcache_purge_range(UINTPTR_MAX - 15u, 32u);
    icache_inval_range(UINTPTR_MAX - 15u, 32u);
    icache_sync_range(UINTPTR_MAX - 15u, 32u);

    dcache_inval_range((uintptr_t)cache_line, 0);
    icache_inval_range((uintptr_t)cache_line, 0);

    printf("KOSCACHE alias=1 overflow=1\n");
    return EXIT_SUCCESS;
}
