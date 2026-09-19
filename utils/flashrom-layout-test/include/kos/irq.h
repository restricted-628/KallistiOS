#ifndef FLASHROM_LAYOUT_TEST_IRQ_H
#define FLASHROM_LAYOUT_TEST_IRQ_H

#include <stdbool.h>

int irq_disable(void);
void irq_restore(int old);
bool irq_inside_int(void);

#endif
