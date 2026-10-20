/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/vm.h>
#include <kos/fiber.h>
#include <dc/memory.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDRESS 0x101ff000u
#define BYTES 4096u
#define COOKIE 0x564d3437u
#ifndef VM_FILE_FATAL
#define VM_FILE_FATAL 0
#endif
static volatile uint32_t *const data = (void *)ADDRESS;
static mmucontext_t *context;
static vm_space_t *space;
static vm_file_t *file;
static vm_pin_t *pin;
static kfiber_t *parent;
static alignas(32) unsigned char stack[8192];
static unsigned failures, loads, yields, abandoned, transfers, reloads;
static atomic_uint reads, closes, parent_returned, blocked;
static semaphore_t entered, resume;
static bool delay_read;
static kthread_t *worker;
static int worker_result, worker_errno;
static void failed(unsigned line, const char *expression) {
    int saved = errno; ++failures;
    printf("VM4DISC CHECK line=%u errno=%d: %s\n", line, saved, expression);
    errno = saved;
}
#define VERIFY(x) do { if(!(x)) failed(__LINE__, #x); } while(0)
#define REQUIRE(x) do { if(!(x)) { failed(__LINE__, #x); goto cleanup; } } while(0)
/* Seed only the separately owned preflight page in replaceable UTLB slot 0.
   Some runtimes enable general translation only after a non-SQ LDTLB. A
   software page-table entry alone then cannot provoke the first TLB miss.
   The normal checked unmap below retires this seed before backing is freed;
   no VM-managed page is preloaded, so later VM accesses require normal refill. */
static void preload_preflight(void) {
    volatile uint32_t *const pteh = (void *)0xff000000u;
    volatile uint32_t *const ptel = (void *)0xff000004u;
    volatile uint32_t *const mmucr = (void *)0xff000010u;
    unsigned vpn = ADDRESS >> 12;
    const mmupage_t *page = &context->sub[vpn / MMU_SUB_PAGES]->page[vpn % MMU_SUB_PAGES];
    irq_mask_t old = irq_disable();
    uint32_t saved_h = *pteh, saved_l = *ptel, saved_control = *mmucr;
    *mmucr = saved_control & ~(63u << 10);
    *pteh = page->pteh | (unsigned)context->asid;
    *ptel = page->ptel;
    __asm__ volatile("ldtlb");
    *mmucr = saved_control;
    *pteh = saved_h;
    *ptel = saved_l;
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
    irq_restore(old);
}

/* This preflight uses separately owned low-level backing before VM admission.
   It distinguishes general translated execution from SQ-only emulation. */
static int translated_preflight(void) {
    uint32_t *backing = aligned_alloc(BYTES, BYTES);
    if(!backing) return -1;
    backing[0] = COOKIE;
    dcache_purge_range((uintptr_t)backing, BYTES);
    int rc = mmu_page_map_ex(context, ADDRESS >> 12,
            ((uintptr_t)backing & MEM_AREA_CACHE_MASK) >> 12, 1,
            MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true);
    if(rc < 0) { free(backing); return -1; }
    printf("VM4DISC preflight backing=%08lx\n", (unsigned long)backing);
    preload_preflight();
    int translated = data[0] == COOKIE;
    if(translated) {
        uint32_t *replacement = aligned_alloc(BYTES, BYTES);
        if(!replacement) {
            if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
                arch_panic("resident-file preflight retirement failed");
            free(backing);
            return -1;
        }
        replacement[0] = ~COOKIE;
        dcache_purge_range((uintptr_t)replacement, BYTES);
        rc = mmu_page_map_ex(context, ADDRESS >> 12,
                ((uintptr_t)replacement & MEM_AREA_CACHE_MASK) >> 12, 1,
                MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true);
        if(rc == 0) {
            uint32_t observed = data[0];
            printf("VM4DISC preflight remap expected=%08lx observed=%08lx\n",
                   (unsigned long)~COOKIE, (unsigned long)observed);
            VERIFY(observed == ~COOKIE);
            if(observed != ~COOKIE)
                translated = -1;
            /* The replacement retired the old mapping before freeing it. */
            free(backing);
            backing = replacement;
        }
        else {
            free(replacement);
            VERIFY(rc == 0);
            translated = -1;
        }
    }
    if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
        arch_panic("resident-file preflight retirement failed");
    free(backing);
    return translated;
}

/* Original deterministic bytes stored as VM4DATA.BIN in the disc image. */
static unsigned char pattern(size_t i) { return (unsigned char)((i * 37 + i / 251) ^ 0x9b); }
/* Synthetic delay only: bytes/stat/seek still delegate to real ISO9660 /cd.
   Retain the underlying direct-backend open until final callback. */
static void *delayed_open(vfs_handler_t *h, const char *path, int mode) {
    (void)h;
    if(strcmp(path, "/data")) { errno = ENOENT; return NULL; }
    file_t fd = fs_open("/cd/VM4DATA.BIN", mode);
    return fd == FILEHND_INVALID ? NULL : (void *)(uintptr_t)(fd + 1);
}
static file_t inner(void *h) { return (file_t)((uintptr_t)h - 1); }
static int delayed_close(void *h) { atomic_fetch_add(&closes, 1); return fs_close(inner(h)); }
static off_t delayed_seek(void *h, off_t pos, int whence) { return fs_seek(inner(h), pos, whence); }
static int delayed_stat(void *h, struct stat *st) { return fs_fstat(inner(h), st); }
static uint64_t delayed_total(void *h) { return fs_total64(inner(h)); }
static ssize_t delayed_read(void *h, void *out, size_t n) {
    VERIFY(((uintptr_t)out & 0xe0000000u) == 0x80000000u);
    VERIFY(!((uintptr_t)out & 4095) && n <= 4096);
    vm_file_info_t q;
    VERIFY(file && !vm_file_query(file, &q) && q.loading_pages == 1);
    printf("VM4DISC IO_READ count=%u\n", atomic_fetch_add(&reads, 1) + 1);
    if(delay_read) { sem_signal(&entered); sem_wait(&resume); }
    return fs_read(inner(h), out, n);
}
static vfs_handler_t handler = {
    .nmmgr = {.pathname = "/vmdelay", .version = 0x10000,
        .type = NMMGR_TYPE_VFS, .list_ent = NMMGR_LIST_INIT},
    .open = delayed_open, .close = delayed_close, .read = delayed_read,
    .seek = delayed_seek, .fstat = delayed_stat, .total64 = delayed_total
};
static void check_page(size_t page) {
    size_t valid = page == 2 ? 109 : BYTES;
    volatile const unsigned char *p = (void *)(ADDRESS + page * BYTES);
    for(size_t i = 0; i < BYTES; ++i)
        VERIFY(p[i] == (i < valid ? pattern(19 + page * BYTES + i) : 0));
}
static int pin_page(size_t page) {
    return vm_pin_acquire(space, (void *)(ADDRESS + page * BYTES), BYTES, VM_PIN_READ, &pin);
}
static void pinned_child(void *unused) {
    (void)unused;
    VERIFY(!vm_file_prefetch(file, 0)); ++loads;
    VERIFY(!pin_page(0)); check_page(0); ++yields;
    VERIFY(!fiber_switch(parent)); check_page(0); ++yields;
    VERIFY(!vm_pin_release(pin)); pin = NULL;
}
static void *unblock(void *unused) {
    (void)unused; sem_wait(&entered);
    VERIFY(!atomic_load(&parent_returned));
    vm_file_page_info_t q;
    VERIFY(!vm_file_page_query(file, 0, &q) && q.state == VM_FILE_LOADING);
    VERIFY(vm_file_prefetch(file, 1) < 0 && errno == EBUSY);
    int error = 88; VERIFY(vm_file_close(file, &error) < 0 && errno == EBUSY && error == 88);
    atomic_fetch_add(&blocked, 1); sem_signal(&resume); return NULL;
}
static void *load_worker(void *unused) {
    (void)unused;
    worker_result = vm_file_prefetch(file, 0); worker_errno = errno;
    return NULL;
}
static void waiting_child(void *unused) {
    (void)unused;
    worker = thd_create(false, load_worker, NULL);
    VERIFY(worker);
    VERIFY(!fiber_switch(parent));
    failed(__LINE__, "abandoned waiter resumed");
}
static void *pin_worker(void *unused) {
    (void)unused;
    worker_result = vm_file_prefetch(file, 1); worker_errno = errno;
    if(!worker_result) worker_result = pin_page(1);
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); puts("VM4DISC BEGIN");
    mmucontext_t *previous = mmu_cxt_current;
    bool published = false;
    int direct = 0;
    int translated = -1, error = 0;
    kfiber_t *child = NULL;
    kthread_t *thread = NULL;
    sem_init(&entered, 0); sem_init(&resume, 0);
    context = mmu_context_create(76);
    REQUIRE(context && mmu_enabled());
    irq_mask_t mask = irq_disable();
    mmu_use_table(context); mmu_switch_context(context); irq_restore(mask);
    translated = translated_preflight(); REQUIRE(translated == 1);
    space = vm_space_create(context, (void *)ADDRESS, 3 * BYTES); REQUIRE(space);
    REQUIRE(!fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT));
    direct = fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT;
    REQUIRE(direct);
    printf("VM4DISC BACKEND direct=%d source=/cd/VM4DATA.BIN\n", direct);
    REQUIRE(!nmmgr_handler_add(&handler.nmmgr)); published = true;
    vm_file_options_t options = {.offset = 19, .bytes = 8301,
        .resident_limit = 2, .budget_bytes = 40000};
    REQUIRE(!vm_file_open(space, "/cd/VM4DATA.BIN", &options, &file));
    puts("VM4DISC SOURCE_OPEN /cd/VM4DATA.BIN");
    if(VM_FILE_FATAL) {
        printf("VM4DISC FATAL_ARMED reads=%u\n", atomic_load(&reads));
        volatile unsigned char absent = *(volatile unsigned char *)ADDRESS;
        (void)absent; failed(__LINE__, "absent access returned"); goto cleanup;
    }
    for(size_t i = 0; i < 3; ++i) {
        REQUIRE(!vm_file_prefetch(file, i)); ++loads;
        REQUIRE(!pin_page(i)); check_page(i);
        VERIFY(vm_file_evict(file, i) < 0 && errno == EBUSY);
        REQUIRE(!vm_pin_release(pin)); pin = NULL;
        REQUIRE(!vm_file_evict(file, i));
    }
    REQUIRE(!vm_file_close(file, &error)); file = NULL; VERIFY(!error);
    REQUIRE(!vm_file_open(space, "/vmdelay/data", &options, &file));
    parent = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT); REQUIRE(parent);
    delay_read = true;
    thread = thd_create(false, unblock, NULL); REQUIRE(thread);
    child = fiber_create(stack, sizeof(stack), pinned_child, NULL); REQUIRE(child);
    REQUIRE(!fiber_switch(child)); atomic_store(&parent_returned, 1);
    REQUIRE(!thd_join(thread, NULL)); thread = NULL; delay_read = false;
    VERIFY(vm_file_evict(file, 0) < 0 && errno == EBUSY);
    REQUIRE(!fiber_switch(child));
    REQUIRE(fiber_get_state(child) == KFIBER_STATE_FINISHED);
    REQUIRE(!fiber_destroy(child)); child = NULL;
    REQUIRE(!vm_file_evict(file, 0));
    /* Resident parent owns the file/worker. Destroying this waiting child never
       frees an executing synchronous stack or the worker's input state. */
    delay_read = true;
    child = fiber_create(stack, sizeof(stack), waiting_child, NULL); REQUIRE(child);
    REQUIRE(!fiber_switch(child)); REQUIRE(worker); sem_wait(&entered);
    REQUIRE(!fiber_destroy(child)); child = NULL; ++abandoned;
    REQUIRE(!vm_file_cancel(file));
    VERIFY(vm_file_close(file, &error) < 0 && errno == EBUSY);
    sem_signal(&resume); REQUIRE(!thd_join(worker, NULL)); worker = NULL;
    VERIFY(worker_result < 0 && worker_errno == ECANCELED); delay_read = false;
    REQUIRE(!vm_file_close(file, &error)); file = NULL; VERIFY(!error);
    REQUIRE(!vm_file_open(space, "/vmdelay/data", &options, &file));
    thread = thd_create(false, pin_worker, NULL); REQUIRE(thread);
    REQUIRE(!thd_join(thread, NULL)); thread = NULL;
    REQUIRE(!worker_result && pin); ++transfers; ++loads; check_page(1);
    REQUIRE(!vm_pin_release(pin)); pin = NULL;
    /* The same retained private /cd open reloads after eviction; no pathname
       reopen, media replacement or filesystem-global shutdown is allowed. */
    REQUIRE(!vm_file_evict(file, 1)); REQUIRE(!vm_file_prefetch(file, 1)); ++loads;
    REQUIRE(!pin_page(1)); check_page(1); REQUIRE(!vm_pin_release(pin)); pin = NULL;
    ++reloads;
    REQUIRE(!vm_file_close(file, &error)); file = NULL; VERIFY(!error);
    VERIFY(fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT);
cleanup:
    /* Normal path explicitly retires children/workers before final ownership.
       A failed prerequisite is a failed fixture, never a runtime pass. */
    if(worker) { sem_signal(&resume); thd_join(worker, NULL); }
    if(thread) {
        sem_signal(&entered); sem_signal(&resume); thd_join(thread, NULL); thread = NULL;
    }
    if(child) VERIFY(!fiber_destroy(child));
    if(pin) { VERIFY(!vm_pin_release(pin)); pin = NULL; }
    if(file) { VERIFY(!vm_file_close(file, &error)); file = NULL; }
    if(published) VERIFY(!nmmgr_handler_remove(&handler.nmmgr));
    if(space) VERIFY(!vm_space_destroy(space));
    if(context) {
        irq_mask_t saved = irq_disable(); mmu_use_table(previous);
        if(previous) mmu_switch_context(previous);
        irq_restore(saved); mmu_context_destroy(context);
    }
    sem_destroy(&entered); sem_destroy(&resume);
    printf("VM4DISC RESULT failures=%u translation=%d loads=%u yields=%u blocked=%u abandoned=%u transfers=%u reloads=%u wrapped_reads=%u closes=%u direct=%d\n",
        failures, translated, loads, yields, atomic_load(&blocked), abandoned,
        transfers, reloads, atomic_load(&reads), atomic_load(&closes), direct);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
