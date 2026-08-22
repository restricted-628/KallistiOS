#ifndef G1_TEST_SEM_H
#define G1_TEST_SEM_H

#include <stdint.h>

typedef struct semaphore {
    int initialized;
    int count;
} semaphore_t;

#define SEM_INITIALIZER(value) { 1, (value) }

int sem_wait(semaphore_t *semaphore);
int sem_wait_timed(semaphore_t *semaphore, unsigned int timeout);
int sem_trywait(semaphore_t *semaphore);
int sem_wait_irqsafe(semaphore_t *semaphore);
int sem_signal(semaphore_t *semaphore);

#endif /* G1_TEST_SEM_H */
