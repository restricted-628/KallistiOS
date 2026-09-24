#ifndef __KOS_MUTEX_H
#define __KOS_MUTEX_H

#include <pthread.h>

#define MUTEX_TYPE_NORMAL 0u

typedef pthread_mutex_t mutex_t;

static inline int mutex_init(mutex_t *mutex, unsigned int type) {
    (void)type;
    return pthread_mutex_init(mutex, NULL);
}

static inline int mutex_lock(mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

static inline int mutex_unlock(mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

static inline int mutex_destroy(mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

#endif
