#ifndef TEST_ALLOC_H
#define TEST_ALLOC_H
#include <stddef.h>
#include <stdlib.h>
extern size_t test_alloc_calls, test_alloc_fail_at, test_alloc_live;
extern size_t test_alloc_sizes[64];
void *test_malloc(size_t size);
void *test_calloc(size_t count, size_t size);
void test_free(void *pointer);
#ifndef TEST_ALLOC_IMPL
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#endif
#endif
