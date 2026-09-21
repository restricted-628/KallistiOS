#ifndef TEST_IRQ_H
#define TEST_IRQ_H
#include <assert.h>
typedef unsigned irq_mask_t;
extern unsigned test_irq_depth;
static inline irq_mask_t irq_disable(void) { return test_irq_depth++; }
static inline void irq_restore(irq_mask_t mask) {
    assert(test_irq_depth == mask + 1);
    test_irq_depth = mask;
}
static inline int irq_inside_int(void) { return 0; }
#endif
