/* KallistiOS ##version##

   Caller-owned matrix stack example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static int close_enough(float actual, float expected) {
    float difference = actual - expected;

    if(difference < 0.0f)
        difference = -difference;

    return difference < 0.0001f;
}

static void check_translation(float x, float y, float z) {
    alignas(32) matrix_t matrix;

    mat_store(&matrix);
    assert(close_enough(matrix[3][0], x));
    assert(close_enough(matrix[3][1], y));
    assert(close_enough(matrix[3][2], z));
    assert(close_enough(matrix[3][3], 1.0f));
}

int main(int argc, char **argv) {
    alignas(32) matrix_t storage[4];
    mat_stack_t stack;

    (void)argc;
    (void)argv;

    assert(mat_stack_init(&stack, storage, 4) == 0);
    mat_identity();

    /* Preserve the root before applying the parent transform. */
    assert(mat_stack_push(&stack) == 0);
    mat_translate(10.0f, 20.0f, 30.0f);
    check_translation(10.0f, 20.0f, 30.0f);

    /* Preserve the parent while visiting a child node. */
    assert(mat_stack_push(&stack) == 0);
    mat_translate(1.0f, 2.0f, 3.0f);
    check_translation(11.0f, 22.0f, 33.0f);

    assert(mat_stack_pop(&stack) == 0);
    check_translation(10.0f, 20.0f, 30.0f);

    mat_identity();
    assert(mat_stack_restore(&stack) == 0);
    check_translation(0.0f, 0.0f, 0.0f);
    assert(stack.depth == 1);

    assert(mat_stack_pop(&stack) == 0);
    errno = 0;
    assert(mat_stack_pop(&stack) == -1 && errno == ERANGE);

    while(stack.depth < stack.capacity)
        assert(mat_stack_push(&stack) == 0);

    errno = 0;
    assert(mat_stack_push(&stack) == -1 && errno == ENOSPC);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2,
                   vid_mode->width, 1,
                   "RESULT: PASS (caller-owned matrix stack)");
    puts("RESULT: PASS (caller-owned matrix stack)");

    for(;;)
        thd_sleep(1000);
}
