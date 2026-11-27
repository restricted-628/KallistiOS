/* Read directly from a file-backed address without explicit prefetch. */
#include <kos.h>
#include <dc/fs_iso9660.h>
#include <dc/memory.h>
#include <errno.h>
#include <stdlib.h>
#include <kos/vm_demand.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_MMU | INIT_CDROM | INIT_FS_NULL | INIT_NO_DCLOAD);

enum { PAGE_BYTES = 4096, SOURCE_OFFSET = 19, SOURCE_BYTES = 8301 };
static const uintptr_t arena = 0x101ff000;
static _Alignas(32) unsigned char carrier_stack[65536];

/* Use a separate temporary page to verify that translation and later refills
   are available. No file page or its translation exists at this point. */
static int check_translation(mmucontext_t *context) {
    const uint32_t cookie = 0x44503121;
    uint32_t *backing = aligned_alloc(PAGE_BYTES, PAGE_BYTES);
    if(!backing)
        return -1;
    backing[0] = cookie;
    dcache_purge_range((uintptr_t)backing, PAGE_BYTES);
    unsigned vpn = arena / PAGE_BYTES;
    int rc = mmu_page_map_ex(context, vpn,
        ((uintptr_t)backing & MEM_AREA_CACHE_MASK) / PAGE_BYTES, 1,
        MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true);
    if(!rc) {
        const mmupage_t *page =
            &context->sub[vpn / MMU_SUB_PAGES]->page[vpn % MMU_SUB_PAGES];
        volatile uint32_t *const pteh = (void *)0xff000000u;
        volatile uint32_t *const ptel = (void *)0xff000004u;
        volatile uint32_t *const mmucr = (void *)0xff000010u;
        irq_mask_t mask = irq_disable();
        uint32_t saved_h = *pteh, saved_l = *ptel, saved_control = *mmucr;
        *mmucr = saved_control & ~(63u << 10);
        *pteh = page->pteh | (unsigned)context->asid;
        *ptel = page->ptel;
        __asm__ volatile("ldtlb" ::: "memory");
        *mmucr = saved_control;
        *pteh = saved_h;
        *ptel = saved_l;
        __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop" ::: "memory");
        irq_restore(mask);
        uint32_t observed = *(volatile uint32_t *)arena;
        if(mmu_page_unmap_ex(context, vpn, 1) < 0)
            arch_panic("DP1 preflight could not retire its mapping");
        rc = observed == cookie ? 0 : -1;
    }
    free(backing);
    return rc;
}

static unsigned char source_byte(size_t offset) {
    return (unsigned char)((offset * 37 + offset / 251) ^ 0x9b);
}

static int check_cold_page(vm_file_t *file, size_t page) {
    vm_file_page_info_t info;
    if(vm_file_page_query(file, page, &info) < 0 || info.state != VM_FILE_ABSENT)
        return -1;

    vm_file_demand_t *use;
    if(vm_file_demand_begin(file, page, 1, &use) < 0)
        return -1;

    bool cold = !vm_file_page_query(file, page, &info) && info.state == VM_FILE_ABSENT;
    volatile const unsigned char *bytes =
        (const unsigned char *)(arena + page * PAGE_BYTES);
    const size_t start = page * PAGE_BYTES;
    errno = EDOM;
    unsigned char first = *bytes;
    bool matched = cold && first == source_byte(SOURCE_OFFSET + start) && errno == EDOM;
    for(size_t i = 1; i < PAGE_BYTES; ++i) {
        const unsigned char expected =
            start + i < SOURCE_BYTES ? source_byte(SOURCE_OFFSET + start + i) : 0;
        if(bytes[i] != expected)
            matched = false;
    }
    const bool ready = !vm_file_page_query(file, page, &info)
        && info.state == VM_FILE_READY;
    const bool retained = vm_file_evict(file, page) < 0 && errno == EBUSY;
    if(vm_file_demand_end(use) < 0)
        arch_panic("DP1 fixture could not end its use");
    return matched && ready && retained ? 0 : -1;
}

static void select_context(mmucontext_t *context) {
    irq_mask_t mask = irq_disable();
    mmu_use_table(context);
    if(context)
        mmu_switch_context(context);
    irq_restore(mask);
}

static void *run_test(void *data) {
    int *result = data;
    vm_space_t *space = NULL;
    vm_file_t *file = NULL;
    mmucontext_t *previous = mmu_cxt_current;
    mmucontext_t *context = mmu_context_create(77);
    if(!context)
        return NULL;
    select_context(context);
    if(!mmu_enabled() || check_translation(context) < 0)
        goto done;
    space = vm_space_create(context, (void *)arena, 3 * PAGE_BYTES);
    if(!space || fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT) < 0)
        goto done;
    const vm_file_options_t options = {
        .requested = (void *)arena, .offset = SOURCE_OFFSET, .bytes = SOURCE_BYTES,
        .resident_limit = 1, .budget_bytes = 40000
    };
    if(vm_file_open(space, "/cd/VM4DATA.BIN", &options, &file) < 0)
        goto done;

    if(check_cold_page(file, 0) || check_cold_page(file, 2)
            || check_cold_page(file, 0))
        goto done;
    vm_file_page_info_t tail;
    if(vm_file_page_query(file, 2, &tail) < 0 || tail.state != VM_FILE_ABSENT ||
            vm_file_evict(file, 0) < 0)
        goto done;
    *result = EXIT_SUCCESS;

done:
    if(file) {
        int close_error = 0;
        if(vm_file_close(file, &close_error) < 0)
            arch_panic("DP1 fixture could not retire its file");
        if(close_error)
            *result = EXIT_FAILURE;
    }
    if(space && vm_space_destroy(space) < 0)
        arch_panic("DP1 fixture could not retire its address space");
    select_context(previous);
    mmu_context_destroy(context);
    return NULL;
}

int main(void) {
    if(dbgio_dev_select("scif") < 0 || dbgio_set_irq_usage(0) < 0)
        return EXIT_FAILURE;
    memset(carrier_stack, 0xa5, sizeof(carrier_stack));
    const kthread_attr_t attr = {.stack_size = sizeof(carrier_stack),
        .stack_ptr = carrier_stack, .label = "dp1-disc"};
    int result = EXIT_FAILURE;
    kthread_t *carrier = thd_create_ex(&attr, run_test, &result);
    if(!carrier)
        return EXIT_FAILURE;
    if(thd_join(carrier, NULL) < 0)
        arch_panic("DP1 fixture could not join its carrier");
    size_t untouched = 0;
    while(untouched < sizeof(carrier_stack) && carrier_stack[untouched] == 0xa5)
        ++untouched;
    dbgio_printf("Demand carrier stack: %u touched, %u low bytes untouched\n",
           (unsigned)(sizeof(carrier_stack) - untouched), (unsigned)untouched);
    if(untouched < 8192)
        result = EXIT_FAILURE;
    dbgio_write_str(result == EXIT_SUCCESS
        ? "DP1DISC PASS cold=3 tail=1 reload=1 reclaim=2\n" : "DP1DISC FAIL\n");
    return result;
}
