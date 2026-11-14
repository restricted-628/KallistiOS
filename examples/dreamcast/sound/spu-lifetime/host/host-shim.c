/* Host primitives only. Request state machine and target probe compile unchanged.
 * Worker semantics model KOS pending/quit/join; pthread scheduling is not SH-4. */
#include "host-shim.h"
#include <dc/spu.h>
#include <stdio.h>
#include <sched.h>
#include <unistd.h>

static pthread_mutex_t irq_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned irq_depth;
irq_mask_t irq_disable(void) {
    unsigned prior = irq_depth;
    if(!irq_depth++) assert(!pthread_mutex_lock(&irq_lock));
    return prior;
}
void irq_restore(irq_mask_t prior) {
    assert(irq_depth && prior < irq_depth);
    irq_depth = prior;
    if(!prior) assert(!pthread_mutex_unlock(&irq_lock));
}
static struct timespec until_ms(unsigned ms) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)(ms % 1000) * 1000000;
    t.tv_sec += ms / 1000 + t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}
uint64_t timer_ms_gettime64(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int sem_init(semaphore_t *s, int n) {
    assert(!pthread_mutex_init(&s->lock, NULL));
    assert(!pthread_cond_init(&s->wake, NULL)); s->count = n; return 0;
}
int sem_destroy(semaphore_t *s) {
    assert(!pthread_cond_destroy(&s->wake));
    assert(!pthread_mutex_destroy(&s->lock)); return 0;
}
int sem_wait_timed(semaphore_t *s, int ms) {
    int result = 0;
    struct timespec deadline = until_ms(ms);
    pthread_mutex_lock(&s->lock);
    while(!s->count && !result)
        result = ms ? pthread_cond_timedwait(&s->wake, &s->lock, &deadline)
                    : pthread_cond_wait(&s->wake, &s->lock);
    if(!result) --s->count;
    pthread_mutex_unlock(&s->lock);
    if(result) { errno = result; return -1; } return 0;
}
int sem_wait(semaphore_t *s) { return sem_wait_timed(s, 0); }
void sem_signal(semaphore_t *s) {
    pthread_mutex_lock(&s->lock); ++s->count;
    pthread_cond_signal(&s->wake); pthread_mutex_unlock(&s->lock);
}
struct waiter { void *object; bool ready; pthread_cond_t cv; struct waiter *next; };
static struct waiter *waiters;
int genwait_wait(void *object, const char *label, int ms) {
    (void)label;
    assert(irq_depth == 1);
    struct waiter w = {object, false, PTHREAD_COND_INITIALIZER, waiters};
    struct timespec deadline = until_ms(ms);
    int result = 0;
    waiters = &w;
    while(!w.ready && !result)
        result = ms ? pthread_cond_timedwait(&w.cv, &irq_lock, &deadline)
                    : pthread_cond_wait(&w.cv, &irq_lock);
    struct waiter **p = &waiters;
    while(*p != &w) p = &(*p)->next;
    *p = w.next;
    pthread_cond_destroy(&w.cv);
    if(result) { errno = result == ETIMEDOUT ? EAGAIN : result; return -1; }
    return 0;
}
void genwait_wake_all(void *object) {
    irq_disable_scoped();
    for(struct waiter *w = waiters; w; w = w->next)
        if(w->object == object) { w->ready = true; pthread_cond_signal(&w->cv); }
}
void genwait_wake_one(void *object) { genwait_wake_all(object); }
static _Thread_local kthread_t *current;
static kthread_t main_thread;
static void *thread_entry(void *p) {
    current = p;
    return current->fn(current->data);
}
kthread_t *thd_get_current(void) { return current ? current : &main_thread; }
kthread_t *thd_create_ex(const kthread_attr_t *attr, void *(*fn)(void *), void *data) {
    (void)attr;
    kthread_t *t = calloc(1, sizeof(*t)); assert(t);
    t->fn = fn; t->data = data;
    assert(!pthread_create(&t->id, NULL, thread_entry, t)); return t;
}
kthread_t *thd_create(bool detached, void *(*fn)(void *), void *data) {
    assert(!detached); return thd_create_ex(NULL, fn, data);
}
int thd_join(kthread_t *t, void **result) {
    int rc = pthread_join(t->id, result); assert(!rc); free(t); return rc;
}
void thd_sleep(unsigned ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
void thd_pass(void) { sched_yield(); }
struct kthread_worker {
    kthread_t *thread; pthread_mutex_t lock; pthread_cond_t wake;
    bool pending, quit; void (*fn)(void *); void *data;
};
static void *worker_entry(void *p) {
    kthread_worker_t *w = p;
    pthread_mutex_lock(&w->lock);
    for(;;) {
        while(!w->pending && !w->quit) pthread_cond_wait(&w->wake, &w->lock);
        if(w->quit) break;
        w->pending = false;
        pthread_mutex_unlock(&w->lock); w->fn(w->data); pthread_mutex_lock(&w->lock);
    }
    pthread_mutex_unlock(&w->lock); return NULL;
}
kthread_worker_t *thd_worker_create_ex(const kthread_attr_t *a, void (*fn)(void *), void *data) {
    kthread_worker_t *w = calloc(1, sizeof(*w)); assert(w);
    pthread_mutex_init(&w->lock, NULL); pthread_cond_init(&w->wake, NULL);
    w->fn = fn; w->data = data; w->thread = thd_create_ex(a, worker_entry, w); return w;
}
void thd_worker_wakeup(kthread_worker_t *w) {
    pthread_mutex_lock(&w->lock); w->pending = true;
    pthread_cond_signal(&w->wake); pthread_mutex_unlock(&w->lock);
}
void thd_worker_destroy(kthread_worker_t *w) {
    pthread_mutex_lock(&w->lock); w->quit = true;
    pthread_cond_signal(&w->wake); pthread_mutex_unlock(&w->lock);
    thd_join(w->thread, NULL); pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->lock); free(w);
}
kthread_t *thd_worker_get_thread(kthread_worker_t *w) { return w->thread; }
void spu_memload(uintptr_t to, const void *from, size_t length) {
    (void)to; (void)from; (void)length; abort();
}
void spu_memread(void *to, uintptr_t from, size_t length) {
    (void)to; (void)from; (void)length; abort();
}
static unsigned disabled, cleared;
void spu_disable(void) { ++disabled; }
void spu_memset_sq(uintptr_t dst, uint32_t value, size_t length) {
    assert(!dst && !value && length == SPU_RAM_SIZE_2MB); ++cleared;
}
int probe_main(int, char **);
void spu_transfer_system_init(void);
int main(void) {
    alarm(20);
    spu_transfer_system_init();
    int result = probe_main(0, NULL);
    assert(disabled == 1 && cleared == 1);
    printf("HOST: disable/clear only after final successful shutdown PASS\n");
    return result;
}
