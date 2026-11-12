/* Original synchronous KOS VFS adapter. Descriptor ownership stays with caller. */
#include <kosdec/adx_file.h>
#include <errno.h>
#include <string.h>
static struct kosdec_adx_input_result copy_cached(struct kosdec_adx_file *f,
    uint32_t offset, uint8_t *dst, size_t bytes) {
    size_t within = offset - f->cache_offset;
    size_t available = f->cache_used - within;
    if(bytes > available) bytes = available;
    memcpy(dst, f->cache + within, bytes);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, bytes};
}
static struct kosdec_adx_input_result read_at(void *context, uint32_t offset,
                                             uint8_t *dst, size_t bytes) {
    struct kosdec_adx_file *f = context;
    struct kosdec_adx_input_result r = {KOSDEC_ADX_INPUT_ERROR, 0};
    size_t request = bytes;
    uint8_t *target = dst;
    if(f->cache) {
        if(offset >= f->cache_offset && offset - f->cache_offset < f->cache_used)
            return copy_cached(f, offset, dst, bytes);
        f->cache_used = 0;
        if(offset >= f->stream.input_bytes) {
            if(offset == f->stream.input_bytes) r.status = KOSDEC_ADX_INPUT_END;
            return r;
        }
        request = f->stream.input_bytes - offset;
        if(request > f->cache_capacity) request = f->cache_capacity;
        target = f->cache;
    }
    _off64_t position = fs_seek64(f->descriptor, (_off64_t)offset, SEEK_SET);
    if(position != (_off64_t)offset) {
        f->io_errno = position < 0 ? errno : EIO;
        return r;
    }
    ssize_t got = fs_read(f->descriptor, target, request);
    if(got < 0) {
        f->io_errno = errno;
        if(errno == EINTR || errno == EAGAIN) r.status = KOSDEC_ADX_INPUT_WAIT;
    }
    else if((size_t)got > request) {
        f->io_errno = EIO;
    }
    else if(got && f->cache) {
        f->cache_offset = offset;
        f->cache_used = (size_t)got;
        return copy_cached(f, offset, dst, bytes);
    }
    else {
        r.status = got ? KOSDEC_ADX_INPUT_DATA : KOSDEC_ADX_INPUT_END;
        r.bytes = (size_t)got;
    }
    return r;
}
enum kosdec_adx_status kosdec_adx_file_init(struct kosdec_adx_file *f,
                                         file_t descriptor, bool repeat) {
    if(!f) return KOSDEC_ADX_BAD_ARGUMENT;
    *f = (struct kosdec_adx_file){.descriptor = descriptor};
    kosdec_adx_stream_init(&f->stream, read_at, f, 0, repeat);
    int64_t bytes = fs_total64(descriptor);
    if(bytes < 0 || (uint64_t)bytes > UINT32_MAX) {
        f->io_errno = bytes < 0 ? errno : EOVERFLOW;
        f->stream.terminal = KOSDEC_ADX_IO_ERROR;
        return f->stream.terminal;
    }
    f->stream.input_bytes = (uint32_t)bytes;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_file_init_buffered(struct kosdec_adx_file *f,
    file_t descriptor, bool repeat, uint8_t *cache, size_t cache_bytes) {
    if(!f || !cache || cache_bytes < 512 || cache_bytes > 32768)
        return KOSDEC_ADX_BAD_ARGUMENT;
    enum kosdec_adx_status status = kosdec_adx_file_init(f, descriptor, repeat);
    if(status == KOSDEC_ADX_MORE) {
        f->cache = cache;
        f->cache_capacity = cache_bytes;
    }
    return status;
}
