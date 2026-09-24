#ifndef __SCIF_CONFIG_TEST_DC_SCIF_H
#define __SCIF_CONFIG_TEST_DC_SCIF_H

#include <stdint.h>

typedef enum scif_parity {
    SCIF_PARITY_NONE = 0,
    SCIF_PARITY_EVEN,
    SCIF_PARITY_ODD
} scif_parity_t;

typedef enum scif_flow_control {
    SCIF_FLOW_NONE = 0,
    SCIF_FLOW_HARDWARE
} scif_flow_control_t;

typedef enum scif_rx_trigger {
    SCIF_RX_TRIGGER_1 = 1,
    SCIF_RX_TRIGGER_4 = 4,
    SCIF_RX_TRIGGER_8 = 8,
    SCIF_RX_TRIGGER_14 = 14
} scif_rx_trigger_t;

typedef enum scif_tx_trigger {
    SCIF_TX_TRIGGER_1 = 1,
    SCIF_TX_TRIGGER_2 = 2,
    SCIF_TX_TRIGGER_4 = 4,
    SCIF_TX_TRIGGER_8 = 8
} scif_tx_trigger_t;

typedef struct scif_config {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t stop_bits;
    scif_parity_t parity;
    scif_flow_control_t flow_control;
    scif_rx_trigger_t rx_trigger;
    scif_tx_trigger_t tx_trigger;
} scif_config_t;

#endif
