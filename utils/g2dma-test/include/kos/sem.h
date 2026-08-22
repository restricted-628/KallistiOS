#ifndef __KOS_SEM_H
#define __KOS_SEM_H

typedef struct semaphore {
    int count;
    int initialized;
} semaphore_t;

int sem_init(semaphore_t *semaphore, int count);
int sem_destroy(semaphore_t *semaphore);
int sem_wait_timed(semaphore_t *semaphore, unsigned int timeout);
int sem_trywait(semaphore_t *semaphore);
int sem_signal(semaphore_t *semaphore);
int sem_count(const semaphore_t *semaphore);

static inline int sem_wait(semaphore_t *semaphore) {
    return sem_wait_timed(semaphore, 0);
}

#endif
