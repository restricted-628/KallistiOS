/* Original private-descriptor lifecycle; no global VFS/Audio state changes. */
#include <kosdec/adx_named_file.h>
#include <kosdec/adx_file.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

struct kosdec_adx_named_file {
    struct kosdec_adx_file input;
    kthread_t *owner;
    bool loaned, closing;
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
    if(!stream || !f->loaned || *stream != &f->input.stream)
        return error(EINVAL);
    f->loaned = false;
    *stream = NULL;
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
