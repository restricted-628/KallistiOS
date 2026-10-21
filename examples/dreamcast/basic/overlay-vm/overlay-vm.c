/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/fiber.h>
#include <kos/overlay.h>
#include <kos/vm_exec.h>
#include <dc/fs_iso9660.h>
#include <dc/memory.h>
#include <errno.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include "member-api.h"

KOS_INIT_FLAGS(INIT_DEFAULT);
extern const ovl_expected_t member_identity[2];
static mmucontext_t *context;
static vm_space_t *space;
static ovl_slot_t *slot;
static ovl_token_t running_token, callback_token;
static ovl_view_t running_view, callback_view;
static ovl_entry_t registered_callback;
static kfiber_t *parent;
static alignas(32) unsigned char child_stack[16384];
static semaphore_t entered, resume_callback;
static mutex_t sample_log = MUTEX_INITIALIZER;
static volatile unsigned heartbeat, stop_heartbeat;
static unsigned loaded, unloaded, calls, yields, callbacks, blocked, preemptions;
static unsigned samples, negatives, expected_id, cleanups;
static bool refuse_begin, refuse_abort, refuse_retire, fail_write;
static volatile uint32_t *const pteh = (void *)0xff000000u;
static volatile uint32_t *const mmucr = (void *)0xff000010u;

/* Fail closed rather than freeing a possibly live translated continuation. */
#define CHECK(x) do { if(!(x)) { \
    printf("OVL3 CHECK line=%d errno=%d: %s\n", __LINE__, errno, #x); \
    arch_panic("overlay-vm fixture assertion"); } } while(0)

/* Link-only fault injection at the consumer boundary. Every non-injected call
   reaches the actual Kernel implementation. No production hooks or observer
   contract violations are used to synthesize these failures. */
int __real_vm_exec_begin(vm_exec_load_t *load);
int __real_vm_exec_abort(vm_exec_load_t **load);
int __real_vm_exec_retire(vm_exec_region_t *region);
int __real_vm_exec_write(vm_exec_load_t *load, size_t offset, const void *data, size_t bytes);
int __wrap_vm_exec_begin(vm_exec_load_t *load) {
    if(refuse_begin) { errno = EBUSY; return -1; }
    return __real_vm_exec_begin(load);
}
int __wrap_vm_exec_abort(vm_exec_load_t **load) {
    if(refuse_abort) { errno = EAGAIN; return -1; }
    return __real_vm_exec_abort(load);
}
int __wrap_vm_exec_retire(vm_exec_region_t *region) {
    if(refuse_retire) { errno = EIO; return -1; }
    return __real_vm_exec_retire(region);
}
int __wrap_vm_exec_write(vm_exec_load_t *load, size_t offset, const void *data, size_t bytes) {
    int rc = __real_vm_exec_write(load, offset, data, bytes);
    if(!rc && fail_write) { errno = EIO; return -1; }
    return rc;
}

static void sample_context(const char *where) {
    irq_mask_t old = irq_disable();
    mmucontext_t *selected = mmu_cxt_current;
    uint32_t h = *pteh, control = *mmucr;
    irq_restore(old);
    CHECK(selected == context && (h & 255) == (unsigned)context->asid && (control & 1));
    CHECK(!mutex_lock(&sample_log));
    ++samples;
    printf("OVL3 CONTEXT %s table=%p asid=%lu mmucr=%08lx\n", where,
           selected, (unsigned long)(h & 255), (unsigned long)control);
    CHECK(!mutex_unlock(&sample_log));
}

/* Same explicit preflight as the published primitive fixture. Only a separate
   temporary page is seeded: no overlay-owned page bypasses ordinary refill.
   This permits unchanged runtimes that activate general translation at LDTLB
   to distinguish that mode from SQ-only operation before testing the consumer. */
static void preflight(void) {
    uint32_t *backing = aligned_alloc(4096, 4096);
    CHECK(backing);
    backing[0] = MEMBER_COOKIE;
    dcache_purge_range((uintptr_t)backing, 4096);
    CHECK(!mmu_page_map_ex(context, MEMBER_BASE >> 12,
          ((uintptr_t)backing & MEM_AREA_CACHE_MASK) >> 12, 1,
          MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true));
    unsigned vpn = MEMBER_BASE >> 12;
    const mmupage_t *page = &context->sub[vpn / MMU_SUB_PAGES]->page[vpn % MMU_SUB_PAGES];
    volatile uint32_t *const ptel = (void *)0xff000004u;
    irq_mask_t old = irq_disable();
    uint32_t h = *pteh, l = *ptel, control = *mmucr;
    *mmucr = control & ~(63u << 10);
    *pteh = page->pteh | (unsigned)context->asid;
    *ptel = page->ptel;
    __asm__ volatile("ldtlb");
    *mmucr = control; *pteh = h; *ptel = l;
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
    irq_restore(old);
    CHECK(*(volatile uint32_t *)MEMBER_BASE == MEMBER_COOKIE);
    CHECK(!mmu_page_unmap_ex(context, MEMBER_BASE >> 12, 1));
    free(backing);
}

static void observer(ovl_slot_t *which, bool active, void *unused) {
    (void)unused;
    CHECK(which == slot);
    if(active) ++loaded;
    else ++unloaded;
    ovl_token_t token = {0};
    ovl_view_t view;
    CHECK(ovl_acquire(slot, &token, &view) < 0 && errno == EBUSY);
}

static void busy(uint64_t generation) {
    unsigned old_loaded = loaded, old_unloaded = unloaded;
    ovl_load_result_t result;
    CHECK(ovl_load(slot, "/cd/B.DCO", &member_identity[1], &result) < 0 && errno == EBUSY);
    CHECK(ovl_unload(slot) < 0 && errno == EBUSY);
    CHECK(loaded == old_loaded && unloaded == old_unloaded);
    ovl_token_t token = {0};
    ovl_view_t view;
    CHECK(!ovl_acquire(slot, &token, &view));
    CHECK(view.generation == generation);
    CHECK(!ovl_release(slot, &token));
    blocked += 2;
}

static void *heartbeat_thread(void *unused) {
    (void)unused;
    sample_context("independent-carrier");
    while(!stop_heartbeat) {
        ++heartbeat;
        thd_sleep(1);
    }
    return NULL;
}

static int service(unsigned id, unsigned visit, unsigned mode) {
    CHECK(id == expected_id && visit == (mode == 1 ? 1u : 2u));
    sample_context("translated-helper-enter");
    if(mode == 1) {
        /* No explicit yield/wait in this interval. An independent thread must
           progress while five translated return addresses are still live. */
        unsigned before = heartbeat;
        uint64_t deadline = timer_ms_gettime64() + 1000;
        while(heartbeat == before && timer_ms_gettime64() < deadline) {}
        CHECK(heartbeat != before);
        ++preemptions;
        ++yields;
        CHECK(!fiber_switch(parent));
    }
    else {
        CHECK(mode == 2);
        ++callbacks;
        CHECK(!sem_signal(&entered));
        CHECK(!sem_wait(&resume_callback));
    }
    sample_context("translated-helper-resume");
    return 16;
}

static void fiber_wrapper(void *unused) {
    (void)unused;
    member_context_t c = {service, MEMBER_COOKIE, 1};
    CHECK(running_view.entry(&c) == (int)(expected_id * 1000 + 27));
    ++calls;
    CHECK(!ovl_release(slot, &running_token)); /* final return is resident */
}

static void *callback_wrapper(void *unused) {
    (void)unused;
    sample_context("callback-carrier");
    member_context_t c = {service, MEMBER_COOKIE, 2};
    CHECK(registered_callback(&c) == (int)(expected_id * 1000 + 28));
    ++calls;
    return NULL; /* owner joins and deregisters before releasing its token */
}

static void exercise(uint64_t generation) {
    CHECK(!ovl_acquire(slot, &running_token, &running_view));
    CHECK(running_view.base == (void *)MEMBER_BASE && running_view.generation == generation);
    CHECK(!ovl_acquire(slot, &callback_token, &callback_view));
    registered_callback = callback_view.entry;
    stop_heartbeat = 0;
    kthread_t *ticker = thd_create(false, heartbeat_thread, NULL);
    CHECK(ticker);
    kfiber_t *child = fiber_create(child_stack, sizeof(child_stack), fiber_wrapper, NULL);
    CHECK(child && !fiber_switch(child));
    CHECK(fiber_get_state(child) != KFIBER_STATE_FINISHED);
    busy(generation);
    sample_context("parent-with-suspended-return");
    CHECK(!fiber_switch(child));
    CHECK(fiber_get_state(child) == KFIBER_STATE_FINISHED && !running_token._slot);
    CHECK(!fiber_destroy(child));
    stop_heartbeat = 1;
    CHECK(!thd_join(ticker, NULL));
    busy(generation); /* separately retained callback still owns the image */
    kthread_t *carrier = thd_create(false, callback_wrapper, NULL);
    CHECK(carrier && !sem_wait(&entered));
    busy(generation); /* callback blocked with translated returns on its stack */
    CHECK(!sem_signal(&resume_callback));
    CHECK(!thd_join(carrier, NULL));
    registered_callback = NULL;
    busy(generation); /* finished/joined alone does not release the token */
    CHECK(!ovl_release(slot, &callback_token));
}

/* Run only without tokens, callbacks or other application carriers. Each
   mismatch is restored before any translated invocation. These are admission
   negatives, never tests that resume live translated code in a wrong context. */
static void admission_negatives(void) {
    mmucontext_t *other = mmu_context_create(context->asid);
    CHECK(other);
    for(unsigned mode = 0; mode < 3; ++mode) {
        ovl_token_t token = {0};
        ovl_view_t view, original;
        memset(&view, 0xa7, sizeof(view)); original = view;
        irq_mask_t old = irq_disable();
        uint32_t h = *pteh, control = *mmucr;
        if(mode == 0) mmu_use_table(other);
        if(mode == 1) *pteh = (h & ~255u) | ((h + 1) & 255);
        if(mode == 2) *mmucr = control & ~1u;
        __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
        irq_restore(old);
        int rc = ovl_acquire(slot, &token, &view), error = errno;
        old = irq_disable();
        mmu_use_table(context); *pteh = h; *mmucr = control;
        __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
        irq_restore(old);
        CHECK(rc < 0 && error == EBUSY && !token._slot && !memcmp(&view, &original, sizeof(view)));
        ++negatives;
    }
    mmu_context_destroy(other);
    sample_context("admission-restored");
}

static void load_member(unsigned index) {
    const char *name = index ? "/cd/B.DCO" : "/cd/A.DCO";
    ovl_load_result_t result;
    printf("OVL3 SOURCE %s bytes=%lu\n", name, (unsigned long)member_identity[index].package_bytes);
    CHECK(!ovl_load(slot, name, &member_identity[index], &result));
    CHECK(result.image_admitted && !result.error && !result.source_close_error);
}

static void held_cleanup(void) {
    ovl_load_result_t result;
    ovl_token_t token = {0}; ovl_view_t view;
    load_member(0);
    int physical = mmu_virt_to_phys(context, MEMBER_BASE / 4096);
    CHECK(physical >= 0);
    unsigned old_loaded = loaded, old_unloaded = unloaded;
    refuse_begin = refuse_retire = true;
    CHECK(ovl_load(slot, "/cd/B.DCO", &member_identity[1], &result) < 0 && errno == EBUSY);
    CHECK(!result.image_admitted && result.error == EBUSY && !result.source_close_error);
    /* Real PREPARED abort preserved old backing, but admission must stay shut. */
    CHECK(mmu_virt_to_phys(context, MEMBER_BASE / 4096) == physical);
    CHECK(ovl_acquire(slot, &token, &view) < 0 && errno == ENOENT);
    CHECK(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    CHECK(ovl_load(slot, "/cd/A.DCO", &member_identity[0], &result) < 0 && errno == EBUSY);
    CHECK(ovl_unload(slot) < 0 && errno == EIO);
    refuse_begin = refuse_retire = false;
    CHECK(!ovl_unload(slot));
    CHECK(mmu_virt_to_phys(context, MEMBER_BASE / 4096) < 0);
    CHECK(loaded == old_loaded && unloaded == old_unloaded + 1);
    ++cleanups;

    load_member(1);
    old_loaded = loaded; old_unloaded = unloaded;
    fail_write = refuse_abort = true;
    CHECK(ovl_load(slot, "/cd/A.DCO", &member_identity[0], &result) < 0 && errno == EIO);
    CHECK(!result.image_admitted && result.error == EIO && !result.source_close_error);
    CHECK(ovl_acquire(slot, &token, &view) < 0 && errno == ENOENT);
    CHECK(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    CHECK(ovl_load(slot, "/cd/B.DCO", &member_identity[1], &result) < 0 && errno == EBUSY);
    CHECK(ovl_unload(slot) < 0 && errno == EAGAIN);
    fail_write = refuse_abort = false; refuse_retire = true;
    CHECK(ovl_unload(slot) < 0 && errno == EIO); /* real abort ran, retire held */
    CHECK(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    refuse_retire = false;
    CHECK(!ovl_unload(slot));
    for(unsigned i = 0; i < MEMBER_PAGES; ++i)
        CHECK(mmu_virt_to_phys(context, MEMBER_BASE / 4096 + i) < 0);
    CHECK(loaded == old_loaded && unloaded == old_unloaded + 1);
    ++cleanups;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("OVL3 BEGIN");
    mmucontext_t *previous = mmu_cxt_current;
    unsigned previous_asid = *pteh & 255;
    context = mmu_context_create(77);
    CHECK(context && mmu_enabled());
    irq_mask_t old = irq_disable();
    mmu_use_table(context); mmu_switch_context(context); irq_restore(old);
    preflight();
    CHECK(!fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT));
    int direct = fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT;
    CHECK(direct);
    printf("OVL3 BACKEND direct=%d\n", direct);
    space = vm_space_create(context, (void *)MEMBER_BASE, MEMBER_PAGES * 4096);
    ovl_slot_options_t options = {(void *)MEMBER_BASE, MEMBER_PAGES * 4096, observer, NULL};
    CHECK(space && !ovl_slot_create_vm(space, &options, &slot));
    CHECK(!sem_init(&entered, 0) && !sem_init(&resume_callback, 0));
    parent = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    CHECK(parent);
    int physical[MEMBER_PAGES] = {0};
    unsigned remapped = 0;
    for(unsigned iteration = 0; iteration < 3; ++iteration) {
        void *pressure[MEMBER_PAGES] = {0};
        if(iteration) {
            CHECK(!ovl_unload(slot));
            for(unsigned i = 0; i < MEMBER_PAGES; ++i) {
                pressure[i] = aligned_alloc(4096, 4096);
                CHECK(pressure[i]);
            }
        }
        expected_id = iteration == 1 ? 2 : 1;
        load_member(expected_id - 1);
        unsigned changed = 0;
        for(unsigned i = 0; i < MEMBER_PAGES; ++i) {
            int page = mmu_virt_to_phys(context, MEMBER_BASE / 4096 + i);
            CHECK(page >= 0);
            if(iteration && page != physical[i]) ++changed;
            physical[i] = page;
            free(pressure[i]);
        }
        if(iteration == 1) CHECK(changed > 0);
        if(changed) ++remapped;
        printf("OVL3 PHYSICAL id=%u changed=%u first=%08lx\n", expected_id,
               changed, (unsigned long)physical[0] * 4096);
        if(iteration == 0) admission_negatives();
        exercise(iteration + 1);
    }
    ovl_load_result_t result;
    CHECK(ovl_load(slot, "/cd/BADHEAD.DCO", &member_identity[0], &result) < 0);
    CHECK(result.error == ENOEXEC && !result.image_admitted && !result.source_close_error);
    ovl_token_t token = {0}; ovl_view_t view;
    CHECK(!ovl_acquire(slot, &token, &view) && view.generation == 3);
    CHECK(!ovl_release(slot, &token));
    CHECK(ovl_load(slot, "/cd/BADHASH.DCO", &member_identity[0], &result) < 0);
    CHECK(result.error == ENOEXEC && !result.image_admitted && !result.source_close_error);
    CHECK(ovl_acquire(slot, &token, &view) < 0 && errno == ENOENT);
    held_cleanup();
    CHECK(!ovl_unload(slot) && !ovl_slot_destroy(slot)); slot = NULL;
    CHECK(!vm_space_destroy(space)); space = NULL;
    CHECK(!sem_destroy(&entered) && !sem_destroy(&resume_callback));
    old = irq_disable(); mmu_use_table(previous);
    *pteh = (*pteh & ~255u) | previous_asid;
    irq_restore(old); mmu_context_destroy(context);
    printf("OVL3 RESULT failures=0 loads=%u unloads=%u calls=%u yields=%u callbacks=%u blocked=%u preemptions=%u negatives=%u samples=%u direct=%d remapped=%u cleanups=%u\n",
           loaded, unloaded, calls, yields, callbacks, blocked, preemptions,
           negatives, samples, direct, remapped, cleanups);
    return 0;
}
