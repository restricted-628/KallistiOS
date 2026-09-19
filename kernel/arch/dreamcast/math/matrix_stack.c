/* KallistiOS ##version##

   matrix_stack.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix_stack.h>

#include <errno.h>
#include <stdint.h>

static int mat_stack_valid(const mat_stack_t *stack) {
    return stack && stack->storage && stack->capacity &&
           stack->capacity <= SIZE_MAX / sizeof(matrix_t) &&
           stack->depth <= stack->capacity &&
           !((uintptr_t)stack->storage & (_Alignof(matrix_t) - 1u));
}

int mat_stack_init(mat_stack_t *stack, matrix_t *storage, size_t capacity) {
    if(!stack || !storage || !capacity ||
       capacity > SIZE_MAX / sizeof(matrix_t) ||
       ((uintptr_t)storage & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }

    stack->storage = storage;
    stack->capacity = capacity;
    stack->depth = 0;
    return 0;
}

int mat_stack_clear(mat_stack_t *stack) {
    if(!mat_stack_valid(stack)) {
        errno = EINVAL;
        return -1;
    }

    stack->depth = 0;
    return 0;
}

int mat_stack_push(mat_stack_t *stack) {
    if(!mat_stack_valid(stack)) {
        errno = EINVAL;
        return -1;
    }

    if(stack->depth == stack->capacity) {
        errno = ENOSPC;
        return -1;
    }

    mat_store(&stack->storage[stack->depth]);
    ++stack->depth;
    return 0;
}

int mat_stack_pop(mat_stack_t *stack) {
    if(!mat_stack_valid(stack)) {
        errno = EINVAL;
        return -1;
    }

    if(!stack->depth) {
        errno = ERANGE;
        return -1;
    }

    --stack->depth;
    mat_load(&stack->storage[stack->depth]);
    return 0;
}

int mat_stack_restore(const mat_stack_t *stack) {
    if(!mat_stack_valid(stack)) {
        errno = EINVAL;
        return -1;
    }

    if(!stack->depth) {
        errno = ERANGE;
        return -1;
    }

    mat_load(&stack->storage[stack->depth - 1u]);
    return 0;
}
