#pragma once
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef unsigned irq_mask_t;
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t);
static inline void irq_cleanup(irq_mask_t *p) { irq_restore(*p); }
#define irq_disable_scoped() irq_mask_t irq_scope __attribute__((cleanup(irq_cleanup))) = irq_disable()
#define irq_inside_int() false
#define __is_aligned(p, n) (!((uintptr_t)(p) & ((n) - 1)))
#define HW_TYPE_RETAIL 0
#define hardware_sys_mode(p) HW_TYPE_RETAIL
#define MEM_AREA_P2_BASE 0xa0000000u

typedef pthread_mutex_t mutex_t;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
static inline int mutex_lock(mutex_t *m) { return pthread_mutex_lock(m); }
static inline int mutex_unlock(mutex_t *m) { return pthread_mutex_unlock(m); }
static inline void mutex_cleanup(mutex_t **m) { mutex_unlock(*m); }
#define mutex_lock_scoped(m) mutex_t *mutex_scope __attribute__((cleanup(mutex_cleanup))) = (m); mutex_lock(mutex_scope)
typedef pthread_cond_t condvar_t;
#define COND_INITIALIZER PTHREAD_COND_INITIALIZER
#define cond_wait pthread_cond_wait
#define cond_broadcast pthread_cond_broadcast

typedef struct { pthread_mutex_t lock; pthread_cond_t wake; unsigned count; } semaphore_t;
#define SEM_INITIALIZER(n) {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (n)}
int sem_init(semaphore_t *, int);
int sem_destroy(semaphore_t *);
int sem_wait(semaphore_t *);
int sem_wait_timed(semaphore_t *, int);
void sem_signal(semaphore_t *);
int genwait_wait(void *, const char *, int);
void genwait_wake_all(void *);
void genwait_wake_one(void *);
uint64_t timer_ms_gettime64(void);

typedef struct kthread { pthread_t id; void *(*fn)(void *); void *data; } kthread_t;
typedef struct { size_t stack_size; int prio; const char *label; } kthread_attr_t;
#define PRIO_DEFAULT 10
kthread_t *thd_create(bool, void *(*)(void *), void *);
kthread_t *thd_create_ex(const kthread_attr_t *, void *(*)(void *), void *);
kthread_t *thd_get_current(void);
int thd_join(kthread_t *, void **);
void thd_sleep(unsigned);
void thd_pass(void);
typedef struct kthread_worker kthread_worker_t;
kthread_worker_t *thd_worker_create_ex(const kthread_attr_t *, void (*)(void *), void *);
void thd_worker_wakeup(kthread_worker_t *);
void thd_worker_destroy(kthread_worker_t *);
kthread_t *thd_worker_get_thread(kthread_worker_t *);
