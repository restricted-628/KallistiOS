/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   White-box corruption checks compile the production allocator verbatim.
   The arena descriptor and mutex remain valid; block metadata is damaged.
*/
#include <assert.h>
#include <stdio.h>

#ifndef HEAP_SOURCE
#define HEAP_SOURCE "../../kernel/mm/heap.c"
#endif
#include HEAP_SOURCE

_Thread_local bool mm_heap_test_irq_context;

enum { REGION_BYTES = 4096, CASES = 12 };
static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)
#define FAULT(test) do { errno = 0; CHECK(test); CHECK(errno == EFAULT); } while(0)

int main(void) {
    for(unsigned kind = 0; kind < CASES; ++kind) {
        uint8_t *region = aligned_alloc(MM_HEAP_ALIGNMENT, REGION_BYTES);
        uint8_t saved[REGION_BYTES], damaged[REGION_BYTES];
        mm_heap_stats_t stats;
        mm_heap_t *heap;
        mm_heap_block_t *first, *last;
        size_t payload_bytes;
        void *allocation;

        CHECK(region != NULL);
        heap = mm_heap_create(region, REGION_BYTES);
        CHECK(heap != NULL);
        allocation = mm_heap_alloc(heap, 64);
        CHECK(allocation != NULL);
        memset(allocation, 0x5a, 64);
        first = heap->first;
        last = first->next;
        CHECK(last && !last->next);
        payload_bytes = (size_t)(heap->region_end - heap->region_begin);
        memcpy(saved, heap->region_begin, payload_bytes);

        switch(kind) {
            case 0: first->magic = 0; break;
            case 1: first->flags |= 2; break;
            case 2: first->capacity = SIZE_MAX; break;
            case 3: first->requested = first->capacity + 1; break;
            case 4: first->previous = first; break;
            case 5: first->next = first; break;
            case 6: first->next = (mm_heap_block_t *)((uint8_t *)last + 1); break;
            case 7: last->previous = NULL; break;
            /* Old validator dereferenced this one-past-end back-link. */
            case 8: last->next = (mm_heap_block_t *)heap->region_end; break;
            /* A successor must have room for a header AND a payload. */
            case 9:
                last->capacity -= MM_HEAP_ALIGNMENT;
                last->next = (mm_heap_block_t *)(heap->region_end - MM_HEAP_ALIGNMENT);
                break;
            case 10:
                last->capacity -= sizeof(*last);
                last->next = (mm_heap_block_t *)(heap->region_end - sizeof(*last));
                break;
            case 11: last->capacity = 0; break;
        }

        memcpy(damaged, heap->region_begin, payload_bytes);
        FAULT(mm_heap_validate(heap) == -1);
        FAULT(mm_heap_alloc(heap, 32) == NULL);
        FAULT(mm_heap_calloc(heap, 1, 32) == NULL);
        FAULT(mm_heap_realloc(heap, allocation, 128) == NULL);
        FAULT(mm_heap_free(heap, allocation) == -1);
        FAULT(mm_heap_get_stats(heap, &stats) == -1);
        FAULT(mm_heap_destroy(heap) == -1);
        CHECK(memcmp(damaged, heap->region_begin, payload_bytes) == 0);

        memcpy(heap->region_begin, saved, payload_bytes);
        CHECK(mm_heap_validate(heap) == 0);
        CHECK(mm_heap_free(heap, allocation) == 0);
        CHECK(mm_heap_destroy(heap) == 0);
        free(region);
    }
    printf("HEAP-CORRUPTION: PASS cases=%u checks=%u operations=7\n", CASES, checks);
    return 0;
}
