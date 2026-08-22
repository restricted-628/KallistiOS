/* KallistiOS ##version##

   arch/dreamcast/kernel/fiber_context.c
   Copyright (C) 2026 Joseph Black
*/

#include <arch/fiber.h>

#include <stddef.h>
#include <string.h>

/* Keep these checks beside the assembly implementation. A layout change that
   is not mirrored in fiber_switch.s must fail the build, not corrupt a stack. */
_Static_assert(offsetof(arch_fiber_context_t, r8) == 0);
_Static_assert(offsetof(arch_fiber_context_t, r15) == 28);
_Static_assert(offsetof(arch_fiber_context_t, fr12) == 32);
_Static_assert(offsetof(arch_fiber_context_t, fr15) == 44);
_Static_assert(offsetof(arch_fiber_context_t, pr) == 48);
_Static_assert(offsetof(arch_fiber_context_t, sr) == 52);
_Static_assert(sizeof(arch_fiber_context_t) == 56);

void arch_fiber_context_init(arch_fiber_context_t *context,
                             uintptr_t stack_top, uintptr_t entry,
                             irq_mask_t sr) {
    memset(context, 0, sizeof(*context));
    context->r15 = stack_top;
    context->pr = entry;
    context->sr = sr;
}
