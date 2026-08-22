/* KallistiOS ##version##

   hardware/scif_config.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include "scif_config_internal.h"

#define SCIF_PERIPHERAL_CLOCK 50000000u

#define SCSMR_CHR             0x40u
#define SCSMR_PE              0x20u
#define SCSMR_PM              0x10u
#define SCSMR_STOP            0x08u

#define SCSCR_CKE1            0x02u

#define SCFCR_MCE             0x08u

static int encode_receive_trigger(scif_rx_trigger_t trigger,
                                  uint16_t *bits) {
    switch(trigger) {
        case SCIF_RX_TRIGGER_1:
            *bits = 0x00u;
            return 0;
        case SCIF_RX_TRIGGER_4:
            *bits = 0x40u;
            return 0;
        case SCIF_RX_TRIGGER_8:
            *bits = 0x80u;
            return 0;
        case SCIF_RX_TRIGGER_14:
            *bits = 0xc0u;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int encode_transmit_trigger(scif_tx_trigger_t trigger,
                                   uint16_t *bits) {
    switch(trigger) {
        case SCIF_TX_TRIGGER_8:
            *bits = 0x00u;
            return 0;
        case SCIF_TX_TRIGGER_4:
            *bits = 0x10u;
            return 0;
        case SCIF_TX_TRIGGER_2:
            *bits = 0x20u;
            return 0;
        case SCIF_TX_TRIGGER_1:
            *bits = 0x30u;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int encode_baud(uint32_t baud, scif_register_config_t *registers) {
    uint64_t best_error = UINT64_MAX;
    uint32_t best_actual = 0;
    uint8_t best_clock = 0;
    uint8_t best_rate = 0;

    if(baud == 0) {
        registers->control_clock = SCSCR_CKE1;
        registers->bit_rate = 0;
        registers->actual_baud = 0;
        registers->baud_error_ppm = 0;
        return 0;
    }

    /*
       Internal asynchronous rate = PCLK / (32 * 4^clock * (BRR + 1)).
       Test all four clock divisors and round BRR+1 to the nearest encodable
       value. The driver reports the resulting ppm error rather than imposing
       a policy limit that would reject a rate an application intentionally
       selected for a known peer.
    */
    for(uint8_t clock = 0; clock < 4; ++clock) {
        uint64_t divisor = 32ull << (clock * 2u);
        uint64_t denominator = divisor * baud;
        uint64_t count = (SCIF_PERIPHERAL_CLOCK + denominator / 2u) /
                         denominator;
        uint32_t actual;
        uint64_t error;

        if(count < 1u || count > 256u)
            continue;

        actual = (uint32_t)(SCIF_PERIPHERAL_CLOCK / (divisor * count));
        error = actual > baud ? actual - baud : baud - actual;

        if(error < best_error) {
            best_error = error;
            best_actual = actual;
            best_clock = clock;
            best_rate = (uint8_t)(count - 1u);
        }
    }

    if(best_error == UINT64_MAX) {
        errno = ERANGE;
        return -1;
    }

    registers->mode |= best_clock;
    registers->bit_rate = best_rate;
    registers->actual_baud = best_actual;
    registers->baud_error_ppm = (int32_t)(
        ((int64_t)best_actual - (int64_t)baud) * 1000000ll / baud);
    return 0;
}

int scif_config_encode(const scif_config_t *config,
                       scif_register_config_t *registers) {
    uint16_t receive_trigger;
    uint16_t transmit_trigger;

    if(!config || !registers) {
        errno = EINVAL;
        return -1;
    }

    *registers = (scif_register_config_t){ 0 };

    if(config->data_bits == 7)
        registers->mode |= SCSMR_CHR;
    else if(config->data_bits != 8) {
        errno = EINVAL;
        return -1;
    }

    if(config->stop_bits == 2)
        registers->mode |= SCSMR_STOP;
    else if(config->stop_bits != 1) {
        errno = EINVAL;
        return -1;
    }

    switch(config->parity) {
        case SCIF_PARITY_NONE:
            break;
        case SCIF_PARITY_EVEN:
            registers->mode |= SCSMR_PE;
            break;
        case SCIF_PARITY_ODD:
            registers->mode |= SCSMR_PE | SCSMR_PM;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    switch(config->flow_control) {
        case SCIF_FLOW_NONE:
            break;
        case SCIF_FLOW_HARDWARE:
            registers->fifo |= SCFCR_MCE;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if(encode_receive_trigger(config->rx_trigger, &receive_trigger) < 0 ||
       encode_transmit_trigger(config->tx_trigger, &transmit_trigger) < 0)
        return -1;

    registers->fifo |= receive_trigger | transmit_trigger;
    return encode_baud(config->baud, registers);
}
