/* Exercise the production destination policy before expanding its contract. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MEM_AREA_CACHE_MASK UINT32_C(0x1fffffff)
#define MEM_AREA_P0_BASE UINT32_C(0x00000000)
#define MEM_AREA_P1_BASE UINT32_C(0x80000000)
#define MEM_AREA_P2_BASE UINT32_C(0xa0000000)
#define MEM_AREA_P3_BASE UINT32_C(0xc0000000)
#define HW_TYPE_RETAIL 0
#define GAPS_SRAM_PHYS_BASE UINT32_C(0x01840000)
#define GAPS_SRAM_SIZE UINT32_C(0x8000)

static bool translated;
static int board;
static uint32_t ram_size;
#define HW_MEMSIZE ram_size
static bool mmu_enabled(void) { return translated; }
static int hardware_sys_mode(void *unused) { (void)unused; return board; }

#include "gdrom_dma_destination.h"

static void span(uint32_t base, uint32_t bus_base, uint32_t size,
                 gdrom_dma_destination_t kind, bool leased) {
    const uintptr_t aliases[] = {MEM_AREA_P0_BASE, MEM_AREA_P1_BASE,
                                MEM_AREA_P2_BASE, MEM_AREA_P3_BASE};
    for(unsigned mmu = 0; mmu < 2; ++mmu) {
        translated = mmu;
        for(unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
            uintptr_t alias = aliases[i];
            bool direct = alias == MEM_AREA_P1_BASE || alias == MEM_AREA_P2_BASE;
            gdrom_dma_destination_t expected = mmu && !direct
                ? GDROM_DMA_DESTINATION_INVALID : kind;
            uint32_t bus_address;
            assert(dma_destination_classify(alias | base, size, leased,
                                            &bus_address) == expected);
            if(expected != GDROM_DMA_DESTINATION_INVALID)
                assert(bus_address == bus_base);
            assert(dma_destination_classify(alias | (base + size - 32), 32,
                                            leased, &bus_address) == expected);
            if(expected != GDROM_DMA_DESTINATION_INVALID)
                assert(bus_address == bus_base + size - 32);
            assert(dma_destination_classify(alias | (base + size - 32), 64,
                                            leased, NULL) == GDROM_DMA_DESTINATION_INVALID);
            assert(dma_destination_classify(alias | base, SIZE_MAX, leased, NULL)
                   == GDROM_DMA_DESTINATION_INVALID);
            assert(dma_destination_cacheable(alias | base, kind)
                   == (kind == GDROM_DMA_DESTINATION_SYSTEM_RAM
                       && alias != MEM_AREA_P2_BASE));
        }
    }
    translated = false;
}

int main(void) {
    const struct { int board; uint32_t ram, vram; } profiles[] = {
        {HW_TYPE_RETAIL, 0x01000000, 0x00800000},
        {HW_TYPE_RETAIL, 0x02000000, 0x00800000},
        {9,              0x02000000, 0x01000000},
    };
    for(unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        board = profiles[i].board;
        ram_size = profiles[i].ram;
        span(0x0c000000, 0x0c000000, profiles[i].ram,
             GDROM_DMA_DESTINATION_SYSTEM_RAM, false);
        span(0x0e000000, 0x0e000000, profiles[i].ram,
             GDROM_DMA_DESTINATION_SYSTEM_RAM, false);
        span(0x04000000, 0x04000000, profiles[i].vram, GDROM_DMA_DESTINATION_VRAM, false);
        span(0x05000000, 0x05000000, profiles[i].vram, GDROM_DMA_DESTINATION_VRAM, false);
        span(0x06000000, 0x04000000, 0x00800000, GDROM_DMA_DESTINATION_VRAM, false);
        span(0x07000000, 0x05000000, 0x00800000, GDROM_DMA_DESTINATION_VRAM, false);
        span(GAPS_SRAM_PHYS_BASE, GAPS_SRAM_PHYS_BASE, GAPS_SRAM_SIZE,
             GDROM_DMA_DESTINATION_GAPS_SRAM, true);
        assert(dma_destination_classify(MEM_AREA_P2_BASE | GAPS_SRAM_PHYS_BASE,
                                        32, false, NULL) == GDROM_DMA_DESTINATION_INVALID);
    }

    /* These remain rejected by ordinary transport admission. Hardware image
       windows and new owned G2 destinations need their reviewed paths first.
       AICA registers/modem/G2 control are prohibited DMA destinations. */
    const uint32_t rejected[] = {
        0x00000000, 0x00200000,
        0x005f7800, 0x00600000, 0x00700000, 0x00710000,
        0x00800000, 0x009fffe0, 0x00ffffe0,
        0x01000000, 0x01ffffe0, 0x02700000, 0x02800000, 0x02ffffe0,
        0x03000000, 0x03ffffe0,
        0x04800000, 0x05800000, 0x06800000, 0x07800000,
        0x10000000, 0x10800000, 0x11000000, 0x12000000,
        0x14000000, 0x17ffffe0, 0x18000000, 0x1fffffe0,
    };
    board = HW_TYPE_RETAIL;
    ram_size = 0x01000000;
    for(unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
        assert(dma_destination_classify(MEM_AREA_P2_BASE | rejected[i], 32, true, NULL)
               == GDROM_DMA_DESTINATION_INVALID);
    assert(dma_destination_classify(0xec000000, 32, true, NULL)
           == GDROM_DMA_DESTINATION_INVALID);
    assert(dma_destination_classify(UINTPTR_MAX, 32, true, NULL)
           == GDROM_DMA_DESTINATION_INVALID);
    puts("GD destinations: PASS current spans, profiles, aliases, and ownership gate");
    return 0;
}
