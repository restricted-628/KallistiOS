#ifndef __DC_ASIC_H
#define __DC_ASIC_H

#include <stdint.h>

#define ASIC_EVT_G2_DMA0 UINT16_C(0x000f)
#define ASIC_EVT_G2_DMA1 UINT16_C(0x0010)
#define ASIC_EVT_G2_DMA2 UINT16_C(0x0011)
#define ASIC_EVT_G2_DMA3 UINT16_C(0x0012)
#define ASIC_IRQB UINT8_C(1)

typedef void (*asic_evt_handler)(uint32_t code, void *data);
typedef uint32_t asic_evt_claim_t;

#define ASIC_EVT_CLAIM_INVALID UINT32_C(0)

int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim);
int asic_evt_release(asic_evt_claim_t claim);

#endif
