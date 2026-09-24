/* KallistiOS ##version##

   expansion.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <string.h>

#include <dc/asic.h>
#include <dc/expansion.h>
#include <dc/modem/modem.h>
#include <dc/net/broadband_adapter.h>
#include <dc/net/lan_adapter.h>

#include <kos/irq.h>

static uint32_t probe_sequence;

static uint32_t next_probe_sequence(void) {
    irq_mask_t irq_state = irq_disable();
    uint32_t sequence = ++probe_sequence;

    if(!sequence)
        sequence = ++probe_sequence;

    irq_restore(irq_state);
    return sequence;
}

static void describe_device(expansion_status_t *status,
                            expansion_device_type_t type) {
    status->type = type;
    status->present = type != EXPANSION_DEVICE_NONE;

    switch(type) {
        case EXPANSION_DEVICE_BROADBAND:
            status->capabilities = EXPANSION_CAP_PCI | EXPANSION_CAP_IRQ
                | EXPANSION_CAP_DMA | EXPANSION_CAP_NETWORK
                | EXPANSION_CAP_ETHERNET | EXPANSION_CAP_10MBIT
                | EXPANSION_CAP_100MBIT;
            status->maximum_bps = 100000000u;
            break;
        case EXPANSION_DEVICE_LAN:
            status->capabilities = EXPANSION_CAP_8BIT | EXPANSION_CAP_IRQ
                | EXPANSION_CAP_NETWORK | EXPANSION_CAP_ETHERNET
                | EXPANSION_CAP_10MBIT;
            status->maximum_bps = 10000000u;
            break;
        case EXPANSION_DEVICE_MODEM:
            status->capabilities = EXPANSION_CAP_8BIT | EXPANSION_CAP_IRQ
                | EXPANSION_CAP_NETWORK | EXPANSION_CAP_TELEPHONY;
            break;
        case EXPANSION_DEVICE_UNKNOWN_8BIT:
            status->capabilities = EXPANSION_CAP_8BIT | EXPANSION_CAP_IRQ;
            break;
        case EXPANSION_DEVICE_UNKNOWN_PCI:
            status->capabilities = EXPANSION_CAP_PCI | EXPANSION_CAP_IRQ;
            break;
        case EXPANSION_DEVICE_NONE:
            break;
    }
}

int expansion_probe(expansion_status_t *status, uint32_t flags) {
    asic_evt_status_t irq_8bit;
    asic_evt_status_t irq_pci;
    int result;

    if(!status || (flags & ~EXPANSION_PROBE_RESET_8BIT)) {
        errno = EINVAL;
        return -1;
    }

    memset(status, 0, sizeof(*status));
    status->probe_flags = flags;
    status->sequence = next_probe_sequence();

    if(asic_evt_get_status(ASIC_EVT_EXP_8BIT, &irq_8bit) < 0
            || asic_evt_get_status(ASIC_EVT_EXP_PCI, &irq_pci) < 0) {
        status->probe_error = errno;
        return -1;
    }

    /* The PCI signature is read-only and safe even while its driver is live. */
    if(bba_probe()) {
        describe_device(status, EXPANSION_DEVICE_BROADBAND);
        status->active = irq_pci.handler_present;
        status->complete = true;
        return 0;
    }

    if(irq_pci.handler_present) {
        describe_device(status, EXPANSION_DEVICE_UNKNOWN_PCI);
        status->active = true;
        status->complete = true;
        return 0;
    }

    if(irq_8bit.handler_present) {
        describe_device(status, modem_is_initialized()
                        ? EXPANSION_DEVICE_MODEM
                        : (la_is_initialized() ? EXPANSION_DEVICE_LAN
                           : EXPANSION_DEVICE_UNKNOWN_8BIT));
        status->active = true;
        status->complete = true;

        if(flags & EXPANSION_PROBE_RESET_8BIT) {
            status->probe_error = EBUSY;
            errno = EBUSY;
            return -1;
        }

        return 0;
    }

    if(!(flags & EXPANSION_PROBE_RESET_8BIT)) {
        status->complete = false;
        return 0;
    }

    status->reset_performed = true;
    result = la_probe();

    if(result < 0) {
        status->probe_error = errno;
        return -1;
    }

    if(result > 0) {
        describe_device(status, EXPANSION_DEVICE_LAN);
        status->complete = true;
        return 0;
    }

    result = modem_probe();

    if(result < 0) {
        status->probe_error = errno;
        return -1;
    }

    if(result > 0)
        describe_device(status, EXPANSION_DEVICE_MODEM);

    status->complete = true;
    return 0;
}
