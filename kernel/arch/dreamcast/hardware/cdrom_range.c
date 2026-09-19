/* KallistiOS ##version##

   Bounded raw-disc sector ranges.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>

#include <kos/mutex.h>
#include <kos/timer.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cdrom_request.h"
#include "gdrom_direct_internal.h"

#define CDROM_RANGE_SECTOR_SIZE 2048u
#define CDROM_RANGE_CHUNK_SECTORS 16u
#define CDROM_RANGE_MAX_FAD 0x00ffffffu

/* A range is a bounded, sector-oriented cursor layered on the same request
   engine used by ordinary CD and ISO reads. The range mutex protects cursor
   and close/busy state only; no drive operation runs while it is held. Async
   operations retain the range until their private finalizer advances the
   cursor and releases ownership, so callers may not close or seek it midway. */
struct cdrom_sector_range {
    mutex_t mutex;
    size_t references;
    uint32_t start_fad;
    size_t sector_count;
    size_t position;
    cdrom_request_backend_t backend;
    gdrom_direct_sector_type_t direct_sector_type;
    bool busy;
    bool closed;
};

typedef struct cdrom_range_async {
    cdrom_sector_range_t *range;
    uint8_t *buffer;
    size_t start;
    size_t requested_sectors;
    size_t active_sectors;
    size_t delivered_sectors;
    bool advance;
    cdrom_request_callback_t callback;
    void *callback_data;
    gdrom_direct_result_t direct_result;
} cdrom_range_async_t;

typedef struct cdrom_range_stream {
    cdrom_sector_range_t *range;
    size_t start;
} cdrom_range_stream_t;

static void range_release(cdrom_sector_range_t *range) {
    bool destroy;

    mutex_lock(&range->mutex);
    destroy = --range->references == 0;
    mutex_unlock(&range->mutex);

    if(destroy) {
        mutex_destroy(&range->mutex);
        free(range);
    }
}

static cdrom_sector_range_t *range_open(
        uint32_t start_fad, size_t sector_count,
        cdrom_request_backend_t backend,
        gdrom_direct_sector_type_t direct_sector_type) {
    cdrom_sector_range_t *range;

    if(start_fad < 150u || start_fad > CDROM_RANGE_MAX_FAD
            || !sector_count || sector_count > SIZE_MAX / CDROM_RANGE_SECTOR_SIZE
            || sector_count > (size_t)INT64_MAX
            || sector_count - 1u > CDROM_RANGE_MAX_FAD - start_fad
            || (backend == CDROM_REQUEST_BACKEND_DIRECT
                && direct_sector_type != GDROM_DIRECT_SECTOR_MODE1
                && direct_sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)) {
        errno = EINVAL;
        return NULL;
    }

    range = calloc(1, sizeof(*range));
    if(!range) {
        errno = ENOMEM;
        return NULL;
    }
    if(mutex_init(&range->mutex, MUTEX_TYPE_NORMAL) < 0) {
        free(range);
        return NULL;
    }

    range->references = 1;
    range->start_fad = start_fad;
    range->sector_count = sector_count;
    range->backend = backend;
    range->direct_sector_type = direct_sector_type;
    return range;
}

cdrom_sector_range_t *cdrom_sector_range_open(
        uint32_t start_fad, size_t sector_count) {
    if(cdrom_sector_size_internal() != CDROM_RANGE_SECTOR_SIZE) {
        errno = ENOTSUP;
        return NULL;
    }

    return range_open(start_fad, sector_count, CDROM_REQUEST_BACKEND_BIOS,
                      GDROM_DIRECT_SECTOR_MODE1);
}

cdrom_sector_range_t *gdrom_direct_sector_range_open(
        uint32_t start_fad, size_t sector_count,
        gdrom_direct_sector_type_t sector_type) {
    return range_open(start_fad, sector_count, CDROM_REQUEST_BACKEND_DIRECT,
                      sector_type);
}

int cdrom_sector_range_close(cdrom_sector_range_t *range) {
    if(!range) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        mutex_unlock(&range->mutex);
        errno = EBADF;
        return -1;
    }
    if(range->busy) {
        mutex_unlock(&range->mutex);
        errno = EBUSY;
        return -1;
    }
    range->closed = true;
    mutex_unlock(&range->mutex);
    range_release(range);
    return 0;
}

int cdrom_sector_range_get_info(
        cdrom_sector_range_t *range,
        cdrom_sector_range_info_t *info) {
    if(!range || !info) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        mutex_unlock(&range->mutex);
        errno = EBADF;
        return -1;
    }
    *info = (cdrom_sector_range_info_t) {
        .backend = range->backend,
        .start_fad = range->start_fad,
        .sector_count = range->sector_count,
        .position = range->position,
    };
    mutex_unlock(&range->mutex);
    return 0;
}

int64_t cdrom_sector_range_seek(
        cdrom_sector_range_t *range, int64_t offset, int whence) {
    int64_t base;
    int64_t next;

    if(!range) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        mutex_unlock(&range->mutex);
        errno = EBADF;
        return -1;
    }
    if(range->busy) {
        mutex_unlock(&range->mutex);
        errno = EBUSY;
        return -1;
    }

    switch(whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)range->position;
            break;
        case SEEK_END:
            base = (int64_t)range->sector_count;
            break;
        default:
            mutex_unlock(&range->mutex);
            errno = EINVAL;
            return -1;
    }

    if(offset >= 0) {
        if(base > INT64_MAX - offset) {
            mutex_unlock(&range->mutex);
            errno = EINVAL;
            return -1;
        }
        next = base + offset;
    }
    else {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1u;

        if(magnitude > (uint64_t)base) {
            mutex_unlock(&range->mutex);
            errno = EINVAL;
            return -1;
        }
        next = base - (int64_t)magnitude;
    }
    if(next < 0) {
        mutex_unlock(&range->mutex);
        errno = EINVAL;
        return -1;
    }
    if((uint64_t)next > range->sector_count)
        next = (int64_t)range->sector_count;

    range->position = (size_t)next;
    mutex_unlock(&range->mutex);
    return next;
}

int64_t cdrom_sector_range_tell(cdrom_sector_range_t *range) {
    cdrom_sector_range_info_t info;

    if(cdrom_sector_range_get_info(range, &info) < 0)
        return -1;
    return (int64_t)info.position;
}

int cdrom_sector_range_eof(
        cdrom_sector_range_t *range, bool *eof) {
    cdrom_sector_range_info_t info;

    if(!eof) {
        errno = EINVAL;
        return -1;
    }
    if(cdrom_sector_range_get_info(range, &info) < 0)
        return -1;
    *eof = info.position == info.sector_count;
    return 0;
}

static int range_make_segment(
        cdrom_range_async_t *async,
        cdrom_request_dma_segment_t *segment) {
    /* A logical read is requeued in 32 KiB pieces so a large range never owns
       G1 for its entire length. The request engine accumulates physical and
       useful byte counts across these segments. */
    size_t remaining = async->active_sectors - async->delivered_sectors;
    size_t sectors = remaining > CDROM_RANGE_CHUNK_SECTORS
        ? CDROM_RANGE_CHUNK_SECTORS : remaining;
    uint64_t fad = (uint64_t)async->range->start_fad + async->start
        + async->delivered_sectors;

    if(!remaining
            || (async->range->backend == CDROM_REQUEST_BACKEND_BIOS
                && cdrom_sector_size_internal()
                    != CDROM_RANGE_SECTOR_SIZE)) {
        errno = ENOTSUP;
        return -1;
    }
    if(fad > CDROM_RANGE_MAX_FAD
            || cdrom_request_dma_segment_init_sized(
                segment,
                async->buffer
                    + async->delivered_sectors * CDROM_RANGE_SECTOR_SIZE,
                (uint32_t)fad, sectors, CDROM_RANGE_SECTOR_SIZE, 0,
                sectors * CDROM_RANGE_SECTOR_SIZE, true) < 0)
        return -1;
    if(segment->io_bytes != sectors * CDROM_RANGE_SECTOR_SIZE) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int range_continue(
        cdrom_request_t *request,
        const cdrom_request_dma_segment_t *completed,
        cdrom_request_dma_segment_t *next, void *data) {
    cdrom_range_async_t *async = data;

    (void)request;
    async->delivered_sectors += completed->data_bytes
        / CDROM_RANGE_SECTOR_SIZE;
    if(async->delivered_sectors == async->active_sectors)
        return 0;
    return range_make_segment(async, next) < 0 ? -1 : 1;
}

static void range_async_finalize(
        cdrom_request_t *request,
        const cdrom_request_status_t *status, void *data) {
    cdrom_range_async_t *async = data;
    cdrom_sector_range_t *range = async->range;

    /* Finalizers run before terminal publication. Clear busy and commit the
       cursor here so waiters observe coherent range state with the result. */
    (void)request;
    mutex_lock(&range->mutex);
    if(async->advance && status->state == CDROM_REQUEST_COMPLETE
            && !range->closed)
        range->position = async->start + status->data_bytes
            / CDROM_RANGE_SECTOR_SIZE;
    range->busy = false;
    mutex_unlock(&range->mutex);

    if(!async->callback) {
        range_release(range);
        free(async);
    }
}

static void range_async_complete(
        cdrom_request_t *request,
        const cdrom_request_status_t *status, void *data) {
    cdrom_range_async_t *async = data;

    if(async->callback)
        async->callback(request, status, async->callback_data);
    range_release(async->range);
    free(async);
}

static int range_direct_noop(cdrom_request_t *request, void *data) {
    (void)request;
    (void)data;
    return ERR_OK;
}

cdrom_request_t *cdrom_sector_range_read_async(
        cdrom_sector_range_t *range, void *buffer, size_t sector_count,
        uint32_t timeout, cdrom_request_callback_t callback,
        void *callback_data) {
    cdrom_range_async_t *async;
    cdrom_request_dma_segment_t first;
    cdrom_request_t *request;
    size_t remaining;
    size_t active;
    size_t requested_bytes;
    size_t active_bytes;
    int saved_errno;

    if(!range || (sector_count && (!buffer || ((uintptr_t)buffer & 31u)))
            || sector_count > SIZE_MAX / CDROM_RANGE_SECTOR_SIZE
            || (sector_count && !timeout)) {
        errno = EINVAL;
        return NULL;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        return NULL;
    }

    /* Admission and reference publication are atomic with busy=true. A fast
       request therefore cannot finalize before the range owns its reference. */
    mutex_lock(&range->mutex);
    if(range->closed) {
        errno = EBADF;
        goto fail_locked;
    }
    if(range->busy) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(range->backend == CDROM_REQUEST_BACKEND_BIOS
            && cdrom_sector_size_internal() != CDROM_RANGE_SECTOR_SIZE) {
        errno = ENOTSUP;
        goto fail_locked;
    }

    remaining = range->sector_count - range->position;
    active = sector_count < remaining ? sector_count : remaining;
    requested_bytes = sector_count * CDROM_RANGE_SECTOR_SIZE;
    active_bytes = active * CDROM_RANGE_SECTOR_SIZE;

    async->range = range;
    async->buffer = buffer;
    async->start = range->position;
    async->requested_sectors = sector_count;
    async->active_sectors = active;
    async->advance = true;
    async->callback = callback;
    async->callback_data = callback_data;
    ++range->references;

    if(active) {
        if(range_make_segment(async, &first) < 0)
            goto fail_reference_locked;

        if(range->backend == CDROM_REQUEST_BACKEND_DIRECT)
            request = cdrom_request_submit_direct_dma_chain(
                &first, range->direct_sector_type,
                requested_bytes, active_bytes, active_bytes, timeout,
                range_continue, async, range_async_finalize, async,
                callback ? range_async_complete : NULL, async);
        else
            request = cdrom_request_submit_dma_chain(
                &first, requested_bytes, active_bytes, active_bytes, timeout,
                range_continue, async, range_async_finalize, async,
                callback ? range_async_complete : NULL, async);
    }
    else if(range->backend == CDROM_REQUEST_BACKEND_DIRECT) {
        request = cdrom_request_submit_executor(
            CD_CMD_DMAREAD, NULL, 0, requested_bytes, 0, 0, 0,
            range_direct_noop, range_async_finalize, async,
            callback ? range_async_complete : NULL, async);
    }
    else {
        request = cdrom_request_submit_noop(
            CD_CMD_DMAREAD, requested_bytes, range_async_finalize, async,
            callback ? range_async_complete : NULL, async);
    }
    if(!request)
        goto fail_reference_locked;

    range->busy = true;
    mutex_unlock(&range->mutex);
    return request;

fail_reference_locked:
    --range->references;
fail_locked:
    saved_errno = errno;
    mutex_unlock(&range->mutex);
    free(async);
    errno = saved_errno;
    return NULL;
}

static int range_wait_request(
        cdrom_request_t *request, uint32_t timeout,
        cdrom_request_status_t *status) {
    if(cdrom_request_wait(request, timeout, status) < 0) {
        int saved_errno = errno;

        (void)cdrom_request_cancel(request);
        (void)cdrom_request_wait(request, 0, NULL);
        (void)cdrom_request_destroy(request);
        errno = saved_errno;
        return -1;
    }
    if(status->state != CDROM_REQUEST_COMPLETE) {
        int error = status->error ? status->error : EIO;

        (void)cdrom_request_destroy(request);
        errno = error;
        return -1;
    }
    (void)cdrom_request_destroy(request);
    return 0;
}

ssize_t cdrom_sector_range_read(
        cdrom_sector_range_t *range, void *buffer, size_t sector_count,
        uint32_t timeout) {
    cdrom_request_status_t status;
    cdrom_request_t *request;
    uint8_t *bounce = NULL;
    uint8_t *destination = buffer;
    uint64_t deadline;
    size_t start;
    size_t remaining;
    size_t active;
    size_t delivered = 0;
    int saved_errno;

    if(!range || (sector_count && (!buffer || !timeout))
            || sector_count > (size_t)INTPTR_MAX) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        mutex_unlock(&range->mutex);
        errno = EBADF;
        return -1;
    }
    if(range->busy) {
        mutex_unlock(&range->mutex);
        errno = EBUSY;
        return -1;
    }
    if(range->backend == CDROM_REQUEST_BACKEND_BIOS
            && cdrom_sector_size_internal() != CDROM_RANGE_SECTOR_SIZE) {
        mutex_unlock(&range->mutex);
        errno = ENOTSUP;
        return -1;
    }

    start = range->position;
    remaining = range->sector_count - start;
    active = sector_count < remaining ? sector_count : remaining;
    if(!active) {
        mutex_unlock(&range->mutex);
        return 0;
    }
    range->busy = true;
    mutex_unlock(&range->mutex);

    if((uintptr_t)destination & 31u) {
        bounce = aligned_alloc(
            32, CDROM_RANGE_CHUNK_SECTORS * CDROM_RANGE_SECTOR_SIZE);
        if(!bounce) {
            errno = ENOMEM;
            goto fail;
        }
    }

    deadline = timer_ms_gettime64() + timeout;
    while(delivered < active) {
        size_t chunk = active - delivered;
        uint64_t now = timer_ms_gettime64();
        uint64_t left;
        uint32_t fad;
        uint32_t command_timeout;
        void *chunk_buffer;

        if(now >= deadline) {
            errno = ETIMEDOUT;
            goto fail;
        }
        left = deadline - now;
        command_timeout = left > UINT32_MAX ? UINT32_MAX : (uint32_t)left;
        if(chunk > CDROM_RANGE_CHUNK_SECTORS)
            chunk = CDROM_RANGE_CHUNK_SECTORS;
        fad = range->start_fad + (uint32_t)(start + delivered);
        chunk_buffer = bounce ? (void *)bounce
            : destination + delivered * CDROM_RANGE_SECTOR_SIZE;

        if(range->backend == CDROM_REQUEST_BACKEND_DIRECT)
            request = gdrom_direct_read_sectors_dma_async(
                chunk_buffer, fad, chunk, range->direct_sector_type,
                command_timeout, NULL, NULL, NULL);
        else
            request = cdrom_read_sectors_async(
                chunk_buffer, fad, chunk, command_timeout, NULL, NULL);
        if(!request)
            goto fail;
        if(range_wait_request(request, command_timeout, &status) < 0)
            goto fail;
        if(status.data_bytes != chunk * CDROM_RANGE_SECTOR_SIZE) {
            errno = EIO;
            goto fail;
        }

        if(bounce)
            memcpy(destination + delivered * CDROM_RANGE_SECTOR_SIZE,
                   bounce, chunk * CDROM_RANGE_SECTOR_SIZE);
        delivered += chunk;
    }

    free(bounce);
    mutex_lock(&range->mutex);
    if(!range->closed)
        range->position = start + delivered;
    range->busy = false;
    mutex_unlock(&range->mutex);
    return (ssize_t)delivered;

fail:
    saved_errno = errno;
    free(bounce);
    mutex_lock(&range->mutex);
    range->busy = false;
    mutex_unlock(&range->mutex);
    errno = saved_errno;
    return -1;
}

cdrom_request_t *cdrom_sector_range_preseek_async(
        cdrom_sector_range_t *range, uint32_t timeout,
        cdrom_request_callback_t callback, void *callback_data) {
    cdrom_range_async_t *async;
    cdrom_request_t *request;
    uint64_t fad;
    int saved_errno;

    if(!range || !timeout) {
        errno = EINVAL;
        return NULL;
    }
    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        return NULL;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        errno = EBADF;
        goto fail_locked;
    }
    if(range->busy) {
        errno = EBUSY;
        goto fail_locked;
    }

    fad = (uint64_t)range->start_fad + range->position;
    if(fad > CDROM_RANGE_MAX_FAD) {
        errno = EOVERFLOW;
        goto fail_locked;
    }
    async->range = range;
    async->start = range->position;
    async->callback = callback;
    async->callback_data = callback_data;
    ++range->references;

    if(range->backend == CDROM_REQUEST_BACKEND_DIRECT)
        request = gdrom_direct_seek_async_internal(
            (uint32_t)fad, timeout, &async->direct_result,
            range_async_finalize, async,
            callback ? range_async_complete : NULL, async);
    else
        request = cdrom_seek_async_internal(
            (uint32_t)fad, timeout, range_async_finalize, async,
            callback ? range_async_complete : NULL, async);
    if(!request)
        goto fail_reference_locked;

    range->busy = true;
    mutex_unlock(&range->mutex);
    return request;

fail_reference_locked:
    --range->references;
fail_locked:
    saved_errno = errno;
    mutex_unlock(&range->mutex);
    free(async);
    errno = saved_errno;
    return NULL;
}

static void range_stream_finalize(
        cdrom_stream_session_t *session,
        const cdrom_stream_session_status_t *status, void *data) {
    cdrom_range_stream_t *stream = data;
    cdrom_sector_range_t *range = stream->range;

    /* Streaming advances only by bytes actually transferred; cancellation or
       idle timeout therefore leaves the cursor at a resumable sector. */
    (void)session;
    mutex_lock(&range->mutex);
    if(!range->closed)
        range->position = stream->start
            + status->completed_bytes / CDROM_RANGE_SECTOR_SIZE;
    range->busy = false;
    mutex_unlock(&range->mutex);
    range_release(range);
    free(stream);
}

cdrom_stream_session_t *cdrom_sector_range_stream_start(
        cdrom_sector_range_t *range, size_t sector_count,
        uint32_t start_timeout, uint32_t idle_timeout) {
    cdrom_range_stream_t *stream;
    cdrom_stream_session_t *session;
    size_t remaining;
    size_t active;
    uint32_t fad;
    int saved_errno;

    if(!range || !sector_count || !start_timeout || !idle_timeout) {
        errno = EINVAL;
        return NULL;
    }
    stream = calloc(1, sizeof(*stream));
    if(!stream) {
        errno = ENOMEM;
        return NULL;
    }

    mutex_lock(&range->mutex);
    if(range->closed) {
        errno = EBADF;
        goto fail_locked;
    }
    if(range->busy) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(range->backend == CDROM_REQUEST_BACKEND_BIOS
            && cdrom_sector_size_internal() != CDROM_RANGE_SECTOR_SIZE) {
        errno = ENOTSUP;
        goto fail_locked;
    }

    remaining = range->sector_count - range->position;
    active = sector_count < remaining ? sector_count : remaining;
    if(!active) {
        errno = ENODATA;
        goto fail_locked;
    }
    fad = range->start_fad + (uint32_t)range->position;
    stream->range = range;
    stream->start = range->position;
    ++range->references;

    session = cdrom_stream_session_start_internal(
        fad, active, CDROM_RANGE_SECTOR_SIZE,
        active * CDROM_RANGE_SECTOR_SIZE,
        start_timeout, idle_timeout, range->backend,
        range->direct_sector_type, range_stream_finalize, stream);
    if(!session)
        goto fail_reference_locked;

    range->busy = true;
    mutex_unlock(&range->mutex);
    return session;

fail_reference_locked:
    --range->references;
fail_locked:
    saved_errno = errno;
    mutex_unlock(&range->mutex);
    free(stream);
    errno = saved_errno;
    return NULL;
}
