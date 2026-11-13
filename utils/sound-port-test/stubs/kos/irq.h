#ifndef TEST_IRQ_H
#define TEST_IRQ_H
#include <assert.h>
static int irq_level;
static inline int irq_disable(void) { int old = irq_level; irq_level = 1; return old; }
static inline void irq_restore(int old) { assert(irq_level == 1); irq_level = old; }
#endif
