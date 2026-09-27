/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Real session/request/fiber lifetimes with a no-media transport spy.
*/
#include <kos.h>
#include <kos/fiber_disc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../../../kernel/arch/dreamcast/hardware/cdrom_request.h"
#include "../../../../kernel/arch/dreamcast/hardware/gdrom_direct_internal.h"

static unsigned int checks, failures, sibling_steps, ends, busy_retirements;
static cdrom_stream_session_t *probe_session;
static cdrom_request_t *probe_owner;
static bool hold_begin, hold_transfer, hold_end;
static semaphore_t entered, release;
static kthread_t *owner_thread;
static kfiber_t *main_fiber, *waiter;
static fiber_disc_t *disc;
static fiber_disc_stream_t *stream;
static fiber_disc_read_t *transfer_read;
static cdrom_stream_session_status_t session_status;
static cdrom_request_status_t read_status;
_Alignas(32) static uint8_t stack[8192], sibling_stack[8192], buffer[4096];
static int transport_token;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; \
    printf("FIBER-STREAM: failed line=%d errno=%d\n", __LINE__, errno); \
} } while(0)

static int gate(bool hold, cdrom_request_t *parent, cdrom_request_t *transfer) {
    CHECK(thd_get_current() != owner_thread);
    sem_signal(&entered);
    while(hold && sem_trywait(&release) < 0) {
        if((parent && cdrom_request_cancel_requested_internal(parent))
                || (transfer && cdrom_request_cancel_requested_internal(transfer))) {
            errno = ECANCELED;
            return -1;
        }
        thd_sleep(1);
    }
    return 0;
}

cdrom_stream_session_t *__real_gdrom_direct_stream_session_start(
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t start_timeout, uint32_t idle_timeout);
cdrom_stream_session_t *__wrap_gdrom_direct_stream_session_start(
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t start_timeout, uint32_t idle_timeout) {
    probe_session = __real_gdrom_direct_stream_session_start(
        fad, sectors, type, start_timeout, idle_timeout);
    return probe_session;
}

void __real_genwait_wake_all(const void *object);
void __wrap_genwait_wake_all(const void *object) {
    /* The session finalizer publishes terminal state before its owner request.
       Test that this narrow window returns EBUSY rather than waiting on itself. */
    if(probe_session && probe_owner && object == probe_session) {
        cdrom_stream_session_status_t s;
        cdrom_request_status_t r;
        if(!cdrom_stream_session_get_status(probe_session, &s)
                && !cdrom_request_get_status(probe_owner, &r)
                && s.state >= CDROM_STREAM_SESSION_COMPLETE
                && r.state == CDROM_REQUEST_RUNNING) {
            CHECK(cdrom_stream_session_try_destroy(probe_session) < 0
                  && errno == EBUSY);
            ++busy_retirements;
        }
    }
    __real_genwait_wake_all(object);
}

gdrom_direct_stream_t *__wrap_gdrom_direct_stream_begin(
    cdrom_request_t *parent, semaphore_t *wake, uint32_t fad, size_t sectors,
    gdrom_direct_sector_type_t type, uint32_t timeout, gdrom_direct_result_t *result) {
    probe_owner = parent;
    CHECK(wake && fad == 150 && sectors == 2
          && type == GDROM_DIRECT_SECTOR_MODE1 && timeout == 1000 && result);
    if(gate(hold_begin, parent, NULL) < 0) return NULL;
    return (gdrom_direct_stream_t *)&transport_token;
}

int __wrap_gdrom_direct_stream_transfer(gdrom_direct_stream_t *transport,
    cdrom_request_t *parent, cdrom_request_t *transfer, void *dest, size_t bytes,
    uint32_t timeout, gdrom_direct_result_t *result) {
    CHECK(transport == (gdrom_direct_stream_t *)&transport_token
          && dest == buffer && bytes == sizeof(buffer) && timeout == 1000 && result);
    if(gate(hold_transfer, parent, transfer) < 0) return -1;
    memset(dest, 0x6b, bytes);
    return 0;
}

int __wrap_gdrom_direct_stream_end(gdrom_direct_stream_t *transport,
    gdrom_direct_result_t *result) {
    CHECK(transport == (gdrom_direct_stream_t *)&transport_token && result);
    if(gate(hold_end, NULL, NULL) < 0) return -1;
    ++ends;
    return 0;
}

static void await_ready(void *unused) {
    (void)unused;
    CHECK(!fiber_disc_stream_await_ready(stream, &session_status));
}

static void await_done(void *unused) {
    (void)unused;
    CHECK(!fiber_disc_stream_await(stream, &session_status));
}

static void await_read(void *unused) {
    (void)unused;
    CHECK(!fiber_disc_await(transfer_read, &read_status));
}

static void sibling(void *unused) {
    (void)unused;
    CHECK(fiber_disc_stream_await_ready(stream, NULL) < 0 && errno == EBUSY);
    ++sibling_steps;
}

static void park(kfiber_entry_t entry) {
    waiter = fiber_create(stack, sizeof(stack), entry, NULL);
    CHECK(waiter && !fiber_switch(waiter));
    CHECK(fiber_get_state(waiter) == KFIBER_STATE_WAITING);
}

static void drain(void) {
    uint64_t deadline = timer_ms_gettime64() + 5000;
    while(fiber_get_state(waiter) != KFIBER_STATE_FINISHED) {
        if(timer_ms_gettime64() >= deadline) arch_panic("stream fiber deadline");
        CHECK(fiber_disc_pump(disc) >= 0);
        if(fiber_get_state(waiter) == KFIBER_STATE_READY)
            CHECK(!fiber_switch(waiter));
        else
            CHECK(!fiber_disc_idle(disc, 1000));
    }
    CHECK(!fiber_destroy(waiter));
}

static void reset_gates(void) {
    probe_session = NULL;
    probe_owner = NULL;
    while(sem_trywait(&entered) == 0) {}
    while(sem_trywait(&release) == 0) {}
    hold_begin = hold_transfer = hold_end = false;
}

static void start(uint32_t idle_timeout) {
    stream = fiber_disc_stream_start(disc, 150, 2,
        GDROM_DIRECT_SECTOR_MODE1, 1000, idle_timeout);
    CHECK(stream != NULL);
}

int fiber_disc_stream_contract(void) {
    owner_thread = thd_get_current();
    main_fiber = fiber_main();
    CHECK(main_fiber && !cdrom_request_system_init());
    CHECK(!sem_init(&entered, 0) && !sem_init(&release, 0));
    disc = fiber_disc_create(3);
    CHECK(disc != NULL);
    CHECK(!fiber_disc_stream_start(disc, 150, 0,
        GDROM_DIRECT_SECTOR_MODE1, 1000, 1000) && errno == EINVAL);

    hold_begin = true;
    start(1000);
    CHECK(!sem_wait_timed(&entered, 1000));
    CHECK(fiber_disc_stream_await_ready(stream, NULL) < 0 && errno == EPERM);
    CHECK(fiber_disc_stream_destroy(stream) < 0 && errno == EBUSY);
    CHECK(fiber_disc_destroy(disc) < 0 && errno == EBUSY);
    park(await_ready);
    CHECK(fiber_disc_pump(disc) == 1);
    kfiber_t *other = fiber_create(sibling_stack, sizeof(sibling_stack), sibling, NULL);
    CHECK(other && !fiber_switch(other) && !fiber_destroy(other));
    sem_signal(&release);
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_READY
          && session_status.backend == CDROM_REQUEST_BACKEND_DIRECT);

    reset_gates();
    hold_transfer = hold_end = true;
    transfer_read = fiber_disc_stream_transfer(stream, buffer, sizeof(buffer), 1000);
    CHECK(transfer_read && !sem_wait_timed(&entered, 1000));
    CHECK(!fiber_disc_stream_transfer(stream, buffer, sizeof(buffer), 1000)
          && errno == EBUSY);
    park(await_read);
    sem_signal(&release);
    drain();
    CHECK(read_status.state == CDROM_REQUEST_COMPLETE && buffer[0] == 0x6b);
    CHECK(!fiber_disc_read_destroy(transfer_read));
    CHECK(!sem_wait_timed(&entered, 1000)); /* Transport cleanup still held. */
    park(await_done);
    CHECK(fiber_disc_pump(disc) == 1 && fiber_get_state(waiter) == KFIBER_STATE_WAITING);
    sem_signal(&release);
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_COMPLETE
          && session_status.completed_bytes == sizeof(buffer));
    CHECK(!fiber_disc_stream_destroy(stream));

    /* Cancellation during startup must wake a readiness waiter terminally. */
    reset_gates();
    hold_begin = true;
    start(1000);
    CHECK(!sem_wait_timed(&entered, 1000));
    park(await_ready);
    CHECK(!fiber_disc_stream_cancel(stream));
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_CANCELLED);
    CHECK(!fiber_disc_stream_destroy(stream));

    /* READY from the earlier wait must not satisfy a later terminal wait. */
    reset_gates();
    start(20);
    park(await_ready);
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_READY);
    park(await_done);
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_TIMED_OUT);
    CHECK(!fiber_disc_stream_destroy(stream));

    /* Shutdown cancels both a live transfer and its session, then drains. */
    reset_gates();
    start(1000);
    park(await_ready);
    drain();
    reset_gates();
    hold_transfer = true;
    transfer_read = fiber_disc_stream_transfer(stream, buffer, sizeof(buffer), 1000);
    CHECK(transfer_read && !sem_wait_timed(&entered, 1000));
    park(await_done);
    CHECK(!fiber_disc_shutdown(disc));
    CHECK(!fiber_disc_stream_start(disc, 150, 2,
        GDROM_DIRECT_SECTOR_MODE1, 1000, 1000) && errno == ECANCELED);
    drain();
    CHECK(session_status.state == CDROM_STREAM_SESSION_CANCELLED);
    CHECK(!fiber_disc_stream_destroy(stream));
    /* The read handle can outlive its retired parent; awaiting is immediate. */
    waiter = fiber_create(stack, sizeof(stack), await_read, NULL);
    CHECK(waiter && !fiber_switch(waiter)
          && fiber_get_state(waiter) == KFIBER_STATE_FINISHED);
    CHECK(read_status.state == CDROM_REQUEST_CANCELLED);
    CHECK(!fiber_destroy(waiter) && !fiber_disc_read_destroy(transfer_read));
    CHECK(!fiber_disc_destroy(disc));
    cdrom_request_system_shutdown();
    sem_destroy(&entered);
    sem_destroy(&release);
    CHECK(sibling_steps == 1 && ends == 3 && busy_retirements >= 1);
    printf("FIBER-STREAM: %s checks=%u siblings=%u cleanups=%u busy=%u\n",
           failures ? "FAIL" : "PASS", checks, sibling_steps, ends, busy_retirements);
    return failures ? -1 : 0;
}
