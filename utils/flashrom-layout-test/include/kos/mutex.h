#ifndef FLASHROM_LAYOUT_TEST_MUTEX_H
#define FLASHROM_LAYOUT_TEST_MUTEX_H

typedef struct mutex {
    int locked;
} mutex_t;

#define MUTEX_INITIALIZER { 0 }

int mutex_lock(mutex_t *mutex);
int mutex_lock_irqsafe(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

#endif
