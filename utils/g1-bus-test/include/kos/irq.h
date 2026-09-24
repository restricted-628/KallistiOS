#ifndef G1_TEST_IRQ_H
#define G1_TEST_IRQ_H

#include <stdbool.h>

typedef unsigned int irq_mask_t;

irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t state);
bool irq_inside_int(void);

#endif /* G1_TEST_IRQ_H */
