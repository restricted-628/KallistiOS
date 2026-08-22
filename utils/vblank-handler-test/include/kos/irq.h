#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H

#include <stdbool.h>

typedef int irq_mask_t;

extern bool vblank_test_irq_context;

static inline bool irq_inside_int(void) {
    return vblank_test_irq_context;
}

static inline irq_mask_t irq_disable(void) {
    return 0;
}

static inline void irq_restore(irq_mask_t old) {
    (void)old;
}

#endif
