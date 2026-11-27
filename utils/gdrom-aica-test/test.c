/* Exercise the production polling window with simulated MMIO. */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BIT(n) (UINT32_C(1) << (n))
#define FIFO_AICA BIT(0)
#define FIFO_G2 BIT(4)
#define FIFO_SH4 BIT(5)
#define ASIC_ACK_A 0xa05f6900u
#define ASIC_ACK_C 0xa05f6908u
#define ASIC_EVT_GD_DMA 0x000eu
#define ASIC_EVT_GD_DMA_ILLADDR 0x020cu
#define ASIC_EVT_GD_DMA_OVERRUN 0x020du
#define ASIC_EVT_GD_DMA_ACCESS 0x020eu
#define G1_ATA_DMA_ADDRESS 0xa05f7404u
#define G1_ATA_DMA_LENGTH 0xa05f7408u
#define G1_ATA_DMA_DIRECTION 0xa05f740cu
#define G1_ATA_DMA_ENABLE 0xa05f7414u
#define G1_ATA_DMA_STATUS 0xa05f7418u
#define G1_ATA_DMA_CURRENT_ADDR 0xa05f74f4u
#define G1_ATA_DMA_CURRENT_LEN 0xa05f74f8u
#define G1_DMA_TO_MEMORY 1u
#define G2_DMA_SUSPEND_ADDR(ch) (0xa05f781cu + ((ch) * 0x20u))
#define DMA_CHANNEL_1 1u
#define DMA_CHANNEL_3 3u

typedef unsigned irq_mask_t;
typedef struct {
    bool dma_event_seen;
    uint32_t dma_event, dma_current_address, dma_transferred;
} gdrom_direct_result_t;

static struct {
    uint64_t now;
    uint32_t start[4], enable[4], sh4_channels;
    uint32_t normal, errors, inject_error, drain_error, fifo_mask;
    unsigned programmed, starts, restores, fifo_polls, stop_writes;
    unsigned event_reads, idle_reads, event_delay, idle_delay;
    bool masked, busy, enabled, stuck_engine, stuck_fifo, initial_fifo_busy;
    bool timeout, faulted, watchdog_enabled, watchdog_stopped, idle_without_event;
} bus;
static jmp_buf halt;

static irq_mask_t irq_disable(void) {
    irq_mask_t old = bus.masked;
    bus.masked = true;
    return old;
}

static void irq_restore(irq_mask_t old) {
    assert(!bus.busy && !bus.enabled && (!bus.starts || bus.fifo_polls >= 3));
    bus.masked = old;
    ++bus.restores;
}

static bool dma_is_running(unsigned channel) {
    assert(bus.masked && !bus.starts);
    return (bus.sh4_channels & BIT(channel)) != 0;
}

static uint64_t timer_ms_gettime64(void) {
    assert(bus.masked);
    return bus.now++;
}

static bool g1_bus_dma_in_progress(void) {
    assert(bus.masked);
    if(bus.starts && bus.enabled && bus.idle_delay
            && ++bus.idle_reads >= bus.idle_delay)
        bus.busy = false;
    return bus.busy;
}

static void g1_bus_dma_disable(void) {
    assert(bus.masked);
    ++bus.stop_writes;
    bus.enabled = false;
    if(!bus.stuck_engine)
        bus.busy = false;
}

static void wdt_disable(void) {
    assert(bus.masked);
    bus.watchdog_stopped = true;
}

static int wdt_is_enabled(void) {
    assert(bus.masked && !bus.starts);
    return bus.watchdog_enabled;
}

static void g1_bus_mark_faulted(void) {
    assert(bus.masked);
    bus.faulted = true;
}

static uint32_t read_register(uint32_t address) {
    assert(bus.masked);
    for(unsigned i = 0; i < 4; ++i) {
        if(address == G2_DMA_SUSPEND_ADDR(i) - 4u) {
            assert(!bus.starts);
            return bus.start[i];
        }
        if(address == G2_DMA_SUSPEND_ADDR(i) - 8u) {
            assert(!bus.starts);
            return bus.enable[i];
        }
    }
    switch(address) {
        case ASIC_ACK_A:
            if(bus.starts && bus.enabled && bus.event_delay
                    && ++bus.event_reads >= bus.event_delay)
                bus.normal |= BIT(ASIC_EVT_GD_DMA);
            return bus.normal;
        case ASIC_ACK_C: return bus.errors;
        case G1_ATA_DMA_CURRENT_ADDR:
        case G1_ATA_DMA_CURRENT_LEN:
            assert(bus.starts && !bus.enabled && !bus.busy && bus.fifo_polls >= 3);
            if(address == G1_ATA_DMA_CURRENT_ADDR)
                return 0x00810800u;
            return 2048u;
        case 0xa05f688cu:
            if(!bus.starts)
                return bus.initial_fifo_busy ? bus.fifo_mask : 0;
            assert(!bus.busy && !bus.enabled);
            if(++bus.fifo_polls < 3 || bus.stuck_fifo)
                return bus.fifo_mask ? bus.fifo_mask : FIFO_AICA;
            bus.errors |= bus.drain_error;
            return 0;
        default: assert(false); return 0;
    }
}

static void write_register(uint32_t address, uint32_t value) {
    assert(bus.masked);
    switch(address) {
        case ASIC_ACK_A: bus.normal &= ~value; break;
        case ASIC_ACK_C: bus.errors &= ~value; break;
        case G1_ATA_DMA_ADDRESS:
            assert(value == 0x00810000u && bus.programmed++ == 0);
            break;
        case G1_ATA_DMA_LENGTH:
            assert(value == 2048u && bus.programmed++ == 1);
            break;
        case G1_ATA_DMA_DIRECTION:
            assert(value == G1_DMA_TO_MEMORY && bus.programmed++ == 2);
            break;
        case G1_ATA_DMA_ENABLE:
            assert(value == 1 && bus.programmed == 3);
            bus.enabled = true;
            break;
        case G1_ATA_DMA_STATUS:
            assert(value == 1 && bus.enabled && bus.programmed == 3);
            ++bus.starts;
            bus.busy = true;
            if(bus.inject_error)
                bus.errors |= bus.inject_error;
            else if(!bus.timeout) {
                if(!bus.event_delay && !bus.idle_without_event)
                    bus.normal |= BIT(ASIC_EVT_GD_DMA);
                if(!bus.idle_delay)
                    bus.busy = false;
            }
            break;
        default: assert(false);
    }
}

#define G1_IN32(address) read_register(address)
#define G1_OUT32(address, value) write_register(address, value)
#define G1_DMA_HALT() longjmp(halt, 1)
#include "gdrom_aica_poll.h"

static void refused(uint64_t deadline, int expected_error) {
    gdrom_direct_result_t result = {0};
    assert(poll_aica_dma(0x00810000u, 2048, deadline, &result) == -1);
    assert(errno == expected_error);
    assert(!bus.programmed && !bus.starts && bus.restores == 1 && !bus.masked);
    assert(!result.dma_event_seen && !result.dma_transferred);
}

static void retained_failure(bool engine) {
    memset(&bus, 0, sizeof(bus));
    bus.stuck_engine = engine;
    bus.stuck_fifo = !engine;
    bus.timeout = engine;
    if(setjmp(halt) == 0) {
        gdrom_direct_result_t result = {0};
        (void)poll_aica_dma(0x00810000u, 2048, 5, &result);
        assert(false);
    }
    assert(bus.masked && bus.starts == 1 && bus.stop_writes == 1);
    assert(!bus.restores && bus.faulted && bus.watchdog_stopped);
}

int main(void) {
    gdrom_direct_result_t result = {0};

    for(unsigned i = 0; i < 4; ++i) {
        memset(&bus, 0, sizeof(bus));
        bus.start[i] = 1;
        refused(40, EBUSY);
        memset(&bus, 0, sizeof(bus));
        bus.enable[i] = 1;
        refused(40, EBUSY);
    }
    for(unsigned channel = 1; channel <= 3; channel += 2) {
        memset(&bus, 0, sizeof(bus));
        bus.sh4_channels = BIT(channel);
        refused(40, EBUSY);
    }
    const uint32_t fifo_bits[] = {FIFO_SH4, FIFO_G2, FIFO_AICA};
    for(unsigned i = 0; i < sizeof(fifo_bits) / sizeof(fifo_bits[0]); ++i) {
        memset(&bus, 0, sizeof(bus));
        bus.initial_fifo_busy = true;
        bus.fifo_mask = fifo_bits[i];
        refused(40, EBUSY);

        memset(&bus, 0, sizeof(bus));
        bus.fifo_mask = fifo_bits[i];
        assert(poll_aica_dma(0x00810000u, 2048, 40, &result) == 0);
        assert(bus.fifo_polls == 3 && bus.restores == 1 && !bus.masked);
    }
    memset(&bus, 0, sizeof(bus));
    bus.watchdog_enabled = true;
    refused(40, EBUSY);
    memset(&bus, 0, sizeof(bus));
    refused(0, ETIMEDOUT);

    memset(&bus, 0, sizeof(bus));
    bus.normal = BIT(3) | GDROM_GD_COMPLETE_MASK;
    bus.errors = BIT(7) | GDROM_GD_ERROR_MASK;
    assert(poll_aica_dma(0x00810000u, 2048, 40, &result) == 0);
    assert(result.dma_event_seen && result.dma_event == ASIC_EVT_GD_DMA);
    assert(result.dma_current_address == 0x00810800u && result.dma_transferred == 2048u);
    assert(bus.starts == 1 && bus.fifo_polls == 3 && !bus.masked);
    assert(bus.normal == BIT(3) && bus.errors == BIT(7));

    const uint32_t events[] = {ASIC_EVT_GD_DMA_ILLADDR,
                              ASIC_EVT_GD_DMA_OVERRUN, ASIC_EVT_GD_DMA_ACCESS};
    for(unsigned i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        for(unsigned late = 0; late < 2; ++late) {
            memset(&bus, 0, sizeof(bus));
            memset(&result, 0, sizeof(result));
            if(late)
                bus.drain_error = BIT(events[i] & 31u);
            else
                bus.inject_error = BIT(events[i] & 31u);
            assert(poll_aica_dma(0x00810000u, 2048, 40, &result) == -1);
            assert(errno == EIO && result.dma_event == events[i]);
            assert(bus.stop_writes == 1 && bus.fifo_polls == 3 && !bus.masked);
        }
    }

    /* Completion and engine-idle may arrive in either order. Both are
       required before a successful transfer can enter the drain phase. */
    memset(&bus, 0, sizeof(bus));
    bus.event_delay = 4;
    assert(poll_aica_dma(0x00810000u, 2048, 40, &result) == 0);
    assert(bus.event_reads == 4 && bus.fifo_polls == 3);
    memset(&bus, 0, sizeof(bus));
    bus.idle_delay = 4;
    assert(poll_aica_dma(0x00810000u, 2048, 40, &result) == 0);
    assert(bus.idle_reads == 4 && bus.fifo_polls == 3);
    memset(&bus, 0, sizeof(bus));
    memset(&result, 0, sizeof(result));
    bus.idle_without_event = true;
    assert(poll_aica_dma(0x00810000u, 2048, 5, &result) == -1);
    assert(errno == ETIMEDOUT && !result.dma_event_seen);
    assert(bus.stop_writes == 1 && bus.fifo_polls == 3 && !bus.masked);

    memset(&bus, 0, sizeof(bus));
    memset(&result, 0, sizeof(result));
    bus.timeout = true;
    bus.normal = GDROM_GD_COMPLETE_MASK;
    assert(poll_aica_dma(0x00810000u, 2048, 5, &result) == -1);
    assert(errno == ETIMEDOUT && !result.dma_event_seen);
    assert(bus.stop_writes == 1 && bus.fifo_polls == 3 && !bus.masked);
    retained_failure(true);
    retained_failure(false);

    puts("GD-AICA: PASS admission, acknowledge, retirement, and retained halt");
    return 0;
}
