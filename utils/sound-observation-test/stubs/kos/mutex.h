#ifndef OBS_TEST_MUTEX_H
#define OBS_TEST_MUTEX_H
#include <pthread.h>
#include <stdbool.h>
typedef struct { pthread_mutex_t gate; pthread_cond_t wake; pthread_t owner; unsigned depth; bool recursive; } mutex_t;
#define MUTEX_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (pthread_t)0, 0, false}
#define MUTEX_INITIALIZER_RECURSIVE {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (pthread_t)0, 0, true}
int mutex_lock_timed(mutex_t *, unsigned);
int mutex_lock(mutex_t *);
int mutex_unlock(mutex_t *);
#endif
