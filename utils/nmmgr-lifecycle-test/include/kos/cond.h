#ifndef __KOS_COND_H
#define __KOS_COND_H

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include <kos/mutex.h>

typedef pthread_cond_t condvar_t;

static inline int cond_init(condvar_t *condition) {
    return pthread_cond_init(condition, NULL);
}

static inline int cond_destroy(condvar_t *condition) {
    return pthread_cond_destroy(condition);
}

static inline int cond_broadcast(condvar_t *condition) {
    return pthread_cond_broadcast(condition);
}

static inline int cond_wait_timed(condvar_t *condition, mutex_t *mutex,
                                  int timeout) {
    struct timespec deadline;
    int rv;

    if(timeout == 0)
        return pthread_cond_wait(condition, mutex);

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (long)(timeout % 1000) * 1000000L;

    if(deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }

    rv = pthread_cond_timedwait(condition, mutex, &deadline);

    if(rv == ETIMEDOUT) {
        errno = ETIMEDOUT;
        return -1;
    }

    return rv;
}

#endif
