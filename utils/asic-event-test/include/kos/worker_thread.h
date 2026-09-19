#ifndef __KOS_WORKER_THREAD_H
#define __KOS_WORKER_THREAD_H

#define KTHREAD_LABEL_SIZE 256

typedef struct kthread {
    char label[KTHREAD_LABEL_SIZE];
} kthread_t;

typedef struct kthread_worker kthread_worker_t;

kthread_worker_t *thd_worker_create(void (*routine)(void *), void *data);
void thd_worker_destroy(kthread_worker_t *worker);
void thd_worker_wakeup(kthread_worker_t *worker);
kthread_t *thd_worker_get_thread(kthread_worker_t *worker);
kthread_t *thd_get_current(void);

#endif
