#ifndef __DC_ASIC_H
#define __DC_ASIC_H

#include <stdint.h>

#define ASIC_EVT_PVR_VBLANK_BEGIN UINT16_C(3)
#define ASIC_IRQ_DEFAULT UINT8_C(9)

typedef void (*asic_evt_handler)(uint32_t code, void *data);

typedef struct asic_evt_handler_entry {
    asic_evt_handler handler;
    void *data;
} asic_evt_handler_entry_t;

asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code,
                                              asic_evt_handler handler,
                                              void *data);
void asic_evt_remove_handler(uint16_t code);
void asic_evt_disable(uint16_t code, uint8_t irqlevel);
void asic_evt_enable(uint16_t code, uint8_t irqlevel);

#endif
