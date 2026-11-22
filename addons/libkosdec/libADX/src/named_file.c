/* Original private-descriptor lifecycle; no global VFS/Audio state changes. */
#include <kosdec/adx_named_file.h>
#include <kosdec/adx_file.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct kosdec_adx_named_file {
    struct kosdec_adx_file input;
    kthread_t *owner;
    bool loaned, closing;
    struct kosdec_adx_source *source_loan;
};
static int error(int value) { errno = value; return -1; }
static int caller(void) {
    uint32_t sr;
#ifdef KOSDEC_ADX_NAMED_HOST_TEST
    extern uint32_t kosdec_adx_named_test_sr(void);
    sr = kosdec_adx_named_test_sr();
#else
    __asm__ volatile("stc sr, %0" : "=r"(sr));
#endif
    if(irq_inside_int() || (sr & 0x100000f0u) || !thd_get_current())
        return error(EPERM);
    return 0;
}
static int owner(struct kosdec_adx_named_file *f) {
    if(caller() < 0) return -1;
    if(!f) return error(EINVAL);
    if(f->owner != thd_get_current()) return error(EPERM);
    if(f->closing) return error(EBUSY);
    return 0;
}
int kosdec_adx_named_file_open(struct kosdec_adx_named_file **out,
    const char *path, bool repeat, uint8_t *cache, size_t cache_bytes,
    int *cleanup_error) {
    if(caller() < 0) return -1;
    if(!out || *out || !path || !*path || !cleanup_error ||
       (cache ? cache_bytes < 512 || cache_bytes > 32768 : cache_bytes != 0))
        return error(EINVAL);
    if(strnlen(path, PATH_MAX) == PATH_MAX)
        return error(ENAMETOOLONG);
    *cleanup_error = 0;
    struct kosdec_adx_named_file *f = calloc(1, sizeof(*f));
    if(!f) return error(ENOMEM);
    f->owner = thd_get_current();
    file_t fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) {
        int primary = errno;
        free(f);
        return error(primary);
    }
    enum kosdec_adx_status status = cache ?
        kosdec_adx_file_init_buffered(&f->input, fd, repeat, cache, cache_bytes) :
        kosdec_adx_file_init(&f->input, fd, repeat);
    if(status != KOSDEC_ADX_MORE) {
        int primary = f->input.io_errno;
        int cleanup = fs_close(fd) < 0 ? errno : 0;
        free(f);
        *cleanup_error = cleanup;
        return error(primary);
    }
    *out = f;
    return 0;
}
int kosdec_adx_named_file_borrow(struct kosdec_adx_named_file *f,
    struct kosdec_adx_stream **stream) {
    if(owner(f) < 0) return -1;
    if(!stream || *stream) return error(EINVAL);
    if(f->loaned) return error(EBUSY);
    f->loaned = true;
    *stream = &f->input.stream;
    return 0;
}
int kosdec_adx_named_file_return(struct kosdec_adx_named_file *f,
    struct kosdec_adx_stream **stream) {
    if(owner(f) < 0) return -1;
    if(!stream || !f->loaned || f->source_loan || *stream != &f->input.stream)
        return error(EINVAL);
    f->loaned = false;
    *stream = NULL;
    return 0;
}
static struct kosdec_adx_input_result source_read(void *context,uint32_t offset,
                                                uint8_t *dst,size_t capacity) {
    struct kosdec_adx_named_file *f=context;
    if(!f->source_loan || offset>f->input.stream.input_bytes || (!dst && capacity))
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(offset==f->input.stream.input_bytes)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
    if(capacity>f->input.stream.input_bytes-offset) capacity=f->input.stream.input_bytes-offset;
    if(capacity>512) capacity=512;
    if(!capacity) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    return f->input.stream.read_at(f->input.stream.context,offset,dst,capacity);
}
int kosdec_adx_named_file_borrow_source(struct kosdec_adx_named_file *f,
    struct kosdec_adx_source *out,uint32_t tag) {
    if(owner(f)<0) return -1;
    if(!out || out->read_at) return error(EINVAL);
    if(f->loaned) return error(EBUSY);
    *out=(struct kosdec_adx_source){.read_at=source_read,.context=f,
        .bytes=f->input.stream.input_bytes,.tag=tag};
    f->source_loan=out;
    f->loaned=true;
    return 0;
}
int kosdec_adx_named_file_return_source(struct kosdec_adx_named_file *f,
    struct kosdec_adx_source *loan) {
    if(owner(f)<0) return -1;
    if(!loan || f->source_loan!=loan || !f->loaned || loan->read_at!=source_read ||
       loan->context!=f || loan->bytes!=f->input.stream.input_bytes) return error(EINVAL);
    f->source_loan=NULL;
    f->loaned=false;
    *loan=(struct kosdec_adx_source){0};
    return 0;
}
int kosdec_adx_named_file_close(struct kosdec_adx_named_file **file,
    int *cleanup_error) {
    if(!file || !cleanup_error) return error(EINVAL);
    struct kosdec_adx_named_file *f = *file;
    if(owner(f) < 0) return -1;
    if(f->loaned) return error(EBUSY);
    f->closing = true;
    *file = NULL;
    int cleanup = fs_close(f->input.descriptor) < 0 ? errno : 0;
    free(f);
    *cleanup_error = cleanup;
    return 0;
}
