/* KallistiOS ##version##

   arch/dreamcast/include/arch/fiber.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    arch/fiber.h
    \brief   SH-4 cooperative execution-context support.
    \ingroup fibers
*/

#ifndef __ARCH_FIBER_H
#define __ARCH_FIBER_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <kos/irq.h>

#include <dc/vector.h>

#include <stdint.h>

/** \cond */

/* This is an ABI-call-boundary context, not a kernel interrupt context. The
   caller-saved registers are already permitted to change across the call. */
typedef struct __attribute__((aligned(8))) arch_fiber_context {
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t r13;
    uint32_t r14;
    uint32_t r15;
    uint32_t fr12;
    uint32_t fr13;
    uint32_t fr14;
    uint32_t fr15;
    uint32_t pr;
    uint32_t sr;
} arch_fiber_context_t;

/* Optional architecture-owned accelerator state. This remains separate from
   arch_fiber_context_t so lightweight fibers keep the original footprint. */
typedef struct __attribute__((aligned(32))) arch_fiber_math_context {
    matrix_t matrix;
} arch_fiber_math_context_t;

/* SR.BL and SR.IMASK describe CPU-wide exclusion state. A cooperative
   transfer must not replace that state while the caller is inside a critical
   section. */
static inline bool arch_fiber_irq_state_switchable(irq_mask_t sr) {
    return !(sr & 0x100000f0u);
}

void arch_fiber_context_init(arch_fiber_context_t *context,
                             uintptr_t stack_top, uintptr_t entry,
                             irq_mask_t sr);
void arch_fiber_context_switch(arch_fiber_context_t *from,
                               const arch_fiber_context_t *to);

void arch_fiber_math_context_capture(arch_fiber_math_context_t *context);
void arch_fiber_math_context_init(arch_fiber_math_context_t *context);
void arch_fiber_math_context_switch(arch_fiber_math_context_t *from,
                                    const arch_fiber_math_context_t *to);

/* Report whether architecture-owned, thread-scoped hardware state permits a
   cooperative continuation transfer. This is queried only by fiber paths. */
bool arch_fiber_cooperative_state_switchable(void);

/** \endcond */

__END_DECLS
#endif /* __ARCH_FIBER_H */
