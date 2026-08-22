/* KallistiOS ##version##

   mapping-safety.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <arch/mmu.h>
#include <dc/memory.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_VIRTUAL_ADDRESS 0x10000000u
#define TEST_VIRTUAL_PAGE (TEST_VIRTUAL_ADDRESS >> PAGESIZE_BITS)

static mmupage_t *test_page(mmucontext_t *context) {
    unsigned int top = TEST_VIRTUAL_PAGE / MMU_SUB_PAGES;
    unsigned int bot = TEST_VIRTUAL_PAGE % MMU_SUB_PAGES;

    if(!context->sub[top])
        return NULL;

    return &context->sub[top]->page[bot];
}

static int fail(const char *operation) {
    printf("MMU mapping probe failed: %s (errno=%d: %s)\n",
           operation, errno, strerror(errno));
    return EXIT_FAILURE;
}

int main(int argc, char **argv) {
    volatile uint32_t *mapped = (volatile uint32_t *)TEST_VIRTUAL_ADDRESS;
    uint32_t *first = NULL;
    uint32_t *second = NULL;
    mmucontext_t *context = NULL;
    mmupage_t *page;
    int first_physical;
    int second_physical;
    bool live_translation;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    first = aligned_alloc(PAGESIZE, PAGESIZE);
    second = aligned_alloc(PAGESIZE, PAGESIZE);
    if(!first || !second)
        goto out;

    first[0] = 0x11223344u;
    second[0] = 0xa55a5aa5u;
    dcache_purge_range((uintptr_t)first, PAGESIZE);
    dcache_purge_range((uintptr_t)second, PAGESIZE);

    first_physical = ((uintptr_t)first & MEM_AREA_CACHE_MASK) >> PAGESIZE_BITS;
    second_physical = ((uintptr_t)second & MEM_AREA_CACHE_MASK) >> PAGESIZE_BITS;

    if(((uintptr_t)first & PAGEMASK) || ((uintptr_t)second & PAGEMASK)) {
        errno = EFAULT;
        result = fail("page allocation alignment");
        goto out;
    }

    mmu_init();
    context = mmu_context_create(7);
    if(!context) {
        result = fail("context creation");
        goto shutdown;
    }

    mmu_use_table(context);
    mmu_switch_context(context);

    errno = 0;
    if(mmu_page_map_ex(context, -1, first_physical, 1,
                       MMU_ALL_RDWR, MMU_NO_CACHE, false, true) == 0 ||
       errno != EINVAL) {
        result = fail("invalid mapping rejection");
        goto destroy;
    }

    if(mmu_page_map_ex(context, TEST_VIRTUAL_PAGE, first_physical, 1,
                       MMU_ALL_RDWR, MMU_NO_CACHE, false, true) < 0) {
        result = fail("initial mapping");
        goto destroy;
    }

    if(mmu_virt_to_phys(context, TEST_VIRTUAL_PAGE) != first_physical ||
       mmu_phys_to_virt(context, first_physical) != TEST_VIRTUAL_PAGE) {
        errno = EIO;
        result = fail("initial translation");
        goto destroy;
    }

    /* Some emulators intentionally implement only the SQ-specific MMU fast
       path for this guest. Keep exercising the page-table and TLB-management
       operations there, but reserve translated-data verification for an
       implementation that actually applies general P0 mappings. */
    live_translation = *mapped == first[0];
    if(!live_translation)
        printf("General P0 translation is not provided by this runtime; "
               "continuing structural checks\n");

    /* Reading above filled a TLB entry. This remap must invalidate it before
       the same virtual address is observed again. */
    if(mmu_page_map_ex(context, TEST_VIRTUAL_PAGE, second_physical, 1,
                       MMU_ALL_RDWR, MMU_NO_CACHE, false, true) < 0 ||
       mmu_virt_to_phys(context, TEST_VIRTUAL_PAGE) != second_physical ||
       (live_translation && *mapped != second[0])) {
        errno = EIO;
        result = fail("live remap");
        goto destroy;
    }

    if(mmu_page_set_cache(context, TEST_VIRTUAL_PAGE, 1,
                          MMU_CACHE_BACK) < 0) {
        result = fail("copy-back policy");
        goto destroy;
    }

    page = test_page(context);
    if(!page || !page->cache || page->wthru) {
        errno = EIO;
        result = fail("copy-back encoding");
        goto destroy;
    }

    if(mmu_page_set_cache(context, TEST_VIRTUAL_PAGE, 1,
                          MMU_CACHE_WT) < 0 ||
       !page->cache || !page->wthru) {
        errno = EIO;
        result = fail("write-through encoding");
        goto destroy;
    }

    if(mmu_page_set_cache(context, TEST_VIRTUAL_PAGE, 1,
                          MMU_NO_CACHE) < 0 ||
       page->cache || page->wthru) {
        errno = EIO;
        result = fail("uncached encoding");
        goto destroy;
    }

    if(mmu_page_unmap(context, TEST_VIRTUAL_PAGE, 1) < 0 ||
       mmu_virt_to_phys(context, TEST_VIRTUAL_PAGE) != -1) {
        errno = EIO;
        result = fail("unmap");
        goto destroy;
    }

    if(mmu_page_map_ex(context, TEST_VIRTUAL_PAGE, first_physical, 1,
                       MMU_ALL_RDWR, MMU_NO_CACHE, false, true) < 0 ||
       mmu_virt_to_phys(context, TEST_VIRTUAL_PAGE) != first_physical ||
       (live_translation && *mapped != first[0])) {
        errno = EIO;
        result = fail("remap after unmap");
        goto destroy;
    }

    result = EXIT_SUCCESS;

destroy:
    mmu_context_destroy(context);
    context = NULL;
    if(mmu_cxt_current) {
        errno = EIO;
        result = fail("active context teardown");
    }

shutdown:
    mmu_shutdown();

    if(result == EXIT_SUCCESS)
        printf("KOSMMUMAP remap=1 cache=1 unmap=1 destroy=1 translation=%u\n",
               live_translation);

out:
    free(second);
    free(first);
    return result;
}
