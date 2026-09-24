#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t irq_mask_t;

irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t state);
bool irq_inside_int(void);

static inline void g2_test_irq_cleanup(irq_mask_t *state) {
    irq_restore(*state);
}

#define __g2_test_irq_scoped(line) \
    irq_mask_t g2_test_irq_state_##line \
        __attribute__((cleanup(g2_test_irq_cleanup))) = irq_disable()
#define _g2_test_irq_scoped(line) __g2_test_irq_scoped(line)
#define irq_disable_scoped() _g2_test_irq_scoped(__LINE__)

#endif
