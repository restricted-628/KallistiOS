#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t irq_mask_t;
extern bool test_in_irq;
extern irq_mask_t test_mask;
static inline bool irq_inside_int(void) { return test_in_irq; }
static inline irq_mask_t irq_disable(void) {
    irq_mask_t old = test_mask;
    test_mask |= 0xf0;
    return old;
}
static inline void irq_restore(irq_mask_t old) { test_mask = old; }
#endif
