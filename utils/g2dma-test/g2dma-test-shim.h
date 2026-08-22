#ifndef __G2DMA_TEST_SHIM_H
#define __G2DMA_TEST_SHIM_H

#include <stdint.h>

extern uint32_t g2_test_dma_registers[];

uint32_t g2_test_suspend_read(uint32_t channel);
void g2_test_suspend_write(uint32_t channel, uint32_t value);

#define G2_DMA_REG_BASE ((uintptr_t)g2_test_dma_registers)
#define G2_DMA_SUSPEND_READ(channel) g2_test_suspend_read(channel)
#define G2_DMA_SUSPEND_WRITE(channel, value) \
    g2_test_suspend_write((channel), (value))

#endif
