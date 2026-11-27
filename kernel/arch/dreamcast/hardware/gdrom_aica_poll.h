/* Private polled G1 transfer window for AICA wave RAM. */
#ifndef __GDROM_AICA_POLL_H
#define __GDROM_AICA_POLL_H

/* This runs under one IRQ mask. TMU2 can account for one pending wrap,
   so keep polling and retirement together well below its one-second period. */
#define GDROM_AICA_MAX_POLL_MS 500u
#define GDROM_AICA_RETIRE_MS 100u
#define GDROM_AICA_FIFO_ADDR 0xa05f688cu
#define GDROM_AICA_FIFO_MASK (FIFO_SH4 | FIFO_G2 | FIFO_AICA)
#define GDROM_AICA_G2_START(ch) (0xa05f7818u + (ch) * 0x20u)
#define GDROM_AICA_G2_ENABLE(ch) (0xa05f7814u + (ch) * 0x20u)
#define GDROM_AICA_SR_MASK 0x100000f0u /* BL and IMASK */
#define GDROM_GD_COMPLETE_MASK BIT(ASIC_EVT_GD_DMA & 31u)
#define GDROM_GD_ERROR_MASK (BIT(ASIC_EVT_GD_DMA_ILLADDR & 31u) | \
                            BIT(ASIC_EVT_GD_DMA_OVERRUN & 31u) | \
                            BIT(ASIC_EVT_GD_DMA_ACCESS & 31u))

/* Caller owns idle G1. This helper masks IRQs before checking autonomous
   DMA, so no SH4 or interrupt callback can introduce a G2 access mid-window.
   It never waits on scheduler objects or reads the drive taskfile. */
static int poll_aica_dma(uint32_t address, uint32_t bytes, uint64_t deadline,
                         gdrom_direct_result_t *result) {
    irq_mask_t irq_state = irq_disable();
    uint64_t now = timer_ms_gettime64();
    uint64_t limit = now + GDROM_AICA_MAX_POLL_MS;
    uint64_t retire_deadline;
    uint32_t errors = 0;
    bool complete = false;
    int error = 0;

    if(deadline < limit)
        limit = deadline;
    if(now >= limit) {
        error = ETIMEDOUT;
        goto out;
    }
    /* A running watchdog could reset the machine during IRQ exclusion. */
    if(wdt_is_enabled()) {
        error = EBUSY;
        goto out;
    }

    /* Never suspend another owner's channel. ENABLE also catches a channel
       waiting for an external trigger even when its START bit is clear. */
    for(unsigned i = 0; i < 4; ++i) {
        if((G1_IN32(GDROM_AICA_G2_START(i)) & 1u)
                || (G1_IN32(GDROM_AICA_G2_ENABLE(i)) & 1u)) {
            error = EBUSY;
            goto out;
        }
    }
    if(dma_is_running(DMA_CHANNEL_1) || dma_is_running(DMA_CHANNEL_3)
            || (G1_IN32(GDROM_AICA_FIFO_ADDR) & GDROM_AICA_FIFO_MASK)) {
        error = EBUSY;
        goto out;
    }

    /* Status/Reason have already acknowledged the packet-request interrupt.
       Discard only our stale normal/error latches before arming GDST. */
    G1_OUT32(ASIC_ACK_A, GDROM_GD_COMPLETE_MASK);
    G1_OUT32(ASIC_ACK_C, GDROM_GD_ERROR_MASK);
    G1_OUT32(G1_ATA_DMA_ADDRESS, address);
    G1_OUT32(G1_ATA_DMA_LENGTH, bytes);
    G1_OUT32(G1_ATA_DMA_DIRECTION, G1_DMA_TO_MEMORY);
    G1_OUT32(G1_ATA_DMA_ENABLE, 1);
    G1_OUT32(G1_ATA_DMA_STATUS, 1);

    for(;;) {
        errors = G1_IN32(ASIC_ACK_C) & GDROM_GD_ERROR_MASK;
        complete = (G1_IN32(ASIC_ACK_A) & GDROM_GD_COMPLETE_MASK) != 0;
        if(errors) {
            error = EIO;
            break;
        }
        if(complete && !g1_bus_dma_in_progress())
            break;
        if(timer_ms_gettime64() >= limit) {
            error = ETIMEDOUT;
            break;
        }
    }

    g1_bus_dma_disable();
    retire_deadline = timer_ms_gettime64() + GDROM_AICA_RETIRE_MS;
    while(g1_bus_dma_in_progress()
            || (G1_IN32(GDROM_AICA_FIFO_ADDR) & GDROM_AICA_FIFO_MASK)) {
        if(timer_ms_gettime64() >= retire_deadline) {
            /* D05 also covers the buffered destination writes. No later
               CPU G2 access is safe while the destination remains live. */
            wdt_disable();
            g1_bus_mark_faulted();
            G1_DMA_HALT();
        }
    }

    errors |= G1_IN32(ASIC_ACK_C) & GDROM_GD_ERROR_MASK;
    complete |= (G1_IN32(ASIC_ACK_A) & GDROM_GD_COMPLETE_MASK) != 0;
    result->dma_current_address = G1_IN32(G1_ATA_DMA_CURRENT_ADDR);
    result->dma_transferred = G1_IN32(G1_ATA_DMA_CURRENT_LEN);
    result->dma_event_seen = complete || errors;
    if(errors & BIT(ASIC_EVT_GD_DMA_ILLADDR & 31u))
        result->dma_event = ASIC_EVT_GD_DMA_ILLADDR;
    else if(errors & BIT(ASIC_EVT_GD_DMA_OVERRUN & 31u))
        result->dma_event = ASIC_EVT_GD_DMA_OVERRUN;
    else if(errors)
        result->dma_event = ASIC_EVT_GD_DMA_ACCESS;
    else if(complete)
        result->dma_event = ASIC_EVT_GD_DMA;
    if(errors)
        error = EIO;
    G1_OUT32(ASIC_ACK_A, GDROM_GD_COMPLETE_MASK);
    G1_OUT32(ASIC_ACK_C, GDROM_GD_ERROR_MASK);

out:
    irq_restore(irq_state);
    if(error) {
        errno = error;
        return -1;
    }
    return 0;
}

#endif
