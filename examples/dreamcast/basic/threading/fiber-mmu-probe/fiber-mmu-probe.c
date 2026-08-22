/* KallistiOS ##version##

   fiber-mmu-probe.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber.h>

#include <arch/mmu.h>
#include <dc/sq.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIBER_STACK_SIZE 8192u

static alignas(32) uint8_t fiber_stack[FIBER_STACK_SIZE];
static alignas(32) uint8_t sq_source[64];
static alignas(32) uint8_t sq_destination[64];
static kfiber_t *main_fiber;
static kfiber_t *test_fiber;
static mmucontext_t *expected_context;
static volatile unsigned sequence;
static volatile bool failed;

static void fiber_entry(void *data) {
    uint32_t *sq;

    if(data != fiber_stack || !mmu_enabled() ||
       mmu_cxt_current != expected_context)
        failed = true;

    sq = sq_lock(sq_destination);
    errno = 0;
    if(!sq || fiber_switch(main_fiber) == 0 || errno != EBUSY)
        failed = true;
    sq_unlock();

    sq_cpy(sq_destination, sq_source, sizeof(sq_source));
    if(memcmp(sq_destination, sq_source, sizeof(sq_source)))
        failed = true;

    sequence = 1;
    if(fiber_switch(main_fiber) < 0)
        failed = true;

    if(!mmu_enabled() || mmu_cxt_current != expected_context)
        failed = true;
    sequence = 2;
}

int main(int argc, char **argv) {
    bool initialized_mmu = false;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    if(!mmu_enabled()) {
        mmu_init();
        initialized_mmu = true;
    }

    if(!mmu_enabled()) {
        printf("Unable to enable MMU support\n");
        goto out;
    }

    expected_context = mmu_cxt_current;
    for(size_t i = 0; i < sizeof(sq_source); ++i)
        sq_source[i] = (uint8_t)(i ^ 0xa5u);

    main_fiber = fiber_attach();
    test_fiber = fiber_create(fiber_stack, sizeof(fiber_stack),
                              fiber_entry, fiber_stack);
    if(!main_fiber || !test_fiber) {
        printf("Unable to create MMU fiber probe: %s\n", strerror(errno));
        goto out;
    }

    if(fiber_switch(test_fiber) < 0 || sequence != 1 ||
       !mmu_enabled() || mmu_cxt_current != expected_context)
        failed = true;

    if(fiber_switch(test_fiber) < 0 || sequence != 2 ||
       fiber_get_state(test_fiber) != KFIBER_STATE_FINISHED)
        failed = true;

    if(fiber_destroy(test_fiber) < 0)
        failed = true;
    test_fiber = NULL;

    if(failed) {
        printf("Fiber MMU probe failed at sequence %u\n", sequence);
        goto out;
    }

    printf("KOSFIBERMMU sequence=%u sq=1\n", sequence);
    result = EXIT_SUCCESS;

out:
    if(test_fiber && fiber_get_state(test_fiber) != KFIBER_STATE_RUNNING)
        fiber_destroy(test_fiber);
    if(initialized_mmu)
        mmu_shutdown();
    return result;
}
