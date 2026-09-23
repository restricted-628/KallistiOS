#ifndef G1_TEST_ASIC_H
#define G1_TEST_ASIC_H

#include <stdint.h>

#define ASIC_IRQB 1

#define ASIC_EVT_GD_COMMAND     0x0100u
#define ASIC_EVT_GD_DMA         0x000eu
#define ASIC_EVT_GD_DMA_OVERRUN 0x020du
#define ASIC_EVT_GD_DMA_ILLADDR 0x020cu
#define ASIC_EVT_GD_DMA_ACCESS  0x020eu

typedef void (*asic_evt_handler)(uint32_t code, void *data);
typedef uint32_t asic_evt_claim_t;

#define ASIC_EVT_CLAIM_INVALID 0u

int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim);
int asic_evt_claim_mask(asic_evt_claim_t claim);
int asic_evt_claim_unmask(asic_evt_claim_t claim);
int asic_evt_release(asic_evt_claim_t claim);

#endif /* G1_TEST_ASIC_H */
