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
#include <string.h>

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

#define CHECK_SYNC(condition) do { \
    if(!(condition)) { \
        printf("Fiber sync regression failed at line %d\n", __LINE__); \
        return false; \
    } \
} while(0)

static unsigned foreign_checks;
static unsigned resumed_waiters;

static void *foreign_waits(void *data) {
    (void)data;

    /* Check both unattached and independently attached owner threads. */
    for(unsigned attached = 0; attached < 2; ++attached) {
        if(attached && !fiber_attach()) {
            failed = true;
            return NULL;
        }
        for(unsigned signaled = 0; signaled < 2; ++signaled) {
            if((signaled ? fiber_event_set(event) : fiber_event_clear(event)) < 0) {
                failed = true;
                return NULL;
            }
            errno = 0;
            if(fiber_event_wait(event) != -1 || errno != EXDEV) {
                printf("Foreign event wait failed: attached=%u signaled=%u errno=%d\n",
                       attached, signaled, errno);
                failed = true;
            }
            ++foreign_checks;
        }
    }
    return NULL;
}

static void parked_event(void *data) {
    if(fiber_event_wait(event) < 0 || !data)
        failed = true;
    ++resumed_waiters;
}

static void parked_mutex(void *data) {
    if(fiber_mutex_lock(mutex) < 0 || !data)
        failed = true;
    ++resumed_waiters;
    if(fiber_mutex_unlock(mutex) < 0)
        failed = true;
}

static bool check_lifetimes(void) {
    kthread_t *other;

    event = fiber_event_create(false);
    CHECK_SYNC(event);
    other = thd_create(false, foreign_waits, NULL);
    CHECK_SYNC(other);
    CHECK_SYNC(thd_join(other, NULL) == 0);
    CHECK_SYNC(!failed && foreign_checks == 4);

    /* Main may observe a set event, but must never park on an unset one. */
    CHECK_SYNC(fiber_event_wait(event) == 0);
    CHECK_SYNC(fiber_event_clear(event) == 0);
    errno = 0;
    CHECK_SYNC(fiber_event_wait(event) == -1 && errno == EDEADLK);
    fiber_a = fiber_create(stack_a, sizeof(stack_a), parked_event, NULL);
    fiber_b = fiber_create(stack_b, sizeof(stack_b), parked_event, &resumed_waiters);
    CHECK_SYNC(fiber_a && fiber_b);
    CHECK_SYNC(fiber_switch(fiber_a) == 0 && fiber_switch(fiber_b) == 0);
    CHECK_SYNC(fiber_get_state(fiber_a) == KFIBER_STATE_WAITING &&
               fiber_get_state(fiber_b) == KFIBER_STATE_WAITING);
    CHECK_SYNC(fiber_destroy(fiber_a) == 0);
    /* Reuse the cancelled stack immediately: no queue link may remain in it. */
    memset(stack_a, 0xa5, sizeof(stack_a));
    CHECK_SYNC(fiber_event_set(event) == 0);
    CHECK_SYNC(fiber_get_state(fiber_b) == KFIBER_STATE_READY);
    CHECK_SYNC(fiber_switch(fiber_b) == 0 && resumed_waiters == 1 && !failed);
    CHECK_SYNC(fiber_destroy(fiber_b) == 0 && fiber_event_destroy(event) == 0);

    mutex = fiber_mutex_create();
    CHECK_SYNC(mutex && fiber_mutex_lock(mutex) == 0);
    fiber_a = fiber_create(stack_a, sizeof(stack_a), parked_mutex, NULL);
    fiber_b = fiber_create(stack_b, sizeof(stack_b), parked_mutex, &resumed_waiters);
    CHECK_SYNC(fiber_a && fiber_b);
    CHECK_SYNC(fiber_switch(fiber_a) == 0 && fiber_switch(fiber_b) == 0);
    CHECK_SYNC(fiber_get_state(fiber_a) == KFIBER_STATE_WAITING &&
               fiber_get_state(fiber_b) == KFIBER_STATE_WAITING);
    CHECK_SYNC(fiber_destroy(fiber_a) == 0);
    memset(stack_a, 0x5a, sizeof(stack_a));
    CHECK_SYNC(fiber_mutex_unlock(mutex) == 0);
    CHECK_SYNC(fiber_get_state(fiber_b) == KFIBER_STATE_READY);
    /* Handoff already owns the mutex, even before the waiter runs again. */
    errno = 0;
    CHECK_SYNC(fiber_destroy(fiber_b) == -1 && errno == EBUSY);
    CHECK_SYNC(fiber_switch(fiber_b) == 0 && resumed_waiters == 2 && !failed);
    CHECK_SYNC(fiber_destroy(fiber_b) == 0 && fiber_mutex_destroy(mutex) == 0);
    printf("KOSFIBERSYNC lifetime foreign=%u cancelled=2 resumed=%u\n",
           foreign_checks, resumed_waiters);
    return true;
}

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

    if(failed || !check_lifetimes()) {
        printf("Fiber synchronization failed at sequence %u\n", sequence);
        return EXIT_FAILURE;
    }

    printf("KOSFIBERSYNC sequence=%u fifo=2\n", sequence);
    return EXIT_SUCCESS;
}
