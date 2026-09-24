#ifndef __G2DMA_TEST_SHIM_H
#define __G2DMA_TEST_SHIM_H

#include <stdint.h>

extern uint32_t g2_test_dma_registers[];

uint32_t g2_test_suspend_read(uint32_t channel);
void g2_test_suspend_write(uint32_t channel, uint32_t value);
uint32_t g2_test_start_read(uint32_t channel);
void g2_test_start_write(uint32_t channel, uint32_t value);
void g2_test_ack(uint32_t channel);

#define G2_DMA_REG_BASE ((uintptr_t)g2_test_dma_registers)
#define G2_DMA_START_READ(channel) g2_test_start_read(channel)
#define G2_DMA_START_WRITE(channel, value) g2_test_start_write(channel, value)
#define G2_DMA_ACK(channel) g2_test_ack(channel)
#define G2_DMA_SUSPEND_READ(channel) g2_test_suspend_read(channel)
#define G2_DMA_SUSPEND_WRITE(channel, value) \
    g2_test_suspend_write((channel), (value))

#endif
