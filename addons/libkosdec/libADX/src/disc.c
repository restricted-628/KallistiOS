/* Original staged direct-disc provider; no proprietary runtime dependency. */
#include <kosdec/adx_disc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define DISC_MAGIC UINT32_C(0x41445844)
#define SECTOR KOSDEC_ADX_DISC_SECTOR_BYTES

static void fail(struct kosdec_adx_disc *d, int error) {
    if(!d->io_errno) d->io_errno = error ? error : EIO;
    d->cache_bytes = 0;
}

enum kosdec_adx_status kosdec_adx_disc_init(
    struct kosdec_adx_disc *d, cdrom_sector_range_t *range, uint64_t offset,
    uint32_t bytes, void *buffer, size_t capacity, uint32_t timeout) {
    if(!d || (d->magic && (d->magic != DISC_MAGIC || !d->closed)) ||
       !range || !buffer || ((uintptr_t)buffer & 31u) || !bytes || !timeout ||
       capacity < SECTOR || capacity > KOSDEC_ADX_DISC_MAX_BUFFER_BYTES ||
       capacity % SECTOR) return KOSDEC_ADX_BAD_ARGUMENT;
    cdrom_sector_range_info_t info;
    if(cdrom_sector_range_get_info(range, &info) < 0) return KOSDEC_ADX_IO_ERROR;
    if(info.backend != CDROM_REQUEST_BACKEND_DIRECT) return KOSDEC_ADX_UNSUPPORTED;
    uint64_t sector_count = info.sector_count;
    if(sector_count > UINT64_MAX / SECTOR) return KOSDEC_ADX_BAD_ARGUMENT;
    uint64_t extent = sector_count * SECTOR;
    if(offset > extent || bytes > extent - offset) return KOSDEC_ADX_BAD_ARGUMENT;
    *d = (struct kosdec_adx_disc){
        .magic = DISC_MAGIC, .range = range, .buffer = buffer,
        .buffer_bytes = capacity, .source_offset = offset,
        .input_bytes = bytes, .timeout = timeout
    };
    return KOSDEC_ADX_MORE;
}

/* Never release or publish staging while request ownership is unresolved. */
static enum kosdec_adx_status retire(struct kosdec_adx_disc *d) {
    cdrom_request_status_t s;
    if(cdrom_request_get_status(d->request, &s) < 0) {
        fail(d, errno);
        return KOSDEC_ADX_IO_ERROR;
    }
    if(s.state == CDROM_REQUEST_QUEUED || s.state == CDROM_REQUEST_RUNNING)
        return KOSDEC_ADX_NEED_INPUT;
    if(cdrom_request_destroy(d->request) < 0) {
        if(errno == EBUSY) return KOSDEC_ADX_NEED_INPUT;
        fail(d, errno);
        return KOSDEC_ADX_IO_ERROR;
    }
    d->request = NULL;
    d->cancel_requested = false;
    if(d->closing) return KOSDEC_ADX_DONE;
    if(s.state != CDROM_REQUEST_COMPLETE ||
       s.backend != CDROM_REQUEST_BACKEND_DIRECT ||
       s.completed_bytes != d->pending_bytes || s.data_bytes != d->pending_bytes) {
        fail(d, s.error);
        return KOSDEC_ADX_IO_ERROR;
    }
    if(d->io_errno) return KOSDEC_ADX_IO_ERROR;
    d->cache_offset = d->pending_offset;
    d->cache_bytes = d->pending_bytes;
    return KOSDEC_ADX_MORE;
}

struct kosdec_adx_input_result kosdec_adx_disc_read_at(
    void *context, uint32_t offset, uint8_t *dst, size_t capacity) {
    struct kosdec_adx_disc *d = context;
    struct kosdec_adx_input_result r = {KOSDEC_ADX_INPUT_ERROR, 0};
    if(!d || d->magic != DISC_MAGIC || d->closing || d->closed ||
       d->io_errno || offset > d->input_bytes || (capacity && !dst)) return r;
    if(offset == d->input_bytes) {
        r.status = KOSDEC_ADX_INPUT_END;
        return r;
    }
    r.status = KOSDEC_ADX_INPUT_WAIT;
    if(!capacity) return r;
    bool polled = d->request != NULL;
    if(polled) {
        enum kosdec_adx_status status = retire(d);
        if(status == KOSDEC_ADX_IO_ERROR) r.status = KOSDEC_ADX_INPUT_ERROR;
        if(status != KOSDEC_ADX_MORE) return r;
    }
    uint64_t absolute = d->source_offset + offset;
    if(absolute >= d->cache_offset && absolute - d->cache_offset < d->cache_bytes) {
        size_t at = (size_t)(absolute - d->cache_offset);
        size_t bytes = d->cache_bytes - at;
        if(bytes > capacity) bytes = capacity;
        if(bytes > 512) bytes = 512;
        if(bytes > d->input_bytes - offset) bytes = d->input_bytes - offset;
        memcpy(dst, d->buffer + at, bytes);
        r.status = KOSDEC_ADX_INPUT_DATA;
        r.bytes = bytes;
        return r;
    }
    if(polled) return r; /* Do not submit new I/O in a retirement call. */
    uint64_t sector = absolute / SECTOR;
    uint64_t last = (d->source_offset + d->input_bytes - 1) / SECTOR;
    size_t sectors = d->buffer_bytes / SECTOR;
    if(last - sector + 1 < sectors) sectors = (size_t)(last - sector + 1);
    d->cache_bytes = 0;
    int64_t position = cdrom_sector_range_seek(d->range, (int64_t)sector, SEEK_SET);
    if(position != (int64_t)sector) {
        fail(d, position < 0 ? errno : EIO);
        r.status = KOSDEC_ADX_INPUT_ERROR;
        return r;
    }
    d->pending_offset = sector * SECTOR;
    d->pending_bytes = sectors * SECTOR;
    d->request = cdrom_sector_range_read_async(d->range, d->buffer, sectors,
                                              d->timeout, NULL, NULL);
    if(!d->request) {
        fail(d, errno);
        r.status = KOSDEC_ADX_INPUT_ERROR;
    }
    return r;
}

enum kosdec_adx_status kosdec_adx_disc_close(struct kosdec_adx_disc *d) {
    if(!d || d->magic != DISC_MAGIC) return KOSDEC_ADX_BAD_ARGUMENT;
    if(d->closed) return KOSDEC_ADX_DONE;
    d->closing = true;
    d->cache_bytes = 0;
    if(d->request) {
        if(!d->cancel_requested) {
            if(cdrom_request_cancel(d->request) == 0) d->cancel_requested = true;
            else fail(d, errno);
        }
        enum kosdec_adx_status status = retire(d);
        if(d->request) return status;
    }
    d->closed = true;
    return KOSDEC_ADX_DONE;
}
