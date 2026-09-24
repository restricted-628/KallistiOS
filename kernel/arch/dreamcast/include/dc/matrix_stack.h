/* KallistiOS ##version##

   dc/matrix_stack.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/matrix_stack.h
    \brief   Caller-owned stack for the SH-4 matrix register.
    \ingroup math_matrices

    This API adds bounded hierarchy traversal to the established KOS matrix
    routines without allocating memory or creating global stack state.
*/

#ifndef __DC_MATRIX_STACK_H
#define __DC_MATRIX_STACK_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/matrix.h>

#include <stddef.h>

/** \addtogroup math_matrices
    @{
*/

/** \brief Caller-owned matrix stack descriptor.

    The storage remains owned by the caller. After initialization, applications
    may inspect the fields but must not modify them directly.

    One stack must not be mutated concurrently by multiple execution contexts.
    KOS preserves the SH-4 matrix register across thread switches. Cooperative
    fibers share their carrier thread's matrix register, so a fiber that may be
    resumed after another fiber used matrix operations should call
    mat_stack_restore() at the appropriate saved level.
*/
typedef struct mat_stack {
    matrix_t *storage; /**< Caller-owned saved matrices. */
    size_t capacity;   /**< Number of matrices available in storage. */
    size_t depth;      /**< Number of matrices currently saved. */
} mat_stack_t;

/** \brief Initialize a matrix stack over caller-owned storage.

    Initialization does not alter the current SH-4 matrix register. The
    storage must satisfy matrix_t's alignment requirement and remain alive
    until the stack is no longer used.

    \param stack       Stack descriptor to initialize.
    \param storage     Caller-owned matrix array.
    \param capacity    Number of entries in \p storage; must be nonzero.

    \retval 0  Success.
    \retval -1 Error, with `errno` set.
*/
int mat_stack_init(mat_stack_t *stack, matrix_t *storage, size_t capacity);

/** \brief Discard every saved level without changing the current matrix.

    \retval 0  Success.
    \retval -1 Invalid stack, with `errno` set to `EINVAL`.
*/
int mat_stack_clear(mat_stack_t *stack);

/** \brief Save the current SH-4 matrix as a new stack level.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL` or `ENOSPC`.
*/
int mat_stack_push(mat_stack_t *stack);

/** \brief Restore and remove the most recently saved matrix.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL` or `ERANGE`.
*/
int mat_stack_pop(mat_stack_t *stack);

/** \brief Restore the most recently saved matrix without removing it.

    This is useful after a callback or cooperative execution transfer that may
    have used the shared matrix register.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL` or `ERANGE`.
*/
int mat_stack_restore(const mat_stack_t *stack);

/** @} */

__END_DECLS
#endif /* __DC_MATRIX_STACK_H */
