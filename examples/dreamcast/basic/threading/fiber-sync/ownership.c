/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Event ownership must be checked before the already-signaled return.
   Expected to fail on a runtime that accepts foreign ready-event waits.
*/

#include <kos.h>
#include <dc/vblank.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>

static kfiber_event_t *events[2]; /* Unset, set; owned by the test thread. */
static alignas(32) unsigned char child_stack[8192];
static unsigned checks, failures;
static bool child_returned;
static int child_result, child_error;
static volatile bool irq_done;
static volatile int irq_result[2], irq_error[2];

#define CHECK(condition) do { \
    ++checks; \
    if(!(condition)) { \
        ++failures; \
        printf("FIBER-OWNER: failed line %d: %s\n", __LINE__, #condition); \
    } \
} while(0)

static void check_wait(const char *caller, bool set, int result, int error,
                       int expected_error) {
    printf("FIBER-OWNER: caller=%s set=%d result=%d errno=%d expected=%d\n",
           caller, set, result, error, expected_error);
    CHECK(expected_error ? result == -1 && error == expected_error : result == 0);
    CHECK(fiber_event_is_set(events[set]) == set);
}

static void child_wait(void *data) {
    errno = 0;
    child_result = fiber_event_wait(data);
    child_error = errno;
    child_returned = true;
}

typedef struct foreign_case {
    bool attach;
    bool setup_ok;
    bool identity_unchanged;
    int result[2], error[2];
} foreign_case_t;

static void *foreign_wait(void *data) {
    foreign_case_t *probe = data;
    kfiber_t *main = probe->attach ? fiber_attach() : fiber_main();

    probe->setup_ok = probe->attach ? main != NULL : main == NULL;
    if(!probe->setup_ok)
        return NULL;

    for(unsigned set = 0; set < 2; ++set) {
        errno = 0;
        probe->result[set] = fiber_event_wait(events[set]);
        probe->error[set] = errno;
    }
    probe->identity_unchanged = fiber_main() == main;
    return NULL;
}

static void irq_wait(uint32_t code, void *data) {
    int saved_errno = errno;
    (void)code;
    (void)data;

    if(irq_done)
        return;
    /* Record only: no printing, allocation or scheduling in the callback. */
    for(unsigned set = 0; set < 2; ++set) {
        errno = 0;
        irq_result[set] = fiber_event_wait(events[set]);
        irq_error[set] = errno;
    }
    errno = saved_errno;
    irq_done = true;
}

int fiber_sync_ownership_test(void) {
    kfiber_t *child;
    int handler;

    events[0] = fiber_event_create(false);
    events[1] = fiber_event_create(true);
    if(!events[0] || !events[1]) {
        if(events[0]) fiber_event_destroy(events[0]);
        if(events[1]) fiber_event_destroy(events[1]);
        printf("FIBER-OWNER: event allocation failed\n");
        return -1;
    }

    for(unsigned set = 0; set < 2; ++set) {
        errno = 0;
        int result = fiber_event_wait(events[set]);
        int error = errno;
        check_wait("owner-main", set, result, error, set ? 0 : EDEADLK);
    }

    /* Join before reusing the result storage or destroying either event. */
    for(unsigned attach = 0; attach < 2; ++attach) {
        foreign_case_t probe = { .attach = attach };
        kthread_t *thread = thd_create(false, foreign_wait, &probe);
        if(!thread || thd_join(thread, NULL) < 0)
            arch_panic("fiber ownership test thread lifecycle");
        CHECK(probe.setup_ok && probe.identity_unchanged);
        for(unsigned set = 0; set < 2; ++set)
            check_wait(attach ? "foreign-attached" : "unattached", set,
                       probe.result[set], probe.error[set], EXDEV);
    }

    child_returned = false;
    child = fiber_create(child_stack, sizeof(child_stack), child_wait, events[1]);
    if(!child) arch_panic("fiber ownership test child allocation");
    CHECK(fiber_switch(child) == 0);
    CHECK(child_returned && fiber_get_state(child) == KFIBER_STATE_FINISHED);
    check_wait("owner-child", true, child_result, child_error, 0);
    CHECK(fiber_destroy(child) == 0);

    child_returned = false;
    child = fiber_create(child_stack, sizeof(child_stack), child_wait, events[0]);
    if(!child) arch_panic("fiber ownership test child allocation");
    CHECK(fiber_switch(child) == 0);
    CHECK(!child_returned && fiber_get_state(child) == KFIBER_STATE_WAITING);
    errno = 0;
    CHECK(fiber_event_destroy(events[0]) == -1 && errno == EBUSY);
    CHECK(fiber_event_set(events[0]) == 0);
    CHECK(fiber_get_state(child) == KFIBER_STATE_READY);
    CHECK(fiber_switch(child) == 0);
    CHECK(child_returned && child_result == 0);
    CHECK(fiber_get_state(child) == KFIBER_STATE_FINISHED);
    CHECK(fiber_destroy(child) == 0);
    CHECK(fiber_event_clear(events[0]) == 0);

    /* Destroying a parked child must unlink its waiter before stack reuse. */
    child_returned = false;
    child = fiber_create(child_stack, sizeof(child_stack), child_wait, events[0]);
    if(!child) arch_panic("fiber ownership test child allocation");
    CHECK(fiber_switch(child) == 0);
    CHECK(!child_returned && fiber_get_state(child) == KFIBER_STATE_WAITING);
    CHECK(fiber_destroy(child) == 0);
    CHECK(fiber_event_set(events[0]) == 0);
    CHECK(fiber_event_clear(events[0]) == 0);

    handler = vblank_handler_add(irq_wait, NULL);
    CHECK(handler >= 0);
    if(handler >= 0) {
        uint64_t deadline = timer_ms_gettime64() + 1000;
        while(!irq_done && timer_ms_gettime64() < deadline)
            thd_sleep(1);
        if(vblank_handler_remove(handler) < 0)
            arch_panic("fiber ownership test IRQ retirement");
        CHECK(irq_done);
        if(irq_done) {
            for(unsigned set = 0; set < 2; ++set)
                check_wait("irq", set, irq_result[set], irq_error[set], EPERM);
        }
    }

    CHECK(fiber_event_destroy(events[0]) == 0);
    CHECK(fiber_event_destroy(events[1]) == 0);
    printf("FIBER-OWNER: %s checks=%u failures=%u\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? -1 : 0;
}
