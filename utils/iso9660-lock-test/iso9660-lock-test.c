/* Production ISO9660 paths with deterministic pthread interleavings.
   Transport, request scheduling and retained VFS references are host models. */
#include "platform.h"
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#ifndef ISO_SOURCE
#define ISO_SOURCE "../../kernel/arch/dreamcast/fs/fs_iso9660.c"
#endif
#include ISO_SOURCE

#define NI __attribute__((no_instrument_function))
#define CHECK(x) do { if(!(x)) { fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", __func__, __LINE__, #x, errno); exit(1); } } while(0)
static _Thread_local int role;
static _Thread_local int fh_held;
static atomic_int go, progressed, finished;
static int scenario, hook_once;
static unsigned char image[64][2048];
static int direct_reads, bios_reads, stops, read_error, submit_error;
static uint32_t stream_fad;
static size_t stream_offset;
static iso_fd_t *other_fd, *opened;
static int other_result;
static cdrom_request_t *pending_request;
static pthread_t other_thread;
int dbglog_level = -1;

static NI double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static NI void await(atomic_int *flag) {
    double end = now() + 3;
    while(!atomic_load(flag)) {
        if(now() > end) { fputs("FAIL deterministic rendezvous timeout\n", stderr); exit(2); }
        sched_yield();
    }
}
int NI mutex_init(mutex_t *m, int type) { (void)type; return pthread_mutex_init(m, NULL); }
int NI mutex_destroy(mutex_t *m) { return pthread_mutex_destroy(m); }
int NI mutex_lock(mutex_t *m) {
    double end = now() + 3;
    while(pthread_mutex_trylock(m)) {
        if(role == 2 && atomic_load(&go)) atomic_store(&progressed, 1);
        if(now() > end) { fputs("FAIL lock cycle / recursive acquisition\n", stderr); exit(2); }
        sched_yield();
    }
    if(m == &fh_mutex) ++fh_held;
    /* In the old inversion, the metadata thread holds cache before waiting
       for the reader's descriptor lock. In the fix it blocks on cache first. */
    if(role == 2 && scenario == 4 && m == &cache_mutex)
        atomic_store(&progressed, 1);
    return 0;
}
int NI mutex_unlock(mutex_t *m) {
    if(m == &fh_mutex) --fh_held;
    return pthread_mutex_unlock(m);
}
void NI __cyg_profile_func_enter(void *fn, void *caller) {
    (void)caller;
    if(role == 1 && scenario == 4 && fn == (void *)bdread && !hook_once++) {
        atomic_store(&go, 1); await(&progressed);
    }
}
void NI __cyg_profile_func_exit(void *fn, void *caller) {
    (void)caller;
    if(role == 1 && ((scenario >= 1 && scenario <= 3) || (scenario >= 5 && scenario <= 9))
            && fn == ((scenario == 6 || scenario == 8) ? (void *)bdread : (void *)biread) && !hook_once++) {
        atomic_store(&go, 1); await(&progressed);
        if(scenario >= 7) CHECK(atomic_load(&finished));
    }
}

/* Sector and control transport spies. No real MMIO, DMA or firmware. */
static int sectors(void *buffer, uint32_t fad, size_t n) {
    CHECK(!fh_held);
    if(read_error) return read_error;
    CHECK(fad >= 150 && fad - 150 + n <= 64);
    memcpy(buffer, image[fad - 150], n * 2048);
    return ERR_OK;
}
int gdrom_direct_read_sectors_dma(void *b, uint32_t f, size_t n,
    gdrom_direct_sector_type_t t, uint32_t timeout, gdrom_direct_result_t *r) {
    (void)t; CHECK(timeout); ++direct_reads;
    if(r) memset(r, 0, sizeof(*r));
    int e = sectors(b, f, n);
    if(e) { if(r) r->sense_valid = true; errno = EIO; return -1; }
    return 0;
}
int cdrom_bios_read_sectors_ex(void *b, uint32_t f, size_t n, bool dma) {
    CHECK(dma); ++bios_reads; return sectors(b, f, n);
}
int cdrom_result_to_errno(int r) {
    switch(r) { case ERR_OK: return 0; case ERR_TIMEOUT: return ETIMEDOUT;
    case ERR_NO_DISC: return ENODEV; case ERR_DISC_CHG: return ESTALE;
    case ERR_ABORTED: return ECANCELED; case ERR_BUSY: return EBUSY;
    default: return EIO; }
}
int cdrom_sense_to_result(const cdrom_sense_t *s) { (void)s; return read_error ? read_error : ERR_SYS; }
int gdrom_direct_probe(gdrom_direct_probe_result_t *p, uint32_t t) {
    (void)t; memset(p, 0, sizeof(*p)); p->result = ERR_OK; p->status.disc_type = CD_CDROM; return 0;
}
int gdrom_direct_reinitialize(gdrom_direct_reinit_result_t *p, uint32_t t) { return gdrom_direct_probe(&p->probe, t); }
int gdrom_direct_read_toc(cd_toc_t *t, bool h, uint32_t ms, gdrom_direct_result_t *r) { (void)t; (void)ms; (void)r; CHECK(!h); return 0; }
int cdrom_bios_reinit(void) { return 0; }
int cdrom_bios_read_toc(cd_toc_t *t, bool h) { (void)t; CHECK(!h); return 0; }
uint32_t cdrom_locate_data_track(cd_toc_t *t) { (void)t; return 150; }
int cdrom_media_recognize(uint32_t t) { CHECK(t); return 1; }
void cdrom_media_monitor_use_direct(bool d) { (void)d; }
int cdrom_media_event_handler_add(cdrom_media_event_callback_t cb, void *data) { (void)cb; (void)data; return 1; }
int cdrom_media_event_handler_remove(int h) { (void)h; return 0; }
int cdrom_bios_stream_stop(bool abort) { (void)abort; ++stops; CHECK(!fh_held); return 0; }
int cdrom_bios_stream_start(int s, int n, bool dma) {
    CHECK(!fh_held && n > 0 && dma); stream_fad = s; stream_offset = 0; return ERR_OK;
}
int cdrom_bios_stream_request(void *b, size_t n, bool block) {
    (void)block; CHECK(!fh_held && stream_fad >= 150);
    if(scenario == 10 && !hook_once++) { atomic_store(&go, 1); await(&progressed); }
    memcpy(b, image[stream_fad - 150] + stream_offset, n); stream_offset += n;
    return ERR_OK;
}
int cdrom_bios_stream_progress(size_t *n) { *n = 2048 - stream_offset; return 0; }
int thd_poll(thd_cb_t cb, void *d, unsigned t) { (void)t; return cb(d); }
int nmmgr_handler_add(nmmgr_handler_t *h) { (void)h; return 0; }
int nmmgr_handler_remove(nmmgr_handler_t *h) { (void)h; return 0; }

/* Minimal retained-descriptor and request boundary model. The production ISO
   admission, finalizer, callback and close functions are not replaced. */
static iso_fd_t *handles[16];
static unsigned refs[16];
static int callback_count, freed, fast;
static atomic_int finalizer_entered, finalizer_done;
struct cdrom_request {
    cdrom_request_finalizer_t finalizer;
    void *finalizer_data;
    cdrom_request_callback_t callback;
    void *callback_data;
    cdrom_request_status_t status;
    pthread_t worker;
    bool has_worker;
};
static file_t register_fd(iso_fd_t *fd) { handles[1] = fd; refs[1] = 1; return 1; }
file_t fs_dup(file_t f) { CHECK(f == 1 && refs[f]); ++refs[f]; return f; }
vfs_handler_t *fs_get_handler(file_t f) { CHECK(f == 1); return &vh; }
void *fs_get_handle(file_t f) { CHECK(f == 1); return handles[f]; }
int fs_close(file_t f) { CHECK(f == 1 && refs[f]); if(!--refs[f]) { ++freed; iso_close(handles[f]); handles[f] = NULL; } return 0; }
file_t fs_open(const char *p, int flags) { (void)p; (void)flags; return FILEHND_INVALID; }
int fs_ioctl(file_t f, int cmd, ...) { (void)f; (void)cmd; return -1; }
static void *request_run(void *data) {
    cdrom_request_t *r = data;
    role = 3;
    atomic_store(&finalizer_entered, 1);
    r->finalizer(r, &r->status, r->finalizer_data);
    atomic_store(&finalizer_done, 1);
    if(r->callback) r->callback(r, &r->status, r->callback_data);
    return NULL;
}
static cdrom_request_t *submit(size_t bytes, cdrom_request_finalizer_t f, void *d,
    cdrom_request_callback_t cb, void *cd) {
    if(submit_error) { errno = submit_error; return NULL; }
    cdrom_request_t *r = calloc(1, sizeof(*r)); CHECK(r);
    r->finalizer = f; r->finalizer_data = d; r->callback = cb; r->callback_data = cd;
    r->status.state = CDROM_REQUEST_COMPLETE; r->status.data_bytes = bytes;
    if(fast) {
        r->has_worker = true; CHECK(!pthread_create(&r->worker, NULL, request_run, r));
        await(&finalizer_entered);
        /* ISO still owns fh_mutex until it publishes the returned pointer. */
        CHECK(fh_held && !atomic_load(&finalizer_done));
    }
    return r;
}
int cdrom_request_dma_segment_init(cdrom_request_dma_segment_t *s, void *b,
    uint32_t sec, size_t n, size_t off, size_t bytes, bool direct) {
    memset(s, 0, sizeof(*s)); s->buffer = b; s->params.start_sec = sec;
    s->params.num_sec = n; s->io_bytes = n * 2048; s->data_offset = off;
    s->data_bytes = bytes; s->data_direct = direct; return 0;
}
cdrom_request_t *cdrom_request_submit_dma_chain(const cdrom_request_dma_segment_t *s,
    size_t req, size_t bytes, size_t io, uint32_t t, cdrom_request_continue_t c,
    void *cd, cdrom_request_finalizer_t f, void *fd, cdrom_request_callback_t cb, void *d) {
    (void)req; (void)io; (void)t; (void)c; (void)cd;
    CHECK(s->params.start_sec >= 150 && s->params.start_sec - 150 + s->params.num_sec <= 64);
    memcpy(s->buffer, image[s->params.start_sec - 150], s->io_bytes);
    return submit(bytes, f, fd, cb, d);
}
cdrom_request_t *cdrom_request_submit_direct_dma_chain(const cdrom_request_dma_segment_t *s,
    gdrom_direct_sector_type_t type, size_t req, size_t bytes, size_t io, uint32_t t,
    cdrom_request_continue_t c, void *cd, cdrom_request_finalizer_t f, void *fd,
    cdrom_request_callback_t cb, void *d) {
    (void)type; return cdrom_request_submit_dma_chain(s, req, bytes, io, t, c, cd, f, fd, cb, d);
}
cdrom_request_t *cdrom_request_submit_noop(cd_cmd_code_t cmd, size_t req,
    cdrom_request_finalizer_t f, void *fd, cdrom_request_callback_t cb, void *d) {
    (void)cmd; (void)req; return submit(0, f, fd, cb, d);
}
cdrom_request_t *cdrom_request_submit_executor(cd_cmd_code_t cmd, const void *p,
    size_t ps, size_t req, size_t bytes, size_t io, uint32_t t,
    cdrom_request_executor_t ex, cdrom_request_finalizer_t f, void *fd,
    cdrom_request_callback_t cb, void *d) {
    (void)p; (void)ps; (void)bytes; (void)io; (void)t; (void)ex;
    return cdrom_request_submit_noop(cmd, req, f, fd, cb, d);
}
cdrom_request_t *cdrom_seek_async_internal(uint32_t s, uint32_t t,
    cdrom_request_finalizer_t f, void *fd, cdrom_request_callback_t cb, void *d) {
    (void)s; (void)t; return submit(0, f, fd, cb, d);
}
cdrom_request_t *gdrom_direct_seek_async_internal(uint32_t s, uint32_t t,
    gdrom_direct_result_t *r, cdrom_request_finalizer_t f, void *fd,
    cdrom_request_callback_t cb, void *d) { (void)r; return cdrom_seek_async_internal(s,t,f,fd,cb,d); }
struct cdrom_stream_session {
    cdrom_stream_session_finalizer_t finalizer;
    void *data;
    cdrom_stream_session_status_t status;
    pthread_t worker;
};
static void *stream_run(void *data) {
    cdrom_stream_session_t *s = data;
    role = 3; atomic_store(&finalizer_entered, 1);
    s->finalizer(s, &s->status, s->data);
    atomic_store(&finalizer_done, 1); return NULL;
}
cdrom_stream_session_t *cdrom_stream_session_start_internal(uint32_t s, size_t n,
    size_t sz, size_t bytes, uint32_t st, uint32_t it, cdrom_request_backend_t b,
    gdrom_direct_sector_type_t type, cdrom_stream_session_finalizer_t f, void *d) {
    (void)s; (void)n; (void)sz; (void)st; (void)it; (void)b; (void)type;
    if(submit_error) { errno = submit_error; return NULL; }
    cdrom_stream_session_t *session = calloc(1, sizeof(*session)); CHECK(session);
    session->finalizer = f; session->data = d;
    session->status.completed_bytes = bytes;
    if(fast) {
        CHECK(!pthread_create(&session->worker, NULL, stream_run, session));
        await(&finalizer_entered); CHECK(fh_held && !atomic_load(&finalizer_done));
    }
    return session;
}
int cdrom_request_cancel(cdrom_request_t *r) { r->status.state = CDROM_REQUEST_CANCELLED; return 0; }
int cdrom_request_wait(cdrom_request_t *r, uint32_t t, cdrom_request_status_t *s) {
    (void)t; CHECK(!fh_held);
    if(r->has_worker) pthread_join(r->worker, NULL); else request_run(r);
    r->has_worker = false; if(s) *s = r->status; return 0;
}
int cdrom_request_destroy(cdrom_request_t *r) { free(r); return 0; }

static void record(unsigned char *p, const char *name, uint32_t extent, bool dir) {
    iso_dirent_t *d = (iso_dirent_t *)p;
    d->length = 64; d->name_len = (uint8_t)strlen(name);
    memcpy(d->name, name, d->name_len); memcpy(d->extent, &extent, 4);
    uint32_t size = 2048; memcpy(d->size, &size, 4);
    d->flags = dir ? ISO9660_FILE_DIRECTORY : 0;
}
static iso_fd_t *new_fd(uint32_t extent, bool dir) {
    iso_fd_t *fd = aligned_alloc(32, (sizeof(*fd) + 31) & ~(size_t)31); CHECK(fd);
    memset(fd, 0, sizeof(*fd)); fd->first_extent = extent; fd->size = 2048; fd->dir = dir;
    TAILQ_INSERT_TAIL(&iso_fd_queue, fd, next); return fd;
}
static void setup(bool bios) {
    fs_iso9660_init(); CHECK(iso_initialized);
    CHECK(!fs_iso9660_set_backend(bios ? FS_ISO9660_BACKEND_BIOS : FS_ISO9660_BACKEND_DIRECT));
    iso_backend_locked = true; percd_done = true;
    record(image[30], "A", 31, true); record(image[30]+64, "B", 32, true);
    record(image[31], "ONE;1", 40, false); record(image[32], "TWO;1", 41, false);
    record((unsigned char *)&root_dirent, "", 30, true); root_extent = 30; root_size = 2048;
    memset(image[40], 0x5a, 2048);
    memcpy(image[16], "\1CD001", 6); memcpy(image[16]+156, &root_dirent, sizeof(root_dirent));
}
static void *interloper(void *arg) {
    (void)arg; role = 2; await(&go);
    if(scenario == 2) iso_reset();
    else if(scenario == 3) { cdrom_media_event_t e = { .type = CDROM_MEDIA_EVENT_CHANGED }; iso_media_event(&e, NULL); }
    else if(scenario == 7 || scenario == 8) {
        cdrom_request_t *r = fs_iso9660_read_async(1, NULL, 0, 1000, NULL, NULL);
        if(scenario == 8) CHECK(!r && errno == EBUSY);
        else { CHECK(r); CHECK(!cdrom_request_wait(r, 0, NULL)); cdrom_request_destroy(r); }
    }
    else if(scenario == 9) { CHECK(!cdrom_request_wait(pending_request, 0, NULL)); }
    else if(scenario == 6) { unsigned char b; other_result = iso_read(other_fd, &b, 1) != 1; }
    else { struct stat st; other_result = iso_stat(&vh, "B/TWO", &st, 0); }
    atomic_store(&finished, 1); atomic_store(&progressed, 1); return NULL;
}
static void reentry(cdrom_request_t *r, const cdrom_request_status_t *s, void *data) {
    (void)r; (void)s; (void)data; CHECK(!fh_held);
    struct stat st; CHECK(!iso_stat(&vh, "B/TWO", &st, 0)); ++callback_count;
}
int main(int argc, char **argv) {
    CHECK(argc == 3); bool bios = !strcmp(argv[2], "bios"); setup(bios);
    role = 1;
    if(!strcmp(argv[1], "metadata") || !strcmp(argv[1], "reset") || !strcmp(argv[1], "media") || !strcmp(argv[1], "bios-order")) {
        scenario = !strcmp(argv[1], "metadata") ? 1 : !strcmp(argv[1], "reset") ? 2 : !strcmp(argv[1], "media") ? 3 : 4;
        if(scenario == 4) { other_fd = new_fd(41, false); stream_fd = other_fd; }
        CHECK(!pthread_create(&other_thread, NULL, interloper, NULL));
        if(scenario == 4) {
            unsigned char b; iso_fd_t *fd = new_fd(40, false);
            CHECK(iso_read(fd, &b, 1) == 1 && b == 0x5a);
        } else {
            opened = iso_open(&vh, "A/ONE", O_RDONLY); CHECK(opened);
        }
        CHECK(!pthread_join(other_thread, NULL));
        if(scenario == 2 || scenario == 3) CHECK(opened->broken && !percd_done);
        else CHECK(!other_result);
    } else if(!strcmp(argv[1], "bios-stream")) {
        CHECK(bios); scenario = 10;
        CHECK(!pthread_create(&other_thread, NULL, interloper, NULL));
        unsigned char b[32] __attribute__((aligned(32))); iso_fd_t *fd = new_fd(40, false);
        CHECK(iso_read(fd, b, sizeof(b)) == 32 && b[0] == 0x5a && fd->ptr == 32);
        CHECK(!pthread_join(other_thread, NULL)); CHECK(!other_result && stops == 1);
        iso_close(fd);
    } else if(!strcmp(argv[1], "legacy-close")) {
        CHECK(bios); iso_fd_t *fd = new_fd(40, false); stream_fd = fd;
        CHECK(iso_seek(fd, 1, SEEK_SET) == 1 && stops == 1 && !stream_fd);
        stream_fd = fd; CHECK(!iso_close(fd) && stops == 2 && !stream_fd);
    } else if(!strcmp(argv[1], "submit-failure")) {
        file_t f = register_fd(new_fd(40, false));
        unsigned char b[2048] __attribute__((aligned(32))); submit_error = EAGAIN;
        CHECK(!fs_iso9660_read_async(f, b, sizeof(b), 1000, NULL, NULL) && errno == EAGAIN && refs[f] == 1);
        CHECK(!fs_iso9660_read_direct_async(f, b, 1, 1000, NULL, NULL) && errno == EAGAIN && refs[f] == 1);
        CHECK(!fs_iso9660_preseek_async(f, 1000, NULL, NULL) && errno == EAGAIN && refs[f] == 1);
        CHECK(!fs_iso9660_stream_start(f, 1, 1000, 1000) && errno == EAGAIN && refs[f] == 1);
        handles[f]->dir = true;
        CHECK(!fs_iso9660_prefetch_directory_async(f, 1000, NULL, NULL) && errno == EAGAIN && refs[f] == 1);
        CHECK(!handles[f]->async_request && !handles[f]->stream_session); fs_close(f);
    } else if(!strcmp(argv[1], "direct-bypass") || !strcmp(argv[1], "sync-busy") || !strcmp(argv[1], "retire-under-cache")) {
        scenario = !strcmp(argv[1], "direct-bypass") ? 7 : !strcmp(argv[1], "sync-busy") ? 8 : 9;
        CHECK(!bios || scenario == 9);
        file_t f = register_fd(new_fd(40, false));
        if(scenario == 9) { pending_request = fs_iso9660_read_async(f, NULL, 0, 1000, NULL, NULL); CHECK(pending_request); fs_close(f); CHECK(!freed); }
        CHECK(!pthread_create(&other_thread, NULL, interloper, NULL));
        if(scenario == 8) { unsigned char b; CHECK(iso_read(handles[f], &b, 1) == 1); }
        else CHECK(iso_open(&vh, "A/ONE", O_RDONLY));
        CHECK(!pthread_join(other_thread, NULL));
        if(scenario == 9) { CHECK(freed == 1); cdrom_request_destroy(pending_request); } else fs_close(f);
    } else if(!strcmp(argv[1], "readdir") || !strcmp(argv[1], "partial-interleave")) {
        scenario = !strcmp(argv[1], "readdir") ? 5 : 6;
        other_fd = new_fd(41, false);
        CHECK(!pthread_create(&other_thread, NULL, interloper, NULL));
        if(scenario == 5) {
            const dirent_t *de = iso_readdir(new_fd(31, true)); CHECK(de && !strcmp(de->name, "one"));
        } else {
            unsigned char b; CHECK(iso_read(new_fd(40, false), &b, 1) == 1 && b == 0x5a);
        }
        CHECK(!pthread_join(other_thread, NULL)); CHECK(!other_result);
    } else if(!strcmp(argv[1], "errors")) {
        read_error = ERR_TIMEOUT; errno = 0;
        CHECK(!iso_open(&vh, "A/ONE", O_RDONLY) && errno == ETIMEDOUT);
        struct stat st; errno = 0;
        CHECK(iso_stat(&vh, "A/ONE", &st, 0) == -1 && errno == ETIMEDOUT);
        unsigned char b; errno = 0;
        CHECK(iso_read(new_fd(40, false), &b, 1) == -1 && errno == ETIMEDOUT);
        read_error = ERR_NO_DISC;
        CHECK(!iso_open(&vh, "A/ONE", O_RDONLY) && errno == ENODEV && !percd_done);
        int before = direct_reads + bios_reads;
        CHECK(!iso_open(&vh, "A/ONE", O_RDONLY) && errno == ENODEV);
        CHECK(direct_reads + bios_reads == before + 1); /* one failed mount read, no recursive remount */
        read_error = 0; CHECK(iso_open(&vh, "A/ONE", O_RDONLY) && percd_done);
    } else if(!strcmp(argv[1], "stream") || !strcmp(argv[1], "stream-close")) {
        file_t f = register_fd(new_fd(40, false)); fast = !strcmp(argv[1], "stream");
        cdrom_stream_session_t *session = fs_iso9660_stream_start(f, 1, 1000, 1000); CHECK(session);
        if(fast) { CHECK(!pthread_join(session->worker, NULL)); CHECK(!handles[f]->stream_session && handles[f]->ptr == 2048); fs_close(f); }
        else { CHECK(refs[f] == 2); fs_close(f); CHECK(!freed); stream_run(session); }
        CHECK(freed == 1); free(session);
    } else if(!strcmp(argv[1], "prefetch-reset")) {
        file_t f = register_fd(new_fd(31, true));
        cdrom_request_t *r = fs_iso9660_prefetch_directory_async(f, 1000, NULL, NULL); CHECK(r);
        uint32_t gen = directory_snapshot_current_generation(); iso_reset();
        CHECK(directory_snapshot_current_generation() != gen);
        CHECK(!cdrom_request_wait(r, 0, NULL)); CHECK(TAILQ_EMPTY(&directory_snapshots));
        CHECK(handles[f]->broken && !handles[f]->async_request); fs_close(f); cdrom_request_destroy(r);
    } else if(!strcmp(argv[1], "partial")) {
        unsigned char b[3]; iso_fd_t *fd = new_fd(40, false); fd->ptr = 7;
        CHECK(iso_read(fd, b, sizeof(b)) == 3 && b[0] == 0x5a && fd->ptr == 10);
        CHECK(bios ? bios_reads && !direct_reads : direct_reads && !bios_reads && !stops);
    } else if(!strcmp(argv[1], "async") || !strcmp(argv[1], "close-callback") || !strcmp(argv[1], "direct-async") || !strcmp(argv[1], "preseek") || !strcmp(argv[1], "cancel")) {
        file_t f = register_fd(new_fd(40, false)); unsigned char b[2048] __attribute__((aligned(32)));
        fast = strcmp(argv[1], "close-callback") && strcmp(argv[1], "cancel");
        cdrom_request_t *r;
        if(!strcmp(argv[1], "direct-async")) r = fs_iso9660_read_direct_async(f, b, 1, 1000, reentry, NULL);
        else if(!strcmp(argv[1], "preseek")) r = fs_iso9660_preseek_async(f, 1000, reentry, NULL);
        else r = fs_iso9660_read_async(f, b, 2048, 1000, reentry, NULL);
        CHECK(r);
        if(!strcmp(argv[1], "cancel")) CHECK(!cdrom_request_cancel(r));
        if(!fast) { CHECK(refs[f] == 2); fs_close(f); CHECK(!freed); }
        CHECK(!cdrom_request_wait(r, 0, NULL)); CHECK(callback_count == 1);
        if(fast) { CHECK(!handles[f]->async_request); CHECK(handles[f]->ptr == (!strcmp(argv[1], "preseek") ? 0u : 2048u)); fs_close(f); }
        CHECK(freed == 1); cdrom_request_destroy(r);
    } else if(!strcmp(argv[1], "mount")) {
        percd_done = false; opened = iso_open(&vh, "A/ONE", O_RDONLY); CHECK(opened && percd_done);
    } else { fprintf(stderr, "unknown case\n"); return 1; }
    printf("PASS %s %s (production ISO; modeled transport/request/VFS)\n", argv[1], argv[2]);
    return 0;
}
