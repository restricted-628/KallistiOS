#ifndef __EXPANSION_TEST_DC_ASIC_H
#define __EXPANSION_TEST_DC_ASIC_H

#include <stdbool.h>
#include <stdint.h>

#define ASIC_EVT_EXP_8BIT 0x0102
#define ASIC_EVT_EXP_PCI  0x0103

typedef struct asic_evt_status {
    uint16_t code;
    uint8_t enabled_levels;
    bool handler_present;
    bool exclusively_claimed;
    uint64_t dispatches;
} asic_evt_status_t;

int asic_evt_get_status(uint16_t code, asic_evt_status_t *status);

#endif
