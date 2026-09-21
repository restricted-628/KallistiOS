/* Undo the forced-include wrappers in the allocator implementation only. */
#undef malloc
#undef calloc
#undef free
#include <assert.h>
#include <stdint.h>
#include <string.h>

size_t test_alloc_calls, test_alloc_fail_at, test_alloc_live;
size_t test_alloc_sizes[64];

void *test_malloc(size_t size) {
    void *result;
    if(test_alloc_calls < sizeof(test_alloc_sizes) / sizeof(test_alloc_sizes[0]))
        test_alloc_sizes[test_alloc_calls] = size;
    if(++test_alloc_calls == test_alloc_fail_at)
        return NULL;
    result = malloc(size);
    if(result)
        ++test_alloc_live;
    return result;
}

void *test_calloc(size_t count, size_t size) {
    void *result;
    if(size && count > SIZE_MAX / size)
        return NULL;
    result = test_malloc(count * size);
    if(result)
        memset(result, 0, count * size);
    return result;
}

void test_free(void *pointer) {
    if(pointer) {
        assert(test_alloc_live);
        --test_alloc_live;
    }
    free(pointer);
}
