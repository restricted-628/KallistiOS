/* KallistiOS ##version##

   arch/dreamcast/kernel/fiber_context.c
   Copyright (C) 2026 Joseph Black
*/

#include "fiber_arch.h"

#include <sh4zam/shz_xmtrx.h>

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
_Static_assert(sizeof(arch_fiber_math_context_t) == sizeof(shz_mat4x4_t));
_Static_assert(_Alignof(arch_fiber_math_context_t) >= _Alignof(shz_mat4x4_t));

void arch_fiber_context_init(arch_fiber_context_t *context,
                             uintptr_t stack_top, uintptr_t entry,
                             irq_mask_t sr) {
    memset(context, 0, sizeof(*context));
    context->r15 = stack_top;
    context->pr = entry;
    context->sr = sr;
}

void arch_fiber_math_context_capture(arch_fiber_math_context_t *context) {
    shz_xmtrx_store_4x4(&context->matrix);
}

void arch_fiber_math_context_init(arch_fiber_math_context_t *context) {
    /* Initialize backing memory without changing the creator's live XMTRX.
       shz_mat4x4_init_identity() uses XMTRX as scratch in SH4ZAM 0.9.0. */
    context->matrix = (shz_mat4x4_t) { .elem = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
    } };
}

void arch_fiber_math_context_switch(arch_fiber_math_context_t *from,
                                    const arch_fiber_math_context_t *to) {
    shz_xmtrx_store_4x4(&from->matrix);
    shz_xmtrx_load_4x4(&to->matrix);
}
