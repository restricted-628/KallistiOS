/* KallistiOS ##version##

   hardware/g1_bus.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/arch.h>

#include <dc/asic.h>

#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <kos/timer.h>

#include "g1_bus.h"

/* Internal MMIO boundary used by the production-source host test. */
#ifndef G1_BUS_IN8
#define G1_BUS_IN8(address)  (*(volatile uint8_t *)(address))
#endif
#ifndef G1_BUS_IN32
#define G1_BUS_IN32(address) (*(volatile uint32_t *)(address))
#endif
#ifndef G1_BUS_OUT8
#define G1_BUS_OUT8(address, value) \
    (*(volatile uint8_t *)(address) = (uint8_t)(value))
#endif
#ifndef G1_BUS_OUT32
#define G1_BUS_OUT32(address, value) \
    (*(volatile uint32_t *)(address) = (uint32_t)(value))
#endif

/* The GD-ROM master and ATA slave are two devices behind one Holly G1
   controller, not independent buses. This semaphore therefore represents
   ownership of the controller, selected task-file device, and shared DMA
   registers as one indivisible claim. IRQ clients cannot take the semaphore;
   they only identify and publish completion for the thread that already owns
   it. A transport that cannot quiesce DMA latches the fault flag so no later
   client can mistake an available semaphore for safe hardware. */
static semaphore_t g1_bus_sem = SEM_INITIALIZER(1);
static volatile bool g1_bus_faulted;

#define G1_BUS_DMA_CLIENT_COUNT 4
#define G1_BUS_DMA_EVENT_COUNT  4

typedef struct g1_bus_dma_client {
    g1_bus_dma_irq_handler_t handler;
    void *data;
} g1_bus_dma_client_state_t;

typedef struct g1_bus_dma_event {
    uint16_t code;
    asic_evt_claim_t claim;
} g1_bus_dma_event_t;

static g1_bus_dma_client_state_t dma_clients[G1_BUS_DMA_CLIENT_COUNT];
static g1_bus_dma_event_t dma_events[G1_BUS_DMA_EVENT_COUNT] = {
    { ASIC_EVT_GD_DMA,         ASIC_EVT_CLAIM_INVALID },
    { ASIC_EVT_GD_DMA_OVERRUN, ASIC_EVT_CLAIM_INVALID },
    { ASIC_EVT_GD_DMA_ILLADDR, ASIC_EVT_CLAIM_INVALID },
    { ASIC_EVT_GD_DMA_ACCESS,  ASIC_EVT_CLAIM_INVALID }
};
static unsigned int dma_client_count;

static g1_bus_dma_client_state_t gd_command_client;
static asic_evt_claim_t gd_command_claim = ASIC_EVT_CLAIM_INVALID;

static uint8_t selected_device;
static bool selected_device_valid;

static void dma_irq_dispatch(uint32_t code, void *data) {
    unsigned int i;

    (void)data;

    for(i = 0; i < G1_BUS_DMA_CLIENT_COUNT; ++i) {
        if(dma_clients[i].handler
                && dma_clients[i].handler(code, dma_clients[i].data))
            return;
    }
}

static void gd_command_irq_dispatch(uint32_t code, void *data) {
    (void)data;

    if(gd_command_client.handler
            && gd_command_client.handler(code, gd_command_client.data))
        return;
}

static int dma_dispatch_install(void) {
    unsigned int i;

    for(i = 0; i < G1_BUS_DMA_EVENT_COUNT; ++i) {
        if(asic_evt_claim(dma_events[i].code, ASIC_IRQB,
                          dma_irq_dispatch, NULL,
                          &dma_events[i].claim) < 0)
            goto fail;
    }

    return 0;

fail:
    while(i > 0) {
        --i;
        (void)asic_evt_release(dma_events[i].claim);
        dma_events[i].claim = ASIC_EVT_CLAIM_INVALID;
    }

    return -1;
}

static void dma_dispatch_remove(void) {
    unsigned int i;

    for(i = 0; i < G1_BUS_DMA_EVENT_COUNT; ++i) {
        if(dma_events[i].claim != ASIC_EVT_CLAIM_INVALID)
            (void)asic_evt_release(dma_events[i].claim);
        dma_events[i].claim = ASIC_EVT_CLAIM_INVALID;
    }
}

static bool bus_faulted(void) {
    irq_mask_t irq_state = irq_disable();
    bool faulted = g1_bus_faulted;

    irq_restore(irq_state);
    return faulted;
}

static int reject_faulted_lock(void) {
    /* The waiter consumed the semaphore just before observing the latched
       fault. Return that token for other waiters, all of which also fail EIO. */
    sem_signal(&g1_bus_sem);
    errno = EIO;
    return -1;
}

int g1_bus_lock(void) {
    int rv;

    if(bus_faulted()) {
        errno = EIO;
        return -1;
    }

    rv = sem_wait_irqsafe(&g1_bus_sem);
    if(rv < 0)
        return rv;
    return bus_faulted() ? reject_faulted_lock() : 0;
}

int g1_bus_lock_timed(uint32_t timeout) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(bus_faulted()) {
        errno = EIO;
        return -1;
    }

    if(sem_wait_timed(&g1_bus_sem, timeout) < 0)
        return -1;
    return bus_faulted() ? reject_faulted_lock() : 0;
}

int g1_bus_trylock(void) {
    if(bus_faulted()) {
        errno = EIO;
        return -1;
    }

    if(sem_trywait(&g1_bus_sem) < 0)
        return -1;
    return bus_faulted() ? reject_faulted_lock() : 0;
}

int g1_bus_unlock(void) {
    if(bus_faulted()) {
        errno = EIO;
        return -1;
    }

    /* Retail systems normally have the GD-ROM as the master device. */
    if(hardware_sys_mode(NULL) == HW_TYPE_RETAIL)
        g1_bus_select_device(0);

    return sem_signal(&g1_bus_sem);
}

void g1_bus_mark_faulted(void) {
    irq_mask_t irq_state = irq_disable();

    if(!g1_bus_faulted) {
        g1_bus_faulted = true;
        sem_signal(&g1_bus_sem);
    }
    irq_restore(irq_state);
}

bool g1_bus_is_faulted(void) {
    return bus_faulted();
}

int g1_bus_dma_in_progress(void) {
    return G1_BUS_IN32(G1_ATA_DMA_STATUS) != 0;
}

void g1_bus_dma_disable(void) {
    G1_BUS_OUT32(G1_ATA_DMA_ENABLE, 0);
}

g1_bus_dma_client_t g1_bus_dma_client_register(
        g1_bus_dma_irq_handler_t handler, void *data) {
    irq_mask_t irq_state;
    unsigned int i;

    if(!handler) {
        errno = EINVAL;
        return G1_BUS_DMA_CLIENT_INVALID;
    }

    irq_state = irq_disable();

    for(i = 0; i < G1_BUS_DMA_CLIENT_COUNT; ++i) {
        if(!dma_clients[i].handler)
            break;
    }

    if(i == G1_BUS_DMA_CLIENT_COUNT) {
        irq_restore(irq_state);
        errno = ENOSPC;
        return G1_BUS_DMA_CLIENT_INVALID;
    }

    if(!dma_client_count && dma_dispatch_install() < 0) {
        irq_restore(irq_state);
        return G1_BUS_DMA_CLIENT_INVALID;
    }

    dma_clients[i].data = data;
    dma_clients[i].handler = handler;
    ++dma_client_count;
    irq_restore(irq_state);

    return (g1_bus_dma_client_t)i;
}

int g1_bus_dma_client_unregister(g1_bus_dma_client_t client) {
    irq_mask_t irq_state;

    if(client < 0 || client >= G1_BUS_DMA_CLIENT_COUNT) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();

    if(!dma_clients[client].handler) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    dma_clients[client].handler = NULL;
    dma_clients[client].data = NULL;
    --dma_client_count;

    if(!dma_client_count)
        dma_dispatch_remove();

    irq_restore(irq_state);
    return 0;
}

int g1_bus_gd_command_client_register(g1_bus_dma_irq_handler_t handler,
                                      void *data) {
    irq_mask_t irq_state;

    if(!handler) {
        errno = EINVAL;
        return -1;
    }

    irq_state = irq_disable();
    if(gd_command_client.handler) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    if(asic_evt_claim(ASIC_EVT_GD_COMMAND, ASIC_IRQB,
                      gd_command_irq_dispatch, NULL,
                      &gd_command_claim) < 0) {
        irq_restore(irq_state);
        return -1;
    }

    gd_command_client.data = data;
    gd_command_client.handler = handler;
    irq_restore(irq_state);
    return 0;
}

int g1_bus_gd_command_client_mask(void) {
    irq_mask_t irq_state = irq_disable();

    if(!gd_command_client.handler) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    if(asic_evt_claim_mask(gd_command_claim) < 0) {
        irq_restore(irq_state);
        return -1;
    }
    irq_restore(irq_state);
    return 0;
}

int g1_bus_gd_command_client_unmask(void) {
    irq_mask_t irq_state = irq_disable();

    if(!gd_command_client.handler) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    if(asic_evt_claim_unmask(gd_command_claim) < 0) {
        irq_restore(irq_state);
        return -1;
    }
    irq_restore(irq_state);
    return 0;
}

int g1_bus_gd_command_client_unregister(void) {
    irq_mask_t irq_state = irq_disable();

    if(!gd_command_client.handler) {
        irq_restore(irq_state);
        errno = ENOENT;
        return -1;
    }

    gd_command_client.handler = NULL;
    gd_command_client.data = NULL;
    (void)asic_evt_release(gd_command_claim);
    gd_command_claim = ASIC_EVT_CLAIM_INVALID;
    irq_restore(irq_state);
    return 0;
}

uint8_t g1_bus_device_state_init(void) {
    selected_device = G1_BUS_IN8(G1_ATA_DEVICE_SELECT);
    selected_device_valid = true;
    return selected_device;
}

int g1_bus_wait_status(uint8_t set_bits, uint8_t clear_bits,
                       uint32_t timeout, uint8_t *result) {
    const uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    uint8_t status;

    for(;;) {
        status = G1_BUS_IN8(G1_ATA_ALTSTATUS);
        if((status & set_bits) == set_bits && !(status & clear_bits)) {
            if(result)
                *result = status;
            return 0;
        }

        if(timeout && timer_ms_gettime64() >= deadline) {
            if(result)
                *result = status;
            errno = ETIMEDOUT;
            return -1;
        }

        if(!irq_inside_int())
            thd_pass();
    }
}

uint8_t g1_bus_select_device(uint8_t device) {
    uint8_t old = G1_BUS_IN8(G1_ATA_DEVICE_SELECT);

    if(!selected_device_valid) {
        selected_device = old;
        selected_device_valid = true;
    }

    if(((device ^ selected_device) & G1_ATA_DEVICE_SLAVE_BIT)) {
        if(irq_inside_int()) {
            if(g1_bus_dma_in_progress()
                    || (G1_BUS_IN8(G1_ATA_ALTSTATUS)
                        & (G1_ATA_SR_DRQ | G1_ATA_SR_BSY)))
                return 0x0f;
        }
        else {
            if(g1_bus_select_device_timed(device, 0, &old) < 0)
                return 0x0f;
            return old;
        }
    }

    G1_BUS_OUT8(G1_ATA_DEVICE_SELECT, device);
    selected_device = device;
    return old;
}

int g1_bus_select_device_timed(uint8_t device, uint32_t timeout,
                               uint8_t *previous) {
    const uint64_t deadline = timeout ? timer_ms_gettime64() + timeout : 0;
    uint64_t now;
    uint32_t remaining;
    uint8_t old;

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    old = G1_BUS_IN8(G1_ATA_DEVICE_SELECT);
    if(!selected_device_valid) {
        selected_device = old;
        selected_device_valid = true;
    }

    if(((device ^ selected_device) & G1_ATA_DEVICE_SLAVE_BIT)) {
        while(g1_bus_dma_in_progress()) {
            if(timeout && timer_ms_gettime64() >= deadline) {
                errno = ETIMEDOUT;
                return -1;
            }
            thd_pass();
        }

        if(timeout) {
            now = timer_ms_gettime64();
            if(now >= deadline) {
                errno = ETIMEDOUT;
                return -1;
            }
            remaining = (uint32_t)(deadline - now);
        }
        else {
            remaining = 0;
        }

        if(g1_bus_wait_status(0, G1_ATA_SR_DRQ | G1_ATA_SR_BSY,
                              remaining, NULL) < 0)
            return -1;
    }

    G1_BUS_OUT8(G1_ATA_DEVICE_SELECT, device);
    selected_device = device;
    if(previous)
        *previous = old;
    return 0;
}
