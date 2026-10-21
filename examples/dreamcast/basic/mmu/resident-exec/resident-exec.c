/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/vm_exec.h>
#include <kos/fiber.h>
#include <dc/memory.h>
#include <dc/fs_iso9660.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ADDRESS 0x101ff000u
#define BYTES 4096u
#define PAGES 6u
#define FILE_BYTES (5u * BYTES + 4u)
#define COOKIE 0x4f564c33u
extern const unsigned char expected_members[2][FILE_BYTES];
typedef uint32_t (*service_t)(uint32_t, uint32_t);
typedef uint32_t (*entry_t)(service_t);
static volatile uint32_t *const data = (void *)ADDRESS;
static mmucontext_t *context;
static vm_space_t *space;
static vm_exec_region_t *region;
static vm_exec_load_t *load;
static kfiber_t *parent;
static alignas(32) unsigned char stack[8192], stage[BYTES];
static unsigned failures, loads, calls, yields, blocked, users, expected_id;
static bool yielding;
enum { PRESSURE_LIMIT = PAGES };
static void *replacement_pressure[PRESSURE_LIMIT];
static unsigned pressure_count, pressure_peak;
static int previous_physical[PAGES];
static unsigned remapped;
static const uint8_t plan[PAGES] = {5, 5, 5, 5, 5, 6};
static void failed(unsigned line, const char *expression) {
    int saved = errno; ++failures;
    printf("OVL3EXEC CHECK line=%u errno=%d: %s\n", line, saved, expression);
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
    printf("OVL3EXEC preflight backing=%08lx\n", (unsigned long)backing);
    preload_preflight();
    int translated = data[0] == COOKIE;
    if(translated) {
        uint32_t *replacement = aligned_alloc(BYTES, BYTES);
        if(!replacement) {
            if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
                arch_panic("resident-exec preflight retirement failed");
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
            printf("OVL3EXEC preflight remap expected=%08lx observed=%08lx\n",
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
        arch_panic("resident-exec preflight retirement failed");
    free(backing);
    return translated;
}


/* This fixture owns its uses. Kernel does not discover suspended raw users. */
static int prepare_quiescent(void) {
    if(users) { errno = EBUSY; return -1; }
    return vm_exec_prepare(region, plan, PAGES, &load);
}
static uint32_t service(uint32_t id, uint32_t count) {
    VERIFY(id == expected_id && count == (yielding ? 1u : 2u));
    VERIFY(!vm_exec_check_context(region));
    if(yielding) {
        ++yields;
        VERIFY(!fiber_switch(parent)); /* five translated returns remain live */
        VERIFY(!vm_exec_check_context(region));
    }
    return 16;
}
static void child(void *unused) {
    (void)unused;
    ++users;
    uint32_t result = ((entry_t)ADDRESS)(service);
    VERIFY(result == expected_id * 1000 + 27);
    ++calls; --users; /* final return is resident before releasing the use */
}
static int load_member(unsigned index) {
    file_t fd = FILEHND_INVALID;
    int rc = -1;
    if(prepare_quiescent() || vm_exec_begin(load)) goto done;
    const char *path = index ? "/cd/B.BIN" : "/cd/A.BIN";
    printf("OVL3EXEC SOURCE %s\n", path);
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) goto done;
    if(fs_total64(fd) != FILE_BYTES) { errno = EINVAL; goto done; }
    for(size_t offset = 0; offset < FILE_BYTES;) {
        size_t n = FILE_BYTES - offset;
        if(n > sizeof(stage)) n = sizeof(stage);
        ssize_t got = fs_read(fd, stage, n);
        if(got != (ssize_t)n) { errno = EIO; goto done; }
        /* Exact original fixture bytes, no package or ELF parser in the guest. */
        if(memcmp(stage, expected_members[index] + offset, n)) { errno = EIO; goto done; }
        if(vm_exec_write(load, offset, stage, n)) goto done;
        memset(stage, 0xa5, sizeof(stage)); /* source may be reused immediately */
        offset += n;
    }
    if(vm_exec_zero(load, FILE_BYTES, PAGES * BYTES - FILE_BYTES)) goto done;
    int close_rc = fs_close(fd); fd = FILEHND_INVALID; /* close consumes fd */
    if(close_rc) goto done;
    if(vm_exec_publish(&load)) goto done;
    ++loads; rc = 0;
done:
    if(fd != FILEHND_INVALID) VERIFY(!fs_close(fd));
    if(rc && load) VERIFY(!vm_exec_abort(&load));
    return rc;
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("OVL3EXEC BEGIN");
    mmucontext_t *previous = mmu_cxt_current;
    unsigned previous_asid = *(volatile uint32_t *)0xff000000u & 255;
    kfiber_t *c = NULL;
    int translated = -1, direct = 0;
    context = mmu_context_create(77);
    REQUIRE(context && mmu_enabled());
    irq_mask_t mask = irq_disable();
    mmu_use_table(context); mmu_switch_context(context); irq_restore(mask);
    translated = translated_preflight(); REQUIRE(translated == 1);
    REQUIRE(!fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT));
    direct = fs_iso9660_get_backend() == FS_ISO9660_BACKEND_DIRECT;
    REQUIRE(direct);
    printf("OVL3EXEC BACKEND direct=%d\n", direct);
    space = vm_space_create(context, (void *)ADDRESS, PAGES * BYTES);
    REQUIRE(space && !vm_exec_reserve(space, (void *)ADDRESS, PAGES * BYTES, &region));
    parent = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT); REQUIRE(parent);
    for(unsigned iteration = 0; iteration < 3; ++iteration) {
        expected_id = iteration == 1 ? 2 : 1;
        if(iteration) {
            /* Quiescent retirement plus bounded ordinary allocations force a
               different A-to-B layout without changing the executable API.
               The final A also permits reuse of the retired B physical pages. */
            REQUIRE(!vm_exec_retire(region));
            for(unsigned page = 0; page < PAGES; ++page) {
                void *p = aligned_alloc(BYTES, BYTES); REQUIRE(p);
                replacement_pressure[pressure_count++] = p;
            }
            if(pressure_count > pressure_peak) pressure_peak = pressure_count;
        }
        REQUIRE(!load_member(expected_id - 1));
        unsigned changed = 0;
        for(unsigned page = 0; page < PAGES; ++page) {
            int physical = mmu_virt_to_phys(context, ADDRESS / BYTES + page);
            REQUIRE(physical >= 0);
            if(iteration && physical != previous_physical[page]) ++changed;
            previous_physical[page] = physical;
        }
        while(pressure_count) free(replacement_pressure[--pressure_count]);
        if(iteration == 1) VERIFY(changed == PAGES);
        if(changed) ++remapped;
        printf("OVL3EXEC PHYSICAL id=%u changed=%u first=%08lx\n",
               expected_id, changed, (unsigned long)previous_physical[0] * BYTES);
        vm_exec_info_t info;
        REQUIRE(!vm_exec_query(region, &info));
        VERIFY(info.phase == VM_EXEC_READY && info.published &&
               info.backing_bytes == PAGES * BYTES &&
               info.table_budget_bytes == 2 * sizeof(mmusubcontext_t));
        printf("OVL3EXEC READY id=%u backing=%lu metadata=%lu tables=%lu\n",
               expected_id, (unsigned long)info.backing_bytes,
               (unsigned long)info.metadata_bytes, (unsigned long)info.table_budget_bytes);
        REQUIRE(!vm_exec_check_context(region));
        yielding = true;
        c = fiber_create(stack, sizeof(stack), child, NULL); REQUIRE(c);
        REQUIRE(!fiber_switch(c));
        VERIFY(users == 1 && !load);
        VERIFY(prepare_quiescent() < 0 && errno == EBUSY && !load); ++blocked;
        VERIFY(!vm_exec_query(region, &info) && info.phase == VM_EXEC_READY && info.published);
        thd_pass(); /* global application context stays selected across carriers */
        REQUIRE(!vm_exec_check_context(region));
        REQUIRE(!fiber_switch(c));
        VERIFY(!users && fiber_get_state(c) == KFIBER_STATE_FINISHED);
        REQUIRE(!fiber_destroy(c)); c = NULL;
        /* Escaped entry pointer remains valid only under an explicit live use. */
        entry_t callback = (entry_t)ADDRESS;
        yielding = false; ++users;
        REQUIRE(!vm_exec_check_context(region));
        uint32_t result = callback(service);
        VERIFY(result == expected_id * 1000 + 28);
        ++calls; --users;
    }
cleanup:
    if(c && fiber_get_state(c) != KFIBER_STATE_FINISHED)
        arch_panic("resident-exec retains a live translated continuation");
    if(c) VERIFY(!fiber_destroy(c));
    if(load && vm_exec_abort(&load)) arch_panic("resident-exec load cleanup retained");
    if(region) {
        if(vm_exec_retire(region) || vm_exec_destroy(&region))
            arch_panic("resident-exec region cleanup retained");
    }
    while(pressure_count) free(replacement_pressure[--pressure_count]);
    if(space) VERIFY(!vm_space_destroy(space));
    if(context) {
        irq_mask_t saved = irq_disable(); mmu_use_table(previous);
        volatile uint32_t *pteh = (void *)0xff000000u;
        *pteh = (*pteh & ~255u) | previous_asid;
        irq_restore(saved); mmu_context_destroy(context);
    }
    printf("OVL3EXEC PRESSURE peak_pages=%u\n", pressure_peak);
    printf("OVL3EXEC RESULT failures=%u loads=%u calls=%u yields=%u blocked=%u direct=%d translation=%d remapped=%u\n",
           failures, loads, calls, yields, blocked, direct, translated, remapped);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
