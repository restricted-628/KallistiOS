#ifndef DIRECT_RAW_DMA_MMIO_SHIM_H
#define DIRECT_RAW_DMA_MMIO_SHIM_H
#include <stdint.h>
#include <kos/cache.h>
uint8_t dma_probe_in8(uintptr_t address);
uint16_t dma_probe_in16(uintptr_t address);
uint32_t dma_probe_in32(uintptr_t address);
void dma_probe_out8(uintptr_t address, uint8_t value);
void dma_probe_out16(uintptr_t address, uint16_t value);
void dma_probe_out32(uintptr_t address, uint32_t value);
void dma_probe_invalidate(uintptr_t address, size_t size);
#define G1_IN8(a) dma_probe_in8(a)
#define G1_IN16(a) dma_probe_in16(a)
#define G1_IN32(a) dma_probe_in32(a)
#define G1_OUT8(a, v) dma_probe_out8(a, v)
#define G1_OUT16(a, v) dma_probe_out16(a, v)
#define G1_OUT32(a, v) dma_probe_out32(a, v)
#define dcache_inval_range(a, n) dma_probe_invalidate(a, n)
#endif
