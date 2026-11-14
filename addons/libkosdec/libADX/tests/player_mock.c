/* Real pthread producer and latched wake model; no DMA/device certification. */
#include <kosdec/adx_player.h>
#include <kos/timer.h>
#include "player_mock.h"
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
enum mock_failure mock_failure;
snd_stream_callback_t mock_callback;
void *mock_userdata;
snd_stream_config_t mock_output;
uint32_t mock_update_fields;
unsigned mock_allocs,mock_starts,mock_stops,mock_joins,mock_updates;
size_t mock_frames;
int mock_async_error;
bool mock_status_failure;
int16_t mock_pcm[65536];
static int sound_bytes;
static bool stopped;
struct fiber_service {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    fiber_service_entry_t entry;
    void *data;
    bool stop,pending,finished;
};
struct fiber_service_executor {struct fiber_service *service;pthread_t thread;bool started;};
static int error(void) {errno=EIO;return -1;}
void mock_reset(void) {
    mock_failure=MOCK_OK;mock_callback=NULL;mock_userdata=NULL;mock_frames=0;
    mock_allocs=mock_starts=mock_stops=mock_joins=mock_updates=0;stopped=false;
    mock_async_error=0;mock_status_failure=false;mock_update_fields=0;
}
void mock_pause(void) {struct timespec t={0,1000000};nanosleep(&t,NULL);}
uint64_t timer_ms_gettime64(void) {
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
fiber_service_executor_t *fiber_service_executor_create(void) {
    if(mock_failure==MOCK_CREATE){error();return NULL;}
    return calloc(1,sizeof(fiber_service_executor_t));
}
fiber_service_t *fiber_service_add(fiber_service_executor_t *e,void *stack,size_t bytes,
                                  fiber_service_entry_t fn,void *data) {
    assert(stack && bytes>=16384);
    if(mock_failure==MOCK_ADD){error();return NULL;}
    fiber_service_t *s=calloc(1,sizeof(*s));assert(s);
    pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cond,NULL);
    s->entry=fn;s->data=data;e->service=s;return s;
}
static void *run(void *arg) {
    fiber_service_t *s=arg;fiber_service_wait(s,0);s->entry(s,s->data);
    pthread_mutex_lock(&s->lock);s->finished=true;pthread_mutex_unlock(&s->lock);return NULL;
}
int fiber_service_executor_start(fiber_service_executor_t *e,const void *attr) {
    (void)attr;if(mock_failure==MOCK_EXEC_START)return error();
    assert(!pthread_create(&e->thread,NULL,run,e->service));e->started=true;return 0;
}
int fiber_service_wake(fiber_service_t *s) {
    pthread_mutex_lock(&s->lock);
    if(s->finished || s->stop){pthread_mutex_unlock(&s->lock);errno=ECANCELED;return -1;}
    s->pending=true;pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);return 0;
}
int fiber_service_request_stop(fiber_service_t *s) {
    pthread_mutex_lock(&s->lock);s->stop=true;s->pending=true;pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);return 0;
}
bool fiber_service_stop_requested(fiber_service_t *s) {
    pthread_mutex_lock(&s->lock);bool stop=s->stop;pthread_mutex_unlock(&s->lock);return stop;
}
int fiber_service_wait(fiber_service_t *s,uint64_t deadline) {
    pthread_mutex_lock(&s->lock);
    while(!s->stop && !s->pending) {
        if(deadline) {
            struct timespec t={(time_t)(deadline/1000),(long)(deadline%1000)*1000000};
            if(pthread_cond_timedwait(&s->cond,&s->lock,&t)==ETIMEDOUT)break;
        }
        else pthread_cond_wait(&s->cond,&s->lock);
    }
    bool stop=s->stop;s->pending=false;pthread_mutex_unlock(&s->lock);
    if(stop){errno=ECANCELED;return -1;}return 0;
}
int fiber_service_yield(fiber_service_t *s) {
    sched_yield();return fiber_service_stop_requested(s)?-1:0;
}
int fiber_service_executor_destroy(fiber_service_executor_t *e) {
    if(mock_failure==MOCK_JOIN)return error();
    /* Final stop detaches callbacks. Source release keeps output running but
       disables PCM/wakes before joining the old producer. */
    assert((!mock_callback && !mock_userdata) ||
           (mock_userdata && !((struct kosdec_adx_player *)mock_userdata)->pcm_enabled));
    if(e->started){fiber_service_request_stop(e->service);assert(!pthread_join(e->thread,NULL));}
    if(e->service){pthread_cond_destroy(&e->service->cond);pthread_mutex_destroy(&e->service->lock);free(e->service);}
    free(e);++mock_joins;return 0;
}
int snd_stream_config_init(snd_stream_config_t *c) {
    memset(c,0,sizeof(*c));c->sample_rate=44100;c->channels=1;c->transfer_timeout_ms=1000;
    for(unsigned i=0;i<2;++i){c->channel[i].volume=255;c->channel[i].pan=128;}return 0;
}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int bytes) {
    if(mock_failure==MOCK_ALLOC){error();return SND_STREAM_INVALID;}
    ++mock_allocs;mock_callback=cb;sound_bytes=bytes;return 7;
}
void snd_stream_set_userdata(snd_stream_hnd_t h,void *v) {(void)h;mock_userdata=v;}
void *snd_stream_get_userdata(snd_stream_hnd_t h) {(void)h;return mock_userdata;}
void snd_stream_set_callback(snd_stream_hnd_t h,snd_stream_callback_t cb) {
    (void)h;assert(cb || stopped);mock_callback=cb;
}
static int fill(void) {
    int got=-1,requested=sound_bytes/2*mock_output.channels;
    if(mock_failure==MOCK_CALLBACK)requested=sound_bytes+4;
    int16_t *data=mock_callback(7,requested,&got);
    assert(got>=0 && got<=requested && got%(mock_output.channels*2)==0 && (data || !got));
    if(got) {
        assert((uintptr_t)data%2==0 && (uintptr_t)data%32!=0);
        assert(mock_frames*mock_output.channels+(size_t)got/2<=65536);
        memcpy(mock_pcm+mock_frames*mock_output.channels,data,(size_t)got);
        mock_frames+=(unsigned)got/(mock_output.channels*2);
    }
    if(got<requested){errno=ENODATA;return -1;}return 0;
}
int snd_stream_start_ex(snd_stream_hnd_t h,const snd_stream_config_t *c) {
    (void)h;mock_output=*c;++mock_starts;
    if(mock_failure==MOCK_SOUND_START){fill();return error();}
    fill();fill();return 0;
}
int snd_stream_poll_ex(snd_stream_hnd_t h) {
    (void)h;if(mock_failure==MOCK_POLL)return error();return fill();
}
int snd_stream_update(snd_stream_hnd_t h,const snd_stream_config_t *c,uint32_t fields) {
    (void)h;assert(fields==(SND_CHANNEL_UPDATE_VOLUME|SND_CHANNEL_UPDATE_PAN) || fields==SND_CHANNEL_UPDATE_FREQUENCY || fields==SND_CHANNEL_UPDATE_PAN);
    mock_update_fields=fields;++mock_updates;if(mock_failure==MOCK_UPDATE)return error();mock_output=*c;return 0;
}
int snd_stream_stop_ex(snd_stream_hnd_t h,uint32_t timeout) {
    (void)h;assert(timeout);++mock_stops;
    if(mock_failure==MOCK_STOP)return error();stopped=true;mock_async_error=0;return 0;
}
int snd_stream_get_status(snd_stream_hnd_t h,snd_stream_status_t *status) {
    (void)h;if(mock_status_failure){errno=EPIPE;return -1;}
    status->last_error=mock_async_error;return 0;
}
