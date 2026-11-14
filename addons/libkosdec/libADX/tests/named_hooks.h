/* Inject allocation faults only into the owned-file object. */
#include <stddef.h>
void *named_test_calloc(size_t, size_t);
void named_test_free(void *);
#define calloc named_test_calloc
#define free named_test_free
