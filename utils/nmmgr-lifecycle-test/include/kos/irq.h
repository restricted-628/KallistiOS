#ifndef __KOS_IRQ_H
#define __KOS_IRQ_H

#include <stdbool.h>

static inline bool irq_inside_int(void) {
    return false;
}

#endif
