/* Narrow deterministic dependency model for exact extracted production bodies.
 * This is control-flow evidence, not thread scheduling or AICA fetch evidence. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
typedef int mutex_t;
typedef int condvar_t;
typedef int semaphore_t;
typedef int snd_stream_hnd_t;
typedef int kthread_t;
typedef struct { bool playing; } snd_channel_status_ex_t;
typedef struct { int unused; } snd_driver_status_t;
typedef struct filter { TAILQ_ENTRY(filter) lent; } filter_t;
enum { SND_STREAM_STATE_PLAYING, SND_STREAM_STATE_STOPPED, SND_STREAM_STATE_ERROR };
typedef struct {
    const void *service_owner; bool initted; int callback_active, channels, ch[2];
    int queue_gate_held, queued_start_channels, dma_pending, dma_error, state, last_error;
    uintptr_t spu_ram_sch[2]; TAILQ_HEAD(, filter) filters;
} strchan_t;
static strchan_t streams[1];
static mutex_t stream_state_mutex;
static uint64_t ticks;
static int stop_error, barrier_error, dma_error, status_error;
static bool playing, fetch_live;
static unsigned mem_frees, ch_frees, heap_frees, joins, gates, releases;
static char trace[128]; static unsigned trace_size;
static void mark(char c) { assert(trace_size + 1 < sizeof(trace)); trace[trace_size++] = c; trace[trace_size] = 0; }
static int mutex_lock(mutex_t *m) { ++*m; return 0; }
static int mutex_unlock(mutex_t *m) { assert(*m > 0); --*m; return 0; }
static uint64_t timer_ms_gettime64(void) { return ticks; }
static void thd_pass(void) { ++ticks; }
static int checked_handle(int h) { if(h != 0 || !streams[h].initted) { errno = EINVAL; return -1; } return 0; }
static int error_or_ok(int e) { if(e) { errno = e; return -1; } return 0; }
static void snd_sh4_to_aica_stop(void) { ++gates; mark('G'); }
static void snd_sh4_to_aica_start(void) { ++releases; mark('R'); }
static int snd_channel_stop(int ch) { (void)ch; mark('S'); return error_or_ok(stop_error); }
static int snd_driver_get_status(snd_driver_status_t *s, uint32_t t) { (void)s; assert(t); mark('B'); return error_or_ok(barrier_error); }
static int stream_dma_wait(strchan_t *s, uint32_t t) { assert(t); mark('D'); if(dma_error) return error_or_ok(dma_error); s->dma_pending = 0; return 0; }
static int snd_channel_get_status_ex(int ch, snd_channel_status_ex_t *s) { (void)ch; s->playing = playing; mark('P'); return error_or_ok(status_error); }
static void stream_set_error(strchan_t *s, int e) { s->state = SND_STREAM_STATE_ERROR; s->last_error = e; }
static void snd_mem_free(uintptr_t a) { assert(a == 0x40000); ++mem_frees; mark('M'); }
static void snd_sfx_chn_free(int c) { assert(c == 0); ++ch_frees; mark('C'); }
static void tracked_free(void *p) { ++heap_frees; mark('F'); free(p); }
#define free tracked_free
int snd_stream_stop_ex(snd_stream_hnd_t, uint32_t);

enum { SND_STREAM_SERVICE_RUNNING, SND_STREAM_SERVICE_STOPPING, SND_STREAM_SERVICE_STOPPED };
typedef struct {
    mutex_t mutex; condvar_t idle; semaphore_t work; kthread_t *thread; int state;
} snd_stream_service_t;
static bool self_context, finish_service;
static kthread_t service_thread, main_thread;
static int require_thread_context(void) { return 0; }
static kthread_t *thd_get_current(void) { return self_context ? &service_thread : &main_thread; }
static void sem_signal(semaphore_t *s) { ++*s; mark('W'); }
static int cond_wait_timed(condvar_t *c, mutex_t *m, int timeout) {
    (void)c; assert(*m == 1 && timeout > 0); mark('T');
    if(finish_service) {
        snd_stream_service_t *s = (void *)((char *)m - offsetof(snd_stream_service_t, mutex));
        s->state = SND_STREAM_SERVICE_STOPPED; return 0;
    }
    ticks += timeout; errno = ETIMEDOUT; return -1;
}
static int thd_join(kthread_t *t, void *out) { (void)out; assert(t == &service_thread); ++joins; mark('J'); return 0; }
static void sem_destroy(semaphore_t *s) { (void)s; mark('s'); }
static void cond_destroy(condvar_t *c) { (void)c; mark('c'); }
static int mutex_destroy(mutex_t *m) { assert(!*m); mark('m'); return 0; }
