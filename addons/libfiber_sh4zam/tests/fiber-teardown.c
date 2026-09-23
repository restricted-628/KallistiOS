/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Test-only linker wrappers: no allocation instrumentation in the kernel.
*/
#include <kos.h>
#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error This probe requires assertions, including its checked API calls.
#endif

#define REPEATS 8u
#define STACK_SIZE 8192u
#define MAX_TRACKED 16u

static alignas(32) unsigned char stacks[3][STACK_SIZE];
static void *tracked[MAX_TRACKED];
static unsigned allocations, frees, borrowed_frees;
static bool tracing;
static kthread_t *trace_owner;
static kfiber_t *main_fiber;
static unsigned suspended, finished;

typedef struct test_case {
    unsigned flags;
    bool exit_from_child;
} test_case_t;

void *__real_calloc(size_t count, size_t size);
void *__real_aligned_alloc(size_t alignment, size_t size);
void __real_free(void *ptr);

static void record_allocation(void *ptr) {
    irq_mask_t saved = irq_disable();
    if(ptr && tracing && thd_get_current() == trace_owner) {
        assert(allocations < MAX_TRACKED);
        tracked[allocations++] = ptr;
    }
    irq_restore(saved);
}

void *__wrap_calloc(size_t count, size_t size) {
    void *ptr = __real_calloc(count, size);
    record_allocation(ptr);
    return ptr;
}

void *__wrap_aligned_alloc(size_t alignment, size_t size) {
    void *ptr = __real_aligned_alloc(alignment, size);
    record_allocation(ptr);
    return ptr;
}

void __wrap_free(void *ptr) {
    irq_mask_t saved = irq_disable();
    if(ptr) {
        for(unsigned i = 0; i < 3; ++i) {
            if(ptr == stacks[i]) {
                ++borrowed_frees;
                irq_restore(saved);
                return;
            }
        }
        for(unsigned i = 0; i < allocations; ++i) {
            if(ptr == tracked[i]) {
                tracked[i] = NULL;
                ++frees;
                break;
            }
        }
    }
    irq_restore(saved);
    __real_free(ptr);
}

static void never_started(void *data) {
    (void)data;
    assert(!"An undispatched fiber must not run during teardown");
}

static void suspend_child(void *data) {
    (void)data;
    ++suspended;
    assert(fiber_switch(main_fiber) == 0);
    assert(!"Thread teardown must not resume suspended application code");
}

static void finish_or_exit_child(void *data) {
    test_case_t *test = data;
    ++finished;
    if(test->exit_from_child)
        thd_exit(test);
}

static void *owner(void *data) {
    test_case_t *test = data;
    kfiber_t *children[3];

    trace_owner = thd_get_current();
    tracing = true;
    main_fiber = fiber_attach_ex(test->flags);
    assert(main_fiber);
    children[0] = fiber_create(stacks[0], STACK_SIZE, never_started, NULL);
    children[1] = fiber_create(stacks[1], STACK_SIZE, suspend_child, NULL);
    children[2] = fiber_create(stacks[2], STACK_SIZE, finish_or_exit_child, test);
    tracing = false;
    assert(children[0] && children[1] && children[2]);
    /* Runtime + three children, plus four optional XMTRX buffers. */
    assert(allocations == (test->flags ? 8u : 4u));
    assert(frees == 0);
    assert(fiber_switch(children[1]) == 0);
    assert(fiber_get_state(children[0]) == KFIBER_STATE_READY);
    assert(fiber_get_state(children[1]) == KFIBER_STATE_READY);
    assert(suspended == 1);
    assert(fiber_switch(children[2]) == 0);
    assert(!test->exit_from_child);
    assert(fiber_get_state(children[2]) == KFIBER_STATE_FINISHED);
    /* Intentionally leave core contexts for the owner's TLS destructor.
       There are no live event/mutex objects or held locks at owner exit. */
    return test;
}

int main(void) {
    unsigned cases = 0, reclaimed = 0;

    /* Initialize the process-wide fiber TLS key before scoped accounting. */
    assert(fiber_attach());
    for(unsigned repeat = 0; repeat < REPEATS; ++repeat) {
        for(unsigned math = 0; math < 2; ++math) {
            for(unsigned child_exit = 0; child_exit < 2; ++child_exit) {
                test_case_t test = {
                    math ? KFIBER_ATTACH_MATH_CONTEXT : KFIBER_ATTACH_DEFAULT,
                    child_exit != 0
                };
                void *result = NULL;
                kthread_t *thread;

                allocations = frees = borrowed_frees = suspended = finished = 0;
                memset(tracked, 0, sizeof(tracked));
                memset(stacks, 0xa5, sizeof(stacks));
                thread = thd_create(false, owner, &test);
                assert(thread);
                assert(thd_join(thread, &result) == 0);
                assert(result == &test && suspended == 1 && finished == 1);
                assert(allocations == (math ? 8u : 4u));
                assert(frees == allocations && borrowed_frees == 0);
                for(unsigned i = 0; i < allocations; ++i)
                    assert(!tracked[i]);
                reclaimed += frees;
                ++cases;
                /* The joined owner no longer references these borrowed stacks. */
                memset(stacks, 0x5a, sizeof(stacks));
            }
        }
    }
    printf("KOSFIBERTEARDOWN cases=%u reclaimed=%u borrowed-frees=0\n",
           cases, reclaimed);
    return 0;
}
