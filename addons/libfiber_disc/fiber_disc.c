/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos/fiber_disc.h>
#include <kos/fiber.h>
#include <kos/fiber_sync.h>
#include <kos/irq.h>
#include <kos/sem.h>
#include <kos/thread.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

struct fiber_disc_read {
    fiber_disc_t *disc;
    struct fiber_disc_read *next;
    cdrom_request_t *request;
    cdrom_request_status_t status;
    kfiber_event_t *ready;
    kfiber_t *waiter;
};

struct fiber_disc {
    kthread_t *owner;
    kfiber_t *main;
    fiber_disc_read_t *reads;
    semaphore_t completed;
    size_t count, capacity;
    bool closing, retiring;
};

static int check_owner(fiber_disc_t *disc, bool main_only) {
    if(irq_inside_int()) { errno = EPERM; return -1; }
    if(!disc) { errno = EINVAL; return -1; }
    if(thd_get_current() != disc->owner || fiber_main() != disc->main) {
        errno = EXDEV;
        return -1;
    }
    if(main_only && fiber_current() != disc->main) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static bool terminal(cdrom_request_state_t state) {
    return state == CDROM_REQUEST_COMPLETE || state == CDROM_REQUEST_CANCELLED
        || state == CDROM_REQUEST_ERROR || state == CDROM_REQUEST_TIMED_OUT;
}

/* Notification only. Never switch fibers, touch hardware, or release request
   storage here. The pump also accounts for callback return, not just entry. */
static void completed(cdrom_request_t *request,
                      const cdrom_request_status_t *status, void *data) {
    fiber_disc_t *disc = data;
    (void)request;
    (void)status;
    sem_signal(&disc->completed);
}

fiber_disc_t *fiber_disc_create(size_t capacity) {
    fiber_disc_t *disc;
    kfiber_t *main;
    if(irq_inside_int()) { errno = EPERM; return NULL; }
    if(!capacity || capacity > INT_MAX) { errno = EINVAL; return NULL; }
    main = fiber_main();
    if(!main) { errno = EPERM; return NULL; }
    disc = calloc(1, sizeof(*disc));
    if(!disc) return NULL;
    if(sem_init(&disc->completed, 0) < 0) { free(disc); return NULL; }
    disc->owner = thd_get_current();
    disc->main = main;
    disc->capacity = capacity;
    return disc;
}

fiber_disc_read_t *fiber_disc_read_dma(fiber_disc_t *disc, void *buffer,
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t timeout) {
    fiber_disc_read_t *read;
    if(check_owner(disc, false) < 0) return NULL;
    if(disc->closing) { errno = ECANCELED; return NULL; }
    if(disc->count == disc->capacity) { errno = EAGAIN; return NULL; }
    read = calloc(1, sizeof(*read));
    if(!read) return NULL;
    read->ready = fiber_event_create(false);
    if(!read->ready) { free(read); return NULL; }
    read->disc = disc;
    read->request = gdrom_direct_read_sectors_dma_async(
        buffer, fad, sectors, type, timeout, NULL, completed, disc);
    if(!read->request) {
        int error = errno;
        fiber_event_destroy(read->ready);
        free(read);
        errno = error;
        return NULL;
    }
    /* This owner cannot pump until submission returns, even when the callback
       has already run on another thread. Its semaphore notification persists. */
    read->next = disc->reads;
    disc->reads = read;
    ++disc->count;
    return read;
}

int fiber_disc_pump(fiber_disc_t *disc) {
    int pending = 0;
    if(check_owner(disc, true) < 0) return -1;
    /* Consume old hints before sampling state. A later notification stays
       latched; terminal-but-running callbacks use the bounded retry path. */
    while(sem_trywait(&disc->completed) == 0) {}
    disc->retiring = false;
    for(fiber_disc_read_t *read = disc->reads; read; read = read->next) {
        if(!read->request) continue;
        if(cdrom_request_get_status(read->request, &read->status) < 0)
            return -1;
        if(!terminal(read->status.state)) { ++pending; continue; }
        if(cdrom_request_destroy(read->request) < 0) {
            if(errno != EBUSY) return -1;
            /* Terminal data can precede callback completion. Keep both the
               request and notification context alive and retry cooperatively. */
            disc->retiring = true;
            ++pending;
            continue;
        }
        read->request = NULL;
        if(fiber_event_set(read->ready) < 0) return -1;
    }
    return pending;
}

int fiber_disc_idle(fiber_disc_t *disc, uint32_t timeout) {
    if(check_owner(disc, true) < 0) return -1;
    if(!timeout) { errno = EINVAL; return -1; }
    if(disc->retiring) {
        /* Yield the OS thread, including to lower-priority callback workers.
           The application must have dispatched all ready siblings first. */
        thd_sleep(1);
        return 0;
    }
    return sem_wait_timed(&disc->completed, timeout);
}

int fiber_disc_await(fiber_disc_read_t *read, cdrom_request_status_t *status) {
    int result = 0;
    if(!read) { errno = EINVAL; return -1; }
    if(check_owner(read->disc, false) < 0) return -1;
    if(fiber_current() == read->disc->main) { errno = EPERM; return -1; }
    if(read->waiter) { errno = EBUSY; return -1; }
    read->waiter = fiber_current();
    if(read->request)
        result = fiber_event_wait(read->ready);
    read->waiter = NULL;
    if(!result && status) *status = read->status;
    return result;
}

int fiber_disc_cancel(fiber_disc_read_t *read) {
    if(!read) { errno = EINVAL; return -1; }
    if(check_owner(read->disc, false) < 0) return -1;
    return read->request ? cdrom_request_cancel(read->request) : 0;
}

int fiber_disc_shutdown(fiber_disc_t *disc) {
    int result = 0;
    if(check_owner(disc, false) < 0) return -1;
    disc->closing = true;
    for(fiber_disc_read_t *read = disc->reads; read; read = read->next)
        if(fiber_disc_cancel(read) < 0) result = -1;
    return result;
}

int fiber_disc_read_destroy(fiber_disc_read_t *read) {
    fiber_disc_read_t **link;
    if(!read) { errno = EINVAL; return -1; }
    if(check_owner(read->disc, false) < 0) return -1;
    if(read->request || read->waiter) { errno = EBUSY; return -1; }
    if(fiber_event_destroy(read->ready) < 0) return -1;
    for(link = &read->disc->reads; *link != read; link = &(*link)->next) {}
    *link = read->next;
    --read->disc->count;
    free(read);
    return 0;
}

int fiber_disc_destroy(fiber_disc_t *disc) {
    if(check_owner(disc, false) < 0) return -1;
    if(disc->reads) { errno = EBUSY; return -1; }
    sem_destroy(&disc->completed);
    free(disc);
    return 0;
}
