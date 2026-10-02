#ifndef ISO_TEST_THREAD_H
#define ISO_TEST_THREAD_H
#include <stddef.h>
typedef int (*thd_cb_t)(void *);
int thd_poll(thd_cb_t cb, void *data, unsigned timeout);
#endif
