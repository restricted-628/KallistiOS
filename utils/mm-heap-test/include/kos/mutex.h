#ifndef __KOS_MUTEX_H
#define __KOS_MUTEX_H

#include <pthread.h>
#include <errno.h>

#define MUTEX_TYPE_NORMAL 0u

typedef pthread_mutex_t mutex_t;

/* pthread returns a positive error number; KOS uses -1 and errno. */
static inline int mutex_result(int result) {
    if(result) {
        errno = result;
        return -1;
    }
    return 0;
}

static inline int mutex_init(mutex_t *mutex, unsigned int type) {
    (void)type;
    return mutex_result(pthread_mutex_init(mutex, NULL));
}

static inline int mutex_lock(mutex_t *mutex) {
    return mutex_result(pthread_mutex_lock(mutex));
}

static inline int mutex_unlock(mutex_t *mutex) {
    return mutex_result(pthread_mutex_unlock(mutex));
}

static inline int mutex_destroy(mutex_t *mutex) {
    return mutex_result(pthread_mutex_destroy(mutex));
}

#endif
