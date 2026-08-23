/* KallistiOS ##version##

   Host-side caller-owned matrix stack tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix_stack.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static alignas(32) matrix_t current_matrix;

void mat_store(matrix_t *out) {
    memcpy(out, &current_matrix, sizeof(*out));
}

void mat_load(const matrix_t *src) {
    memcpy(&current_matrix, src, sizeof(current_matrix));
}

static void fill_current(float base) {
    size_t row;
    size_t column;

    for(row = 0; row < 4; ++row) {
        for(column = 0; column < 4; ++column)
            current_matrix[row][column] = base + (float)(row * 4 + column);
    }
}

static void expect_current(float base) {
    size_t row;
    size_t column;

    for(row = 0; row < 4; ++row) {
        for(column = 0; column < 4; ++column) {
            assert(current_matrix[row][column] ==
                   base + (float)(row * 4 + column));
        }
    }
}

static void test_validation(void) {
    alignas(32) matrix_t storage[2];
    alignas(8) unsigned char unaligned[sizeof(matrix_t) + 1u];
    mat_stack_t stack = { 0 };

    errno = 0;
    assert(mat_stack_init(NULL, storage, 2) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_stack_init(&stack, NULL, 2) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_stack_init(&stack, storage, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(mat_stack_init(&stack,
                          (matrix_t *)(void *)(unaligned + 1), 1) == -1 &&
                          errno == EINVAL);
    errno = 0;
    assert(mat_stack_init(&stack, storage, SIZE_MAX) == -1 && errno == EINVAL);

    assert(mat_stack_init(&stack, storage, 2) == 0);
    stack.depth = 3;
    errno = 0;
    assert(mat_stack_push(&stack) == -1 && errno == EINVAL);
}

static void test_hierarchy(void) {
    alignas(32) matrix_t storage[2];
    mat_stack_t stack;

    assert(mat_stack_init(&stack, storage, 2) == 0);
    assert(stack.capacity == 2 && stack.depth == 0);

    fill_current(10.0f);
    assert(mat_stack_push(&stack) == 0 && stack.depth == 1);

    fill_current(30.0f);
    assert(mat_stack_push(&stack) == 0 && stack.depth == 2);

    fill_current(50.0f);
    errno = 0;
    assert(mat_stack_push(&stack) == -1 && errno == ENOSPC);
    assert(stack.depth == 2);
    expect_current(50.0f);

    assert(mat_stack_restore(&stack) == 0);
    assert(stack.depth == 2);
    expect_current(30.0f);

    fill_current(60.0f);
    assert(mat_stack_pop(&stack) == 0 && stack.depth == 1);
    expect_current(30.0f);

    assert(mat_stack_pop(&stack) == 0 && stack.depth == 0);
    expect_current(10.0f);

    errno = 0;
    assert(mat_stack_pop(&stack) == -1 && errno == ERANGE);
    errno = 0;
    assert(mat_stack_restore(&stack) == -1 && errno == ERANGE);
}

static void test_clear(void) {
    alignas(32) matrix_t storage[2];
    mat_stack_t stack;

    assert(mat_stack_init(&stack, storage, 2) == 0);
    fill_current(100.0f);
    assert(mat_stack_push(&stack) == 0);
    fill_current(200.0f);
    assert(mat_stack_clear(&stack) == 0 && stack.depth == 0);
    expect_current(200.0f);
}

int main(void) {
    test_validation();
    test_hierarchy();
    test_clear();
    puts("matrix stack tests passed");
    return 0;
}
