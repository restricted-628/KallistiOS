/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   No-media regression: real fibers, request queue, and callback worker.
*/
#include <kos.h>
#include <kos/fiber_disc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);
static unsigned int checks, failures, steps, callbacks_done, slots;
static bool hold_worker, hold_callback, fail_submit;
static int transport_result;
static semaphore_t worker_entered, worker_release, callback_entered, callback_release;
static kthread_t *owner;
static kfiber_t *main_fiber, *a, *b;
static fiber_disc_t *disc;
static fiber_disc_read_t *read_a, *read_b;
static cdrom_request_status_t result_a, result_b;
_Alignas(32) static uint8_t buffer[4096], stack_a[8192], stack_b[8192];

#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("FIBER-DISC: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

typedef struct job {
    cdrom_request_callback_t callback;
    void *data;
    bool hold_worker, hold_callback;
    int result;
} job_t;
static job_t jobs[16];

static int execute(cdrom_request_t *request, void *params) {
    job_t *job = *(job_t **)params;
    CHECK(thd_get_current() != owner);
    sem_signal(&worker_entered);
    while(job->hold_worker && sem_trywait(&worker_release) < 0) {
        if(cdrom_request_cancel_requested_internal(request)) return ERR_ABORTED;
        thd_sleep(1);
    }
    if(cdrom_request_cancel_requested_internal(request)) return ERR_ABORTED;
    if(job->result == ERR_OK) memset(buffer, 0x5a, sizeof(buffer));
    return job->result;
}

static void complete(cdrom_request_t *request,
                     const cdrom_request_status_t *status, void *data) {
    job_t *job = data;
    job->callback(request, status, job->data);
    if(job->hold_callback) {
        sem_signal(&callback_entered);
        sem_wait(&callback_release); /* Deliberate test-only callback delay. */
    }
    ++callbacks_done;
}

cdrom_request_t *__wrap_gdrom_direct_read_sectors_dma_async(
    void *dest, uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t timeout, gdrom_direct_result_t *trace,
    cdrom_request_callback_t callback, void *data) {
    job_t *job;
    CHECK(dest == buffer && fad == 150 && sectors == 2
          && type == GDROM_DIRECT_SECTOR_MODE1 && timeout == 1000
          && !trace && callback && data);
    if(fail_submit) { errno = ENOMEM; return NULL; }
    if(slots == 16) arch_panic("probe slots exhausted");
    job = &jobs[slots++];
    *job = (job_t){ callback, data, hold_worker, hold_callback, transport_result };
    return cdrom_request_submit_executor(CD_CMD_DMAREAD, &job, sizeof(job),
        sizeof(buffer), sizeof(buffer), sizeof(buffer), timeout, execute,
        NULL, NULL, complete, job);
}

static fiber_disc_read_t *submit(void) {
    return fiber_disc_read_dma(disc, buffer, 150, 2,
                              GDROM_DIRECT_SECTOR_MODE1, 1000);
}

static void await_a(void *data) {
    (void)data;
    CHECK(fiber_disc_pump(disc) < 0 && errno == EPERM);
    CHECK(fiber_disc_idle(disc, 1) < 0 && errno == EPERM);
    CHECK(!fiber_disc_await(read_a, &result_a));
}

static void await_b(void *data) {
    (void)data;
    CHECK(!fiber_disc_await(read_b, &result_b));
}

static void sibling(void *data) {
    (void)data;
    CHECK(fiber_disc_await(read_a, NULL) < 0 && errno == EBUSY);
    for(unsigned int i = 0; i < 3; ++i) {
        ++steps;
        CHECK(!fiber_switch(main_fiber));
    }
}

static void run_until_done(kfiber_t *fiber) {
    uint64_t deadline = timer_ms_gettime64() + 5000;
    while(fiber_get_state(fiber) != KFIBER_STATE_FINISHED) {
        if(timer_ms_gettime64() >= deadline) arch_panic("fiber probe deadline");
        CHECK(fiber_disc_pump(disc) >= 0);
        if(fiber_get_state(fiber) == KFIBER_STATE_READY)
            CHECK(!fiber_switch(fiber));
        else if(fiber_disc_idle(disc, 10) < 0)
            CHECK(errno == ETIMEDOUT);
    }
}

static void new_a(void) {
    a = fiber_create(stack_a, sizeof(stack_a), await_a, NULL);
    CHECK(a != NULL);
}

static void release_a(void) {
    CHECK(!fiber_destroy(a));
    CHECK(!fiber_disc_read_destroy(read_a));
    read_a = NULL;
}

static void *wrong_owner(void *unused) {
    (void)unused;
    CHECK(fiber_disc_pump(disc) < 0 && errno == EXDEV);
    CHECK(fiber_disc_shutdown(disc) < 0 && errno == EXDEV);
    return NULL;
}

int main(void) {
    kthread_t *other;
    CHECK(!cdrom_request_system_init());
    CHECK(!sem_init(&worker_entered, 0) && !sem_init(&worker_release, 0)
          && !sem_init(&callback_entered, 0) && !sem_init(&callback_release, 0));
    owner = thd_get_current();
    CHECK(!fiber_disc_create(2) && errno == EPERM);
    main_fiber = fiber_attach();
    CHECK(main_fiber != NULL);
    CHECK(!fiber_disc_create(0) && errno == EINVAL);
    disc = fiber_disc_create(2);
    CHECK(disc != NULL);
    CHECK(fiber_disc_idle(disc, 0) < 0 && errno == EINVAL);
    other = thd_create(false, wrong_owner, NULL);
    CHECK(other && !thd_join(other, NULL));
    fail_submit = true;
    CHECK(!submit() && errno == ENOMEM);
    fail_submit = false;

    /* A parked reader must not stop another fiber on the same OS thread. */
    hold_worker = true;
    read_a = submit();
    CHECK(read_a != NULL);
    CHECK(!sem_wait_timed(&worker_entered, 1000));
    CHECK(fiber_disc_await(read_a, NULL) < 0 && errno == EPERM);
    CHECK(fiber_disc_destroy(disc) < 0 && errno == EBUSY);
    CHECK(fiber_disc_read_destroy(read_a) < 0 && errno == EBUSY);
    new_a();
    CHECK(!fiber_switch(a) && fiber_get_state(a) == KFIBER_STATE_WAITING);
    b = fiber_create(stack_b, sizeof(stack_b), sibling, NULL);
    CHECK(b != NULL);
    for(unsigned int i = 0; i < 4; ++i) CHECK(!fiber_switch(b));
    CHECK(steps == 3 && fiber_get_state(a) == KFIBER_STATE_WAITING);
    CHECK(!fiber_destroy(b));
    sem_signal(&worker_release);
    run_until_done(a);
    CHECK(result_a.state == CDROM_REQUEST_COMPLETE
          && result_a.completed_bytes == sizeof(buffer) && buffer[0] == 0x5a);
    release_a();

    /* Completion before the child starts waiting must remain observable. */
    hold_worker = false;
    read_a = submit();
    CHECK(read_a != NULL);
    uint64_t deadline = timer_ms_gettime64() + 5000;
    while(fiber_disc_pump(disc) > 0) {
        if(timer_ms_gettime64() >= deadline) arch_panic("early completion deadline");
        (void)fiber_disc_idle(disc, 10);
    }
    new_a();
    CHECK(!fiber_switch(a) && fiber_get_state(a) == KFIBER_STATE_FINISHED);
    CHECK(result_a.state == CDROM_REQUEST_COMPLETE);
    release_a();

    /* The notification is not permission to free a still-running callback. */
    hold_callback = true;
    read_a = submit();
    CHECK(read_a != NULL);
    CHECK(!sem_wait_timed(&callback_entered, 1000));
    new_a();
    CHECK(!fiber_switch(a));
    CHECK(fiber_disc_pump(disc) == 1
          && fiber_get_state(a) == KFIBER_STATE_WAITING);
    CHECK(fiber_disc_read_destroy(read_a) < 0 && errno == EBUSY);
    CHECK(!fiber_disc_idle(disc, 1));
    sem_signal(&callback_release);
    deadline = timer_ms_gettime64() + 5000;
    while(fiber_disc_pump(disc) > 0) {
        if(timer_ms_gettime64() >= deadline) arch_panic("callback retirement deadline");
        (void)fiber_disc_idle(disc, 10);
    }
    CHECK(fiber_get_state(a) == KFIBER_STATE_READY);
    CHECK(fiber_disc_read_destroy(read_a) < 0 && errno == EBUSY);
    run_until_done(a);
    release_a();
    hold_callback = false;

    /* Running cancellation and shutdown preserve the waiter and destination. */
    while(sem_trywait(&worker_entered) == 0) {}
    hold_worker = true;
    read_a = submit();
    CHECK(read_a && !sem_wait_timed(&worker_entered, 1000));
    new_a();
    CHECK(!fiber_switch(a));
    CHECK(!fiber_disc_cancel(read_a));
    run_until_done(a);
    CHECK(result_a.state == CDROM_REQUEST_CANCELLED);
    release_a();

    read_a = submit();
    CHECK(read_a && !sem_wait_timed(&worker_entered, 1000));
    read_b = submit();
    CHECK(read_b != NULL);
    CHECK(!submit() && errno == EAGAIN);
    new_a();
    b = fiber_create(stack_b, sizeof(stack_b), await_b, NULL);
    CHECK(b && !fiber_switch(a) && !fiber_switch(b));
    CHECK(!fiber_disc_cancel(read_b)); /* Queued behind held A. */
    run_until_done(b);
    CHECK(result_b.state == CDROM_REQUEST_CANCELLED);
    CHECK(!fiber_destroy(b) && !fiber_disc_read_destroy(read_b));
    CHECK(!fiber_disc_shutdown(disc));
    CHECK(!submit() && errno == ECANCELED);
    run_until_done(a);
    CHECK(result_a.state == CDROM_REQUEST_CANCELLED);
    release_a();
    CHECK(!fiber_disc_destroy(disc));

    disc = fiber_disc_create(1);
    hold_worker = false;
    transport_result = ERR_TIMEOUT;
    read_a = submit();
    CHECK(read_a != NULL);
    new_a();
    run_until_done(a);
    CHECK(result_a.state == CDROM_REQUEST_TIMED_OUT && result_a.error == ETIMEDOUT);
    release_a();
    cdrom_request_system_shutdown();
    CHECK(!submit() && errno == ENODEV);
    CHECK(!fiber_disc_destroy(disc));
    CHECK(callbacks_done == 7);
    printf("FIBER-DISC: %s checks=%u siblings=%u callbacks=%u\n",
           failures ? "FAIL" : "PASS", checks, steps, callbacks_done);
    return failures ? 1 : 0;
}
