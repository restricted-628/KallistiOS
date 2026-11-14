#ifndef ADX_NAMED_TEST_THREAD_H
#define ADX_NAMED_TEST_THREAD_H
typedef struct kthread { int id; } kthread_t;
kthread_t *thd_get_current(void);
#endif
