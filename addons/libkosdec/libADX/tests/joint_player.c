#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
static unsigned checks;
#define CHECK(x) do { __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED); if(!(x)) { \
    fprintf(stderr,"joint_player:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_player_config config;
    uint8_t *data,ring[192]; size_t bytes,position;
    unsigned calls,waits_at_end;
    bool wait,fail,worker_only;
    pthread_t controller;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
/* Assertions called by a worker use an atomic counter too. */
static void worker_check(int value,int line) {
    __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);
    if(!value) {fprintf(stderr,"joint_player feed:%d\n",line);exit(1);}
}
#define WC(x) worker_check(!!(x),__LINE__)
static enum kosdec_adx_joint_feed_status feed(void *ctx,struct kosdec_adx_joint *j) {
    struct fixture *f=ctx;
    WC(j==&f->joint);
    WC(!f->worker_only || !pthread_equal(pthread_self(),f->controller));
    __atomic_add_fetch(&f->calls,1,__ATOMIC_RELAXED);
    if(__atomic_load_n(&f->fail,__ATOMIC_ACQUIRE)) return KOSDEC_ADX_JOINT_FEED_ERROR;
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE)) return KOSDEC_ADX_JOINT_FEED_WAIT;
    if(f->position==f->bytes) {
        if(f->waits_at_end++<3) return KOSDEC_ADX_JOINT_FEED_WAIT;
        return KOSDEC_ADX_JOINT_FEED_END;
    }
    struct kosdec_adx_chunk c;
    size_t n=f->bytes-f->position; if(n>17)n=17;
    WC(kosdec_adx_joint_get(j,KOSDEC_ADX_JOINT_FREE,n,&c)==KOSDEC_ADX_JOINT_OK);
    if(c.bytes) memcpy(c.data,f->data+f->position,c.bytes);
    WC(kosdec_adx_joint_put(j,&c)==KOSDEC_ADX_JOINT_OK);
    f->position+=c.bytes;
    return KOSDEC_ADX_JOINT_FEED_MORE;
}
static unsigned calls(struct fixture *f) {return __atomic_load_n(&f->calls,__ATOMIC_RELAXED);}
static void source(struct fixture *f) {
    f->position=0;f->waits_at_end=0;
    OK(kosdec_adx_joint_ring_init(&f->joint,f->ring,128,64));
    OK(kosdec_adx_joint_reader_init(&f->reader,&f->joint));
    CHECK(kosdec_adx_joint_reader_set_feed(&f->reader,feed,f)==KOSDEC_ADX_DONE);
}
static void init(struct fixture *f,unsigned channels,bool worker) {
    memset(f,0,sizeof(*f));mock_reset();f->data=stream(50,18,channels,32,65,&f->bytes);
    f->controller=pthread_self();f->worker_only=worker;source(f);
    f->config=(struct kosdec_adx_player_config){.pcm=f->pcm,.scratch=f->scratch,
        .pcm_samples=1024,.capacity=512,.scratch_samples=257,.stack=f->stack,
        .stack_bytes=sizeof(f->stack),.sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init_joint(&f->player,&f->reader,&f->config));
}
static struct kosdec_adx_player_info info(struct fixture *f) {
    struct kosdec_adx_player_info i;CHECK(!kosdec_adx_player_get_info(&f->player,&i));return i;
}
static int finish(struct fixture *f) {
    for(unsigned i=0;i<2000;++i) {
        if(kosdec_adx_player_poll(&f->player)<0)return -1;
        if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)return 0;
        mock_pause();
    }
    CHECK(0);return -1;
}
static void close_source(struct fixture *f) {
    OK(kosdec_adx_joint_reader_close(&f->reader));OK(kosdec_adx_joint_close(&f->joint));
}
static void playback(void) {
    for(unsigned ch=1;ch<=2;++ch) {
        struct fixture f;init(&f,ch,true);
        CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
        CHECK(info(&f).decoder==KOSDEC_ADX_DONE && mock_frames==65);
        CHECK(mock_allocs==1 && mock_starts==1);
        CHECK(!kosdec_adx_player_release_source(&f.player));
        unsigned before=calls(&f);close_source(&f);source(&f);
        CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));CHECK(!finish(&f));
        CHECK(calls(&f)>before && mock_frames==130 && mock_allocs==1 && mock_starts==1);
        for(size_t i=0;i<130;++i)for(unsigned c=0;c<ch;++c)
            CHECK(mock_pcm[i*ch+c]==golden[c][i%65]);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(mock_joins==2 && info(&f).retained_sound==7);
        before=calls(&f);for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before);
        close_source(&f);free(f.data);
    }
}
static void failures(void) {
    for(unsigned fail=0;fail<2;++fail) {
        struct fixture f;init(&f,2,true);f.wait=!fail;f.fail=fail;
        CHECK(!kosdec_adx_player_start(&f.player));
        if(fail) {CHECK(finish(&f)<0);CHECK(info(&f).first_error==EIO);}
        else {
            for(unsigned i=0;i<2000 && !calls(&f);++i)mock_pause();
            CHECK(calls(&f)>0);CHECK(!kosdec_adx_player_cancel_source(&f.player));
        }
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        unsigned before=calls(&f);for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before);
        CHECK(!mock_allocs && mock_joins==1);
        close_source(&f);free(f.data);
    }
    struct fixture f;init(&f,1,true);
    CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
    CHECK(!kosdec_adx_player_release_source(&f.player));close_source(&f);free(f.data);
    f.data=stream(50,18,2,32,65,&f.bytes);source(&f);
    CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));
    CHECK(finish(&f)<0 && info(&f).first_error==EPROTO && mock_frames==65);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));close_source(&f);free(f.data);
}
static struct kosdec_adx_input_result unused_input(void *c,uint32_t o,uint8_t *d,size_t n) {
    (void)c;(void)o;(void)d;(void)n;CHECK(0);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
}
static void service(void) {
    struct fixture f;init(&f,2,false);struct kosdec_adx_service s;
    CHECK(kosdec_adx_service_init_joint(&s,&f.reader,f.pcm,1024,32)==KOSDEC_ADX_MORE);
    for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(!calls(&f));
    int16_t pcm[64];
    CHECK(kosdec_adx_joint_reader_read(&f.reader,pcm,0).status==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(!calls(&f));
    struct kosdec_adx_command cmd={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_MORE && !calls(&f));
    for(unsigned i=0;i<100 && s.pipe.write_cursor!=32;++i) {
        unsigned before=calls(&f);CHECK(kosdec_adx_service_step(&s).status<KOSDEC_ADX_DONE);
        CHECK(calls(&f)-before<=1);
    }
    CHECK(s.pipe.write_cursor==32);unsigned before=calls(&f);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_NEED_OUTPUT && calls(&f)==before);
    CHECK(kosdec_adx_pipe_read(&s.pipe,pcm,32).frames==32 && calls(&f)==before);
    for(unsigned i=0;i<32;++i)for(unsigned c=0;c<2;++c) CHECK(pcm[2*i+c]==golden[c][i]);
    cmd.enabled=true;CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE && calls(&f)==before);
    for(unsigned kind=KOSDEC_ADX_SEQUENCE_REPEAT;kind<=KOSDEC_ADX_ENQUEUE;++kind) {
        cmd.kind=(enum kosdec_adx_command_kind)kind;cmd.source.read_at=unused_input;cmd.source.bytes=1;
        CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_UNSUPPORTED);
    }
    kosdec_adx_service_request_cancel(&s);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_CANCELLED && calls(&f)==before);
    close_source(&f);free(f.data);
}
int main(void) {
    service();playback();failures();
    printf("joint_player: %u checks passed, PCM/controls/feed ownership/rebind/errors\n",checks);
    return 0;
}
