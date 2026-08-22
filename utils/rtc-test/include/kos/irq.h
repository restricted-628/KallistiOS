#ifndef __RTC_TEST_KOS_IRQ_H
#define __RTC_TEST_KOS_IRQ_H

typedef unsigned int irq_mask_t;

irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t state);

#endif
