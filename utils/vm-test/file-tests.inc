/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Two inclusion phases: host platform/backend, then actual VM/VFS tests. */
#ifdef FILE_TEST_VFS_SETUP
#include <kos/fs.h>
#include <kos/mutex.h>
static unsigned source_refs, source_opens, source_closes, source_reads;
static unsigned source_seeks, source_stats, source_totals;
static bool source_published = true;
static int source_close_error, source_open_error, source_stat_error;
static int source_seek_error, source_read_error;
static off_t source_size = 20000;
static int64_t source_total = 20000;
static mode_t source_mode = S_IFREG;
static int seek_delta, read_delta;
static atomic_bool read_park, read_entered, read_resume;
static void (*backend_check)(void);
typedef struct { off_t position; } test_source_t;
static unsigned char source_byte(size_t i) { return (unsigned char)((i * 37 + i / 251) ^ 0x9b); }
static void source_check(void) { if(backend_check) backend_check(); }
static void *source_open(vfs_handler_t *h, const char *path, int mode) {
    (void)h;
    CHECK(!strcmp(path, "/data") && mode == O_RDONLY);
    source_check();
    if(source_open_error) { errno = source_open_error; return NULL; }
    test_source_t *s = test_calloc(1, sizeof(*s));
    if(s) ++source_opens;
    return s;
}
static int source_close(void *h) {
    source_check();
    ++source_closes;
    test_free(h);
    if(source_close_error) { errno = source_close_error; return -1; }
    return 0;
}
static ssize_t source_read(void *h, void *buf, size_t n) {
    test_source_t *s = h;
    CHECK(n > 0 && n <= 4096 && !((uintptr_t)buf & 4095));
    source_check();
    ++source_reads;
    if(atomic_load(&read_park)) {
        atomic_store(&read_entered, true);
        while(!atomic_load(&read_resume)) sched_yield();
    }
    /* Errors may leave private staging partially written. */
    for(size_t i = 0; i < n; ++i) ((unsigned char *)buf)[i] = source_byte(s->position + i);
    s->position += n;
    if(source_read_error) { errno = source_read_error; return -1; }
    return (ssize_t)n + read_delta;
}
static off_t source_seek(void *h, off_t o, int whence) {
    source_check(); ++source_seeks;
    CHECK(o >= 0 && whence == SEEK_SET);
    if(source_seek_error) { errno = source_seek_error; return -1; }
    ((test_source_t *)h)->position = o + seek_delta;
    return o + seek_delta;
}
static int source_stat(void *h, struct stat *st) {
    (void)h; source_check(); ++source_stats;
    if(source_stat_error) { errno = source_stat_error; return -1; }
    *st = (struct stat){.st_mode = source_mode, .st_size = source_size};
    return 0;
}
static uint64_t source_total64(void *h) { (void)h; source_check(); ++source_totals; return source_total; }
static size_t source_sentinel(void *h) { (void)h; return UINT32_MAX; }
static vfs_handler_t source_handler = {
    .nmmgr = {"/source", NMMGR_TYPE_VFS}, .open = source_open,
    .close = source_close, .read = source_read, .seek = source_seek,
    .fstat = source_stat, .total64 = source_total64
};
static char *fs_normalize_path(const char *in, char *out) { strcpy(out, in); return out; }
static nmmgr_handler_t *nmmgr_lookup_ref(const char *path) {
    CHECK(!strcmp(path, "/source/data"));
    if(!source_published) return NULL;
    ++source_refs;
    return &source_handler.nmmgr;
}
static void nmmgr_handler_release(nmmgr_handler_t *h) {
    CHECK(h == &source_handler.nmmgr && source_refs);
    --source_refs;
}
#include "fs.inc"
#else
static vm_file_t *file_under_test;
static vm_space_t *file_space;
static size_t expected_offset, expected_bytes;
static void file_page_check(const unsigned char *p, size_t n) {
    CHECK(n == 4096);
    for(size_t i = 0; i < n; ++i)
        CHECK(p[i] == (i < expected_bytes ? source_byte(expected_offset + i) : 0));
}
static void file_callback_locks(void) {
    CHECK(!pthread_mutex_trylock(&fd_mutex));
    CHECK(!pthread_mutex_unlock(&fd_mutex));
    if(file_space) {
        CHECK(!pthread_mutex_trylock(&file_space->mutex));
        CHECK(!pthread_mutex_unlock(&file_space->mutex));
    }
}
static vm_file_options_t file_options(void) {
    return (vm_file_options_t){.offset = 19, .bytes = 8301,
        .resident_limit = 2, .budget_bytes = 100000};
}
static void file_setup(void) {
    reset_failure();
    CHECK(!source_refs && source_opens == source_closes && !live);
    fs_accepting_descriptors = source_published = true;
    backend_check = file_callback_locks;
    check_page = file_page_check;
    file_space = vm_space_create(context(0), P(BASE), 65536);
    CHECK(file_space);
}
static vm_file_t *file_open_options(vm_file_options_t o) {
    vm_file_t *f = NULL;
    CHECK(!vm_file_open(file_space, "/source/data", &o, &f) && f);
    return f;
}
static void file_end(vm_file_t *f) {
    if(f) { int e = -1; CHECK(!vm_file_close(f, &e) && !e); }
    CHECK(!vm_space_destroy(file_space)); file_space = NULL;
    drop(0); check_page = NULL; backend_check = NULL;
    CHECK(!live && !source_refs && source_opens == source_closes);
    for(unsigned i = 0; i < FD_SETSIZE; ++i) CHECK(!fd_table[i]);
}
static void file_load(vm_file_t *f, size_t page) {
    expected_offset = f->info.offset + page * 4096;
    expected_bytes = vm_file_valid_bytes(f, page);
    CHECK(!vm_file_prefetch(f, page));
    file_page_check(f->region->pages[page].backing, 4096);
}
static void file_basic(void) {
    file_setup();
    vm_file_options_t o = file_options();
    vm_file_t *f = file_open_options(o);
    vm_file_info_t q;
    CHECK(!vm_file_query(f, &q));
    CHECK(q.address == P(BASE) && q.pages == 3 && !q.resident_pages && !q.loading_pages);
    CHECK(q.fixed_bytes == sizeof(*f) + sizeof(vm_region_t) + 3 * sizeof(vm_page_t) + 4096);
    CHECK(q.table_reserve_bytes == 2 * sizeof(mmusubcontext_t) + sizeof(mmusubcontext_t *));
    CHECK(q.charged_bytes == q.fixed_bytes + q.table_reserve_bytes);
    size_t fixed_charge = q.charged_bytes;
    unsigned stats = source_stats, totals = source_totals;
    CHECK(vm_space_destroy(file_space) == -1 && errno == EBUSY);
    CHECK(vm_commit(file_space, (void *)q.address, 4096, VM_READ) == -1 && errno == EBUSY);
    CHECK(vm_protect(file_space, (void *)q.address, 4096, VM_READ) == -1 && errno == EBUSY);
    CHECK(vm_decommit(file_space, (void *)q.address, 4096) == -1 && errno == EBUSY);
    CHECK(vm_release(file_space, (void *)q.address) == -1 && errno == EBUSY);
    vm_fault_t *ticket = (void *)1;
    CHECK(vm_fault_arm(file_space, (void *)q.address, VM_FAULT_ZERO_COMMIT, VM_READ, &ticket) == -1 && errno == EBUSY);
    CHECK(ticket == (void *)1);
    irq_context_t frame = fault_frame();
    unsigned before_read = source_reads;
    CHECK(!resolve(file_space, &frame, 0x40, BASE, file_space->context->asid, 1));
    CHECK(source_reads == before_read);
    file_load(f, 0); file_load(f, 2);
    CHECK(vm_file_prefetch(f, 1) == -1 && errno == EAGAIN);
    unsigned reads = source_reads;
    CHECK(!vm_file_prefetch(f, 0) && source_reads == reads);
    vm_pin_t *p = NULL, *p2 = NULL;
    CHECK(vm_pin_acquire(file_space, (void *)q.address, 4096, VM_PIN_READWRITE, &p) == -1 && errno == EACCES);
    CHECK(!vm_pin_acquire(file_space, (void *)q.address, 4096, VM_PIN_READ, &p));
    CHECK(!vm_pin_acquire(file_space, (void *)q.address, 1, VM_PIN_READ, &p2));
    CHECK(vm_file_evict(f, 0) == -1 && errno == EBUSY);
    int e = 77;
    CHECK(vm_file_close(f, &e) == -1 && errno == EBUSY && e == 77);
    CHECK(!vm_pin_release(p));
    CHECK(vm_file_evict(f, 0) == -1 && errno == EBUSY);
    CHECK(!vm_pin_release(p2));
    CHECK(!vm_file_evict(f, 0));
    p = (void *)1;
    CHECK(vm_pin_acquire(file_space, (void *)q.address, 1, VM_PIN_READ, &p) == -1 && p == (void *)1);
    CHECK(!vm_file_evict(f, 2));
    uint32_t seed = 0x349f;
    for(unsigned i = 0; i < 40; ++i) {
        seed = seed * 1664525u + 1013904223u;
        size_t page = (seed >> 16) % 3;
        file_load(f, page);
        CHECK(!vm_file_query(f, &q) && q.charged_bytes == fixed_charge + 4096);
        CHECK(!vm_file_evict(f, page));
    }
    CHECK(source_stats == stats && source_totals == totals);
    /* Unpublish prevents new opens, while the retained backend remains usable.
       The platform name-manager substitute models retention, not its drain. */
    source_published = false;
    CHECK(fs_open("/source/data", O_RDONLY) == FILEHND_INVALID && errno == ENOENT);
    CHECK(source_refs == 1);
    file_load(f, 1); CHECK(!vm_file_cancel(f));
    CHECK(vm_file_prefetch(f, 1) == -1 && errno == ECANCELED);
    CHECK(!vm_file_evict(f, 1)); file_end(f);
}
static void file_open_fail(vm_file_options_t o, int error) {
    vm_file_t *f = (void *)1;
    unsigned before = live;
    CHECK(vm_file_open(file_space, "/source/data", &o, &f) == -1 && errno == error);
    CHECK(f == (void *)1 && live == before && !source_refs);
    CHECK(source_opens == source_closes && !file_space->regions);
}
static void file_open_failures(void) {
    file_setup(); vm_file_options_t o = file_options();
    vm_file_options_t bad = o; bad.bytes = SIZE_MAX; file_open_fail(bad, EOVERFLOW);
    bad = o; bad.offset = UINT64_MAX; file_open_fail(bad, EOVERFLOW);
    bad = o; bad.resident_limit = 4; file_open_fail(bad, EINVAL);
    bad = o; bad.requested = P(BASE + 1); file_open_fail(bad, EINVAL);
    bad = o; bad.resident_limit = 0; file_open_fail(bad, EINVAL);
    bad = o; bad.budget_bytes = 1; file_open_fail(bad, EAGAIN);
    bad = o; bad.bytes = 21000; file_open_fail(bad, EINVAL);
    source_mode = S_IFDIR; file_open_fail(o, ENOTSUP); source_mode = S_IFREG;
    source_size = -1; file_open_fail(o, EINVAL); source_size = 20000;
    source_size = (off_t)INT32_MAX + 1; file_open_fail(o, EOVERFLOW); source_size = 20000;
    source_total = 19999; file_open_fail(o, EIO); source_total = 20000;
    source_handler.total64 = NULL; source_handler.total = source_sentinel;
    file_open_fail(o, EIO); source_handler.total = NULL; source_handler.total64 = source_total64;
    source_close_error = EIO; source_stat_error = ENOSYS;
    file_open_fail(o, ENOSYS); source_close_error = source_stat_error = 0;
    source_open_error = EACCES; file_open_fail(o, EACCES); source_open_error = 0;
    fs_accepting_descriptors = false; file_open_fail(o, ENODEV); fs_accepting_descriptors = true;
    /* Object, backend, VFS wrapper, staging and reservation allocation failures. */
    for(unsigned i = 1; i <= 5; ++i) {
        reset_failure(); fail_at = i; file_open_fail(o, ENOMEM);
    }
    reset_failure();
    vm_file_t *f = file_open_options(o);
    vm_file_info_t info; CHECK(!vm_file_query(f, &info));
    size_t envelope = info.charged_bytes + 4096 * o.resident_limit;
    int ce; CHECK(!vm_file_close(f, &ce));
    o.budget_bytes = envelope - 1; file_open_fail(o, EAGAIN);
    o.budget_bytes = envelope; f = file_open_options(o);
    vm_file_t *unchanged = (void *)1;
    vm_file_options_t overlap = o; overlap.requested = P(BASE);
    CHECK(vm_file_open(file_space, "/source/data", &overlap, &unchanged) < 0 && errno == EEXIST);
    CHECK(unchanged == (void *)1 && source_refs == 1);
    int fd = f->fd;
    /* Generic VFS negative return before detach is deliberately distinct. */
    fail_lock = 1; CHECK(fs_close(fd) == -1 && errno == EAGAIN); fail_lock = 0;
    CHECK(fd_table[fd] && source_refs == 1);
    source_close_error = EIO; int e = -1;
    CHECK(!vm_file_close(f, &e) && e == EIO && !fd_table[fd] && !source_refs);
    source_close_error = 0;
    file_t next = fs_open("/source/data", O_RDONLY); CHECK(next == fd);
    CHECK(!fs_close(next)); /* Never retry the consumed old fd. */
    file_end(NULL);
}
static void file_failed_load(vm_file_t *f, int error) {
    unsigned before = live; size_t charge = f->info.charged_bytes;
    CHECK(vm_file_prefetch(f, 0) == -1 && errno == error);
    CHECK(live == before && f->info.charged_bytes == charge && !f->info.loading_pages);
    vm_file_page_info_t q;
    CHECK(!vm_file_page_query(f, 0, &q) && q.state == VM_FILE_ABSENT);
    expect(file_space, BASE, VM_RESERVED, 12288, 0);
}
static void file_load_failures(void) {
    file_setup(); vm_file_t *f = file_open_options(file_options());
    expected_offset = 19; expected_bytes = 4096;
    for(unsigned i = 1; i <= 3; ++i) {
        reset_failure(); fail_at = i; file_failed_load(f, ENOMEM);
    }
    reset_failure();
    source_seek_error = ESPIPE; file_failed_load(f, ESPIPE); source_seek_error = 0;
    seek_delta = 1; file_failed_load(f, EIO); seek_delta = 0;
    source_read_error = ENODEV; file_failed_load(f, ENODEV); source_read_error = 0;
    int deltas[] = {-4096, -1, 1};
    for(unsigned i = 0; i < 3; ++i) { read_delta = deltas[i]; file_failed_load(f, EIO); }
    read_delta = 0;
    source_handler.read = NULL; file_failed_load(f, EINVAL); source_handler.read = source_read;
    file_load(f, 0); file_end(f);
}
static void cancel_file(void) { CHECK(!vm_file_cancel(file_under_test)); }
static void file_cancel_boundaries(void) {
    for(unsigned stage = 0; stage < 3; ++stage) {
        file_setup(); file_under_test = file_open_options(file_options());
        expected_offset = 19; expected_bytes = 4096;
        if(stage == 0) CHECK(!vm_file_cancel(file_under_test));
        if(stage == 1) allocation_inject = cancel_file;
        if(stage == 2) purge_inject = cancel_file;
        file_failed_load(file_under_test, ECANCELED);
        purge_inject = NULL; file_end(file_under_test); file_under_test = NULL;
    }
}
static void *file_worker(void *arg) {
    int rc = vm_file_prefetch(arg, 0); int error = errno;
    CHECK(rc == -1 && error == ECANCELED); return NULL;
}
static void file_concurrent(void) {
    file_setup(); vm_file_t *f = file_open_options(file_options());
    file_load(f, 2);
    expected_offset = 19; expected_bytes = 4096;
    atomic_store(&read_park, true); atomic_store(&read_entered, false); atomic_store(&read_resume, false);
    pthread_t worker; CHECK(!pthread_create(&worker, NULL, file_worker, f));
    while(!atomic_load(&read_entered)) sched_yield();
    vm_file_info_t q; vm_file_page_info_t page;
    CHECK(!vm_file_query(f, &q) && q.loading_pages == 1 && q.resident_pages == 1);
    CHECK(q.charged_bytes == q.fixed_bytes + q.table_reserve_bytes + 8192);
    CHECK(!vm_file_page_query(f, 0, &page) && page.state == VM_FILE_LOADING);
    CHECK(vm_file_prefetch(f, 0) == -1 && errno == EBUSY);
    CHECK(vm_file_prefetch(f, 1) == -1 && errno == EBUSY);
    CHECK(vm_file_evict(f, 0) == -1 && errno == EBUSY);
    CHECK(vm_file_prefetch(f, 2) == -1 && errno == EBUSY); /* READY does not bypass active. */
    CHECK(!vm_file_evict(f, 2));
    CHECK(!vm_file_evict(f, 1));
    irq_context_t frame = fault_frame(); unsigned before_read = source_reads;
    CHECK(!resolve(file_space, &frame, 0x40, BASE, file_space->context->asid, 1));
    CHECK(source_reads == before_read);
    int e = 19; CHECK(vm_file_close(f, &e) == -1 && errno == EBUSY && e == 19);
    CHECK(!vm_file_cancel(f));
    CHECK(vm_file_close(f, &e) == -1 && errno == EBUSY);
    atomic_store(&read_resume, true); CHECK(!pthread_join(worker, NULL));
    atomic_store(&read_park, false);
    CHECK(!vm_file_query(f, &q) && !q.loading_pages && !q.resident_pages);
    file_end(f);
}
static void file_callers(void) {
    file_setup(); vm_file_t *f = file_open_options(file_options());
    for(unsigned mode = 0; mode < 3; ++mode) {
        test_irq = mode == 0; test_sr = mode == 1 ? 0x10000000 : mode == 2 ? 0x10 : 0;
        vm_file_t *out = (void *)1; vm_file_info_t q; memset(&q, 0xa5, sizeof(q));
        vm_file_info_t saved = q; vm_file_page_info_t p = {VM_FILE_READY, 999}; int e = 99;
        CHECK(vm_file_open(NULL, NULL, NULL, &out) == -1 && errno == EPERM && out == (void *)1);
        CHECK(vm_file_query(f, &q) == -1 && errno == EPERM && !memcmp(&q, &saved, sizeof(q)));
        CHECK(vm_file_page_query(f, 0, &p) == -1 && errno == EPERM && p.valid_bytes == 999);
        CHECK(vm_file_prefetch(f, 0) == -1 && errno == EPERM);
        CHECK(vm_file_evict(f, 0) == -1 && errno == EPERM);
        CHECK(vm_file_cancel(f) == -1 && errno == EPERM);
        CHECK(vm_file_close(f, &e) == -1 && errno == EPERM && e == 99);
    }
    test_irq = 0; test_sr = 0; file_end(f);
}
static void file_tests(void) {
    file_basic(); file_open_failures(); file_load_failures();
    file_cancel_boundaries(); file_concurrent(); file_callers();
    puts("VM4 production VFS/residency/budget/cancel/pin/close PASS");
}
#endif
