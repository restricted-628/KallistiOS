#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H

#include <stdbool.h>

extern _Thread_local bool mm_heap_test_irq_context;

static inline bool irq_inside_int(void) {
    return mm_heap_test_irq_context;
}

#endif
