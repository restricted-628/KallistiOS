/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/

#include "dma_memory.h"
#include <assert.h>
#include <stdio.h>

#define RAM16 UINT32_C(0x01000000)
#define RAM32 UINT32_C(0x02000000)

static void test_span(uintptr_t base, uint32_t ram_size, bool mirror) {
    uintptr_t top = base + ram_size;

    assert(dc_dma_main_ram_contains(base, ram_size, ram_size, mirror));
    assert(dc_dma_main_ram_contains(base, 32, ram_size, mirror));
    assert(dc_dma_main_ram_contains(top - 32, 32, ram_size, mirror));
    assert(!dc_dma_main_ram_contains(top - 32, 64, ram_size, mirror));
    assert(!dc_dma_main_ram_contains(base, (size_t)ram_size + 1, ram_size, mirror));
    assert(!dc_dma_main_ram_contains(base, SIZE_MAX, ram_size, mirror));
    assert(!dc_dma_main_ram_contains(base, 0, ram_size, mirror));
}

int main(void) {
    test_span(0x0c000000u, RAM16, false);
    test_span(0x0c000000u, RAM32, false);
    test_span(0x0e000000u, RAM16, true);
    test_span(0x0e000000u, RAM32, true);

    assert(!dc_dma_main_ram_contains(0x0bffffe0u, 64, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x0d000000u, 32, RAM16, true));
    assert(dc_dma_main_ram_contains(0x0d000000u, 32, RAM32, false));
    assert(!dc_dma_main_ram_contains(0x0cffffe0u, 64, RAM16, true));
    assert(dc_dma_main_ram_contains(0x0cffffe0u, 64, RAM32, false));
    assert(!dc_dma_main_ram_contains(0x0dffffe0u, 64, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x0e000000u, 32, RAM32, false));
    assert(dc_dma_main_ram_contains(0x0e000000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x0f000000u, 32, RAM16, true));
    assert(dc_dma_main_ram_contains(0x0f000000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x10000000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(UINTPTR_MAX, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x8d000000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x04000000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x00800000u, 32, RAM32, true));
    assert(!dc_dma_main_ram_contains(0x0c000000u, 32, 0, true));
    assert(!dc_dma_main_ram_contains(0x0c000000u, 32, 0x04000000u, true));
    puts("DMA main-RAM range tests passed (16/32 MiB, canonical/index mirror)");
    return 0;
}
