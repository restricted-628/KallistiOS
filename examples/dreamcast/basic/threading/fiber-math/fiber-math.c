/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Core-only XMTRX preservation; no service-executor dependency.
*/
#include <kos.h>
#include <dc/matrix.h>
#include <assert.h>
#include <stdio.h>
#include <stdalign.h>

static alignas(32) unsigned char stacks[2][8192];
static kfiber_t *main_fiber;
static unsigned stages[2];
static matrix_t expected[3];

static void expect_matrix(const matrix_t *wanted) {
    matrix_t actual;
    mat_store(&actual);
    for(unsigned row = 0; row < 4; ++row)
        for(unsigned col = 0; col < 4; ++col)
            assert(actual[row][col] == (*wanted)[row][col]);
}

static void child(void *data) {
    unsigned index = *(unsigned *)data;
    matrix_t identity = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}
    };
    expect_matrix(&identity);
    mat_load(&expected[index + 1]);
    stages[index] = 1;
    assert(fiber_switch(main_fiber) == 0);
    expect_matrix(&expected[index + 1]);
    /* Kernel preemption and cooperative switching are separate boundaries. */
    thd_pass();
    expect_matrix(&expected[index + 1]);
    stages[index] = 2;
}

int main(void) {
    kfiber_t *fibers[2];
    unsigned indices[2] = {0, 1};
    for(unsigned matrix = 0; matrix < 3; ++matrix)
        for(unsigned row = 0; row < 4; ++row)
            for(unsigned col = 0; col < 4; ++col)
                expected[matrix][row][col] = (float)(100 * matrix + 4 * row + col);

    mat_load(&expected[0]);
    main_fiber = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    assert(main_fiber);
    assert(fiber_get_attach_flags() == KFIBER_ATTACH_MATH_CONTEXT);
    for(unsigned i = 0; i < 2; ++i) {
        fibers[i] = fiber_create(stacks[i], sizeof(stacks[i]), child, &indices[i]);
        assert(fibers[i]);
    }
    for(unsigned round = 1; round <= 2; ++round) {
        for(unsigned i = 0; i < 2; ++i) {
            assert(fiber_switch(fibers[i]) == 0);
            assert(stages[i] == round);
            expect_matrix(&expected[0]);
        }
    }
    for(unsigned i = 0; i < 2; ++i) {
        assert(fiber_get_state(fibers[i]) == KFIBER_STATE_FINISHED);
        assert(fiber_destroy(fibers[i]) == 0);
    }
    puts("KOSFIBERMATH children=2 rounds=2 xmtrx=16");
    return 0;
}
