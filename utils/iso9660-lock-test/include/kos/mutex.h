#ifndef ISO_TEST_MUTEX_H
#define ISO_TEST_MUTEX_H
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define MUTEX_TYPE_NORMAL 0
int mutex_init(mutex_t *m, int type);
int mutex_destroy(mutex_t *m);
int mutex_lock(mutex_t *m);
int mutex_unlock(mutex_t *m);
static inline void mutex_cleanup(mutex_t **m) { if(*m) mutex_unlock(*m); }
#define LOCK_NAME2(n) lock_##n
#define LOCK_NAME(n) LOCK_NAME2(n)
#define mutex_lock_scoped(m) mutex_t *LOCK_NAME(__LINE__) __attribute__((cleanup(mutex_cleanup))) = (mutex_lock(m), (m))
#endif
