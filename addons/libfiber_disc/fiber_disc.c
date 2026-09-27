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
    fiber_disc_stream_t *stream;
};

struct fiber_disc_stream {
    fiber_disc_t *disc;
    struct fiber_disc_stream *next;
    cdrom_stream_session_t *session;
    cdrom_stream_session_status_t status;
    kfiber_event_t *changed;
    kfiber_t *waiter;
    size_t transfers;
    bool wait_ready;
};

struct fiber_disc {
    kthread_t *owner;
    kfiber_t *main;
    fiber_disc_read_t *reads;
    fiber_disc_stream_t *streams;
    semaphore_t completed;
    size_t count, capacity;
    bool closing, retiring, polling;
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

static fiber_disc_read_t *allocate_read(fiber_disc_t *disc) {
    fiber_disc_read_t *read;
    if(check_owner(disc, false) < 0) return NULL;
    if(disc->closing) { errno = ECANCELED; return NULL; }
    if(disc->count == disc->capacity) { errno = EAGAIN; return NULL; }
    read = calloc(1, sizeof(*read));
    if(!read) return NULL;
    read->ready = fiber_event_create(false);
    if(!read->ready) { free(read); return NULL; }
    read->disc = disc;
    return read;
}

static fiber_disc_read_t *admit_read(fiber_disc_read_t *read) {
    fiber_disc_t *disc = read->disc;
    if(!read->request) {
        int error = errno;
        fiber_event_destroy(read->ready);
        free(read);
        errno = error;
        return NULL;
    }
    /* The owner cannot pump until submission returns, even if the callback
       has already posted its semaphore notification on another thread. */
    read->next = disc->reads;
    disc->reads = read;
    ++disc->count;
    return read;
}

static fiber_disc_read_t *read_dma(fiber_disc_t *disc, void *buffer,
    bool gaps, gaps_sram_lease_t lease, size_t offset,
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t timeout) {
    fiber_disc_read_t *read = allocate_read(disc);
    if(!read) return NULL;
    if(gaps)
        read->request = gdrom_direct_read_sectors_dma_gaps_async(
            lease, offset, fad, sectors, type, timeout, NULL, completed, disc);
    else
        read->request = gdrom_direct_read_sectors_dma_async(
            buffer, fad, sectors, type, timeout, NULL, completed, disc);
    return admit_read(read);
}

fiber_disc_read_t *fiber_disc_read_dma(fiber_disc_t *disc, void *buffer,
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t timeout) {
    return read_dma(disc, buffer, false, GAPS_SRAM_LEASE_INVALID, 0,
                    fad, sectors, type, timeout);
}

fiber_disc_read_t *fiber_disc_read_dma_gaps(fiber_disc_t *disc,
    gaps_sram_lease_t lease, size_t offset, uint32_t fad, size_t sectors,
    gdrom_direct_sector_type_t type, uint32_t timeout) {
    return read_dma(disc, NULL, true, lease, offset,
                    fad, sectors, type, timeout);
}

int fiber_disc_pump(fiber_disc_t *disc) {
    int pending = 0;
    if(check_owner(disc, true) < 0) return -1;
    /* Consume old hints before sampling state. A later notification stays
       latched; terminal-but-running callbacks use the bounded retry path. */
    while(sem_trywait(&disc->completed) == 0) {}
    disc->retiring = false;
    disc->polling = false;
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
        if(read->stream) {
            --read->stream->transfers;
            read->stream = NULL;
        }
        if(fiber_event_set(read->ready) < 0) return -1;
    }
    for(fiber_disc_stream_t *stream = disc->streams; stream; stream = stream->next) {
        if(!stream->session) continue;
        if(cdrom_stream_session_get_status(stream->session, &stream->status) < 0)
            return -1;
        switch(stream->status.state) {
            case CDROM_STREAM_SESSION_COMPLETE:
            case CDROM_STREAM_SESSION_CANCELLED:
            case CDROM_STREAM_SESSION_ERROR:
            case CDROM_STREAM_SESSION_TIMED_OUT:
                /* Transfer callbacks can outlive terminal session status. */
                if(stream->transfers) break;
                if(cdrom_stream_session_try_destroy(stream->session) < 0) {
                    if(errno != EBUSY) return -1;
                    break;
                }
                stream->session = NULL;
                if(fiber_event_set(stream->changed) < 0) return -1;
                continue;
            case CDROM_STREAM_SESSION_READY:
                if(stream->waiter && stream->wait_ready
                        && fiber_event_set(stream->changed) < 0) return -1;
                break;
            default:
                break;
        }
        ++pending;
        disc->polling = true;
    }
    return pending;
}

int fiber_disc_idle(fiber_disc_t *disc, uint32_t timeout) {
    if(check_owner(disc, true) < 0) return -1;
    if(!timeout) { errno = EINVAL; return -1; }
    if(disc->retiring || disc->polling) {
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
    for(fiber_disc_stream_t *stream = disc->streams; stream; stream = stream->next)
        if(fiber_disc_stream_cancel(stream) < 0) result = -1;
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
    if(disc->reads || disc->streams) { errno = EBUSY; return -1; }
    sem_destroy(&disc->completed);
    free(disc);
    return 0;
}

fiber_disc_stream_t *fiber_disc_stream_start(fiber_disc_t *disc,
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t start_timeout, uint32_t idle_timeout) {
    fiber_disc_stream_t *stream;
    if(check_owner(disc, false) < 0) return NULL;
    if(disc->closing) { errno = ECANCELED; return NULL; }
    if(disc->count == disc->capacity) { errno = EAGAIN; return NULL; }
    stream = calloc(1, sizeof(*stream));
    if(!stream) return NULL;
    stream->changed = fiber_event_create(false);
    if(!stream->changed) { free(stream); return NULL; }
    stream->session = gdrom_direct_stream_session_start(
        fad, sectors, type, start_timeout, idle_timeout);
    if(!stream->session) {
        int error = errno;
        fiber_event_destroy(stream->changed);
        free(stream);
        errno = error;
        return NULL;
    }
    stream->disc = disc;
    stream->next = disc->streams;
    disc->streams = stream;
    ++disc->count;
    disc->polling = true;
    return stream;
}

fiber_disc_read_t *fiber_disc_stream_transfer(fiber_disc_stream_t *stream,
    void *buffer, size_t bytes, uint32_t timeout) {
    fiber_disc_read_t *read;
    if(!stream) { errno = EINVAL; return NULL; }
    if(check_owner(stream->disc, false) < 0) return NULL;
    if(!stream->session) { errno = ENODEV; return NULL; }
    read = allocate_read(stream->disc);
    if(!read) return NULL;
    read->request = cdrom_stream_session_transfer_async(stream->session,
        buffer, bytes, timeout, completed, stream->disc);
    read = admit_read(read);
    if(read) {
        read->stream = stream;
        ++stream->transfers;
    }
    return read;
}

static int stream_await(fiber_disc_stream_t *stream, bool ready,
    cdrom_stream_session_status_t *status) {
    int result = 0;
    if(!stream) { errno = EINVAL; return -1; }
    if(check_owner(stream->disc, false) < 0) return -1;
    if(fiber_current() == stream->disc->main) { errno = EPERM; return -1; }
    if(stream->waiter) { errno = EBUSY; return -1; }
    stream->waiter = fiber_current();
    stream->wait_ready = ready;
    while(stream->session) {
        /* Each wait samples a new pump result, not a previous READY event. */
        result = fiber_event_clear(stream->changed);
        if(!result) result = fiber_event_wait(stream->changed);
        if(result || (ready && stream->status.state == CDROM_STREAM_SESSION_READY))
            break;
    }
    stream->waiter = NULL;
    if(!result && status) *status = stream->status;
    return result;
}

int fiber_disc_stream_await_ready(fiber_disc_stream_t *stream,
    cdrom_stream_session_status_t *status) {
    return stream_await(stream, true, status);
}

int fiber_disc_stream_await(fiber_disc_stream_t *stream,
    cdrom_stream_session_status_t *status) {
    return stream_await(stream, false, status);
}

int fiber_disc_stream_cancel(fiber_disc_stream_t *stream) {
    if(!stream) { errno = EINVAL; return -1; }
    if(check_owner(stream->disc, false) < 0) return -1;
    return stream->session ? cdrom_stream_session_cancel(stream->session) : 0;
}

int fiber_disc_stream_destroy(fiber_disc_stream_t *stream) {
    fiber_disc_stream_t **link;
    if(!stream) { errno = EINVAL; return -1; }
    if(check_owner(stream->disc, false) < 0) return -1;
    if(stream->session || stream->waiter) { errno = EBUSY; return -1; }
    if(fiber_event_destroy(stream->changed) < 0) return -1;
    for(link = &stream->disc->streams; *link != stream; link = &(*link)->next) {}
    *link = stream->next;
    --stream->disc->count;
    free(stream);
    return 0;
}
