/* KallistiOS ##version##

   fiber-sync.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define FIBER_STACK_SIZE 8192u

static alignas(32) uint8_t stack_a[FIBER_STACK_SIZE];
static alignas(32) uint8_t stack_b[FIBER_STACK_SIZE];
static alignas(32) uint8_t stack_c[FIBER_STACK_SIZE];
static kfiber_t *main_fiber;
static kfiber_t *fiber_a;
static kfiber_t *fiber_b;
static kfiber_t *fiber_c;
static kfiber_event_t *event;
static kfiber_mutex_t *mutex;
static unsigned sequence;
static bool failed;

static void entry_a(void *data) {
    (void)data;

    if(fiber_mutex_lock(mutex) < 0)
        failed = true;

    errno = 0;
    if(fiber_mutex_lock(mutex) == 0 || errno != EDEADLK)
        failed = true;

    sequence = 1;
    if(fiber_switch(fiber_b) < 0 || sequence != 3)
        failed = true;

    if(fiber_mutex_unlock(mutex) < 0)
        failed = true;
    sequence = 4;
}

static void entry_b(void *data) {
    (void)data;

    if(sequence != 1)
        failed = true;
    sequence = 2;

    if(fiber_mutex_lock(mutex) < 0 || sequence != 4)
        failed = true;
    sequence = 5;
    if(fiber_mutex_unlock(mutex) < 0)
        failed = true;

    if(fiber_event_wait(event) < 0 || sequence != 7)
        failed = true;
    sequence = 8;
}

static void entry_c(void *data) {
    (void)data;

    if(sequence != 2)
        failed = true;
    sequence = 3;

    if(fiber_mutex_lock(mutex) < 0 || sequence != 5)
        failed = true;
    sequence = 6;
    if(fiber_mutex_unlock(mutex) < 0)
        failed = true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    main_fiber = fiber_attach();
    event = fiber_event_create(false);
    mutex = fiber_mutex_create();
    fiber_a = fiber_create(stack_a, sizeof(stack_a), entry_a, NULL);
    fiber_b = fiber_create(stack_b, sizeof(stack_b), entry_b, NULL);
    fiber_c = fiber_create(stack_c, sizeof(stack_c), entry_c, NULL);
    if(!main_fiber || !event || !mutex || !fiber_a || !fiber_b || !fiber_c) {
        printf("Unable to create fiber synchronization probe\n");
        return EXIT_FAILURE;
    }

    if(fiber_switch(fiber_a) < 0 || sequence != 2 ||
       fiber_get_state(fiber_a) != KFIBER_STATE_READY ||
       fiber_get_state(fiber_b) != KFIBER_STATE_WAITING)
        failed = true;

    if(fiber_switch(fiber_c) < 0 || sequence != 3 ||
       fiber_get_state(fiber_c) != KFIBER_STATE_WAITING)
        failed = true;

    errno = 0;
    if(fiber_mutex_trylock(mutex) == 0 || errno != EBUSY)
        failed = true;
    errno = 0;
    if(fiber_mutex_lock(mutex) == 0 || errno != EDEADLK)
        failed = true;
    errno = 0;
    if(fiber_destroy(fiber_a) == 0 || errno != EBUSY)
        failed = true;
    errno = 0;
    if(fiber_mutex_destroy(mutex) == 0 || errno != EBUSY)
        failed = true;

    if(fiber_switch(fiber_a) < 0 || sequence != 4 ||
       fiber_get_state(fiber_a) != KFIBER_STATE_FINISHED ||
       fiber_get_state(fiber_b) != KFIBER_STATE_READY ||
       fiber_get_state(fiber_c) != KFIBER_STATE_WAITING)
        failed = true;

    if(fiber_switch(fiber_b) < 0 || sequence != 5 ||
       fiber_get_state(fiber_b) != KFIBER_STATE_WAITING ||
       fiber_get_state(fiber_c) != KFIBER_STATE_READY)
        failed = true;

    if(fiber_switch(fiber_c) < 0 || sequence != 6 ||
       fiber_get_state(fiber_c) != KFIBER_STATE_FINISHED)
        failed = true;

    errno = 0;
    if(fiber_event_destroy(event) == 0 || errno != EBUSY)
        failed = true;

    sequence = 7;
    if(fiber_event_set(event) < 0 || !fiber_event_is_set(event) ||
       fiber_get_state(fiber_b) != KFIBER_STATE_READY ||
       fiber_event_clear(event) < 0 || fiber_event_is_set(event))
        failed = true;

    if(fiber_switch(fiber_b) < 0 || sequence != 8 ||
       fiber_get_state(fiber_b) != KFIBER_STATE_FINISHED)
        failed = true;

    if(fiber_destroy(fiber_a) < 0 || fiber_destroy(fiber_b) < 0 ||
       fiber_destroy(fiber_c) < 0 ||
       fiber_mutex_destroy(mutex) < 0 || fiber_event_destroy(event) < 0)
        failed = true;

    if(failed) {
        printf("Fiber synchronization failed at sequence %u\n", sequence);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERSYNC sequence=%u fifo=2\n", sequence);
    return EXIT_SUCCESS;
}
