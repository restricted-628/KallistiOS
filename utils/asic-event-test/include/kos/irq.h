#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H

#include <stdint.h>

typedef int irq_t;
typedef struct irq_context irq_context_t;
typedef uint32_t irq_mask_t;
typedef void (*irq_hdl_t)(irq_t code, irq_context_t *context, void *data);

enum {
    EXC_IRQ9 = 9,
    EXC_IRQB = 11,
    EXC_IRQD = 13
};

irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t state);
void irq_set_handler(irq_t code, irq_hdl_t handler, void *data);

#endif
