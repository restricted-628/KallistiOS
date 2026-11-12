/* Original command ownership, terminal races and PCM continuity regressions. */
#include <kosdec/adx_service.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#include "memory_vectors.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"SERVICE_FAIL line=%u\n",(unsigned)__LINE__);abort(); } } while(0)
struct input { uint8_t *data;size_t bytes;unsigned calls,fragment;bool wait,fault; };
struct fixture {
    struct input in;
    struct kosdec_adx_service service;
    struct kosdec_adx_stream stream;
    struct kosdec_adx_sequence sequence;
    int16_t storage[128];
};
static struct kosdec_adx_input_result provider(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *p=ctx;++p->calls;
    CHECK(off<p->bytes && cap<=p->bytes-off && cap<=512);
    if(p->fault)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(p->wait && p->calls%3==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment)cap=p->fragment;
    memcpy(dst,p->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static struct kosdec_adx_source source(struct fixture *f) {
    return (struct kosdec_adx_source){provider,&f->in,(uint32_t)f->in.bytes,77};
}
static void init(struct fixture *f,unsigned ch,bool sequence,bool loop) {
    memset(f,0,sizeof(*f));f->in.fragment=17;f->in.wait=true;
    f->in.data=stream(50,18,ch,32,61,&f->in.bytes);
    if(!loop)memset(f->in.data+20,0,24);
    if(sequence) {
        kosdec_adx_sequence_init(&f->sequence,false);
        struct kosdec_adx_source src=source(f);
        CHECK(kosdec_adx_sequence_enqueue(&f->sequence,&src)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_sequence_start(&f->sequence)==KOSDEC_ADX_MORE);
    }
    else kosdec_adx_stream_init(&f->stream,provider,&f->in,(uint32_t)f->in.bytes,loop);
    CHECK(kosdec_adx_service_init(&f->service,sequence?NULL:&f->stream,
        sequence?&f->sequence:NULL,f->storage,128,64)==KOSDEC_ADX_MORE);
}
static void command(struct fixture *f,struct kosdec_adx_command c,enum kosdec_adx_status expected) {
    enum kosdec_adx_status result=KOSDEC_ADX_INVALID;
    CHECK(!kosdec_adx_service_take(&f->service,&result) && result==KOSDEC_ADX_INVALID);
    CHECK(kosdec_adx_service_post(&f->service,&c)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_post(&f->service,&c)==KOSDEC_ADX_NEED_OUTPUT);
    memset(&c,0xff,sizeof(c)); /* Posted payload is copied. */
    unsigned calls=f->in.calls;
    struct kosdec_adx_stream_result r=kosdec_adx_service_step(&f->service);
    CHECK(r.status==KOSDEC_ADX_MORE && !r.frames && calls==f->in.calls);
    CHECK(kosdec_adx_service_take(&f->service,&result) && result==expected);
    CHECK(!kosdec_adx_service_take(&f->service,&result));
}
static void pause(struct fixture *f,bool enabled) {
    command(f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=enabled},KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_get_state(&f->service).paused==enabled);
}
static void combinations(void) {
    for(unsigned ch=1;ch<=2;++ch)for(unsigned seq=0;seq<2;++seq)
    for(unsigned fragment=1;fragment<=512;fragment*=8) {
        struct fixture f;init(&f,ch,seq,false);f.in.fragment=fragment;
        for(unsigned n=0;n<10;++n)CHECK(!kosdec_adx_service_step(&f.service).frames && !f.in.calls);
        if(seq) {
            for(unsigned n=0;n<2;++n)command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_ENQUEUE,.source=source(&f)},KOSDEC_ADX_MORE);
            command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_RELEASE_LOOP},KOSDEC_ADX_UNSUPPORTED);
        }
        else command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_SEQUENCE_REPEAT},KOSDEC_ADX_UNSUPPORTED);
        pause(&f,false);size_t total=0;bool done=false;
        for(unsigned n=0;n<10000;++n) {
            if(n%11==0 && kosdec_adx_service_get_state(&f.service).status==KOSDEC_ADX_MORE) {
                pause(&f,true);unsigned calls=f.in.calls;
                CHECK(!kosdec_adx_service_step(&f.service).frames && calls==f.in.calls);
                pause(&f,false);
            }
            kosdec_adx_service_step(&f.service);
            int16_t pcm[14];struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&f.service.pipe,pcm,7);
            for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<ch;++k)
                CHECK(pcm[j*ch+k]==golden[k][(total+j)%65]);
            total+=r.frames;
            if(r.drained){CHECK(r.producer==KOSDEC_ADX_DONE);done=true;break;}
        }
        CHECK(done && total==(seq?195u:65u));
        CHECK(kosdec_adx_service_get_state(&f.service).status==KOSDEC_ADX_DONE);
        struct kosdec_adx_command c={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
        CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_DONE);
        kosdec_adx_service_request_cancel(&f.service);
        CHECK(kosdec_adx_service_step(&f.service).status==KOSDEC_ADX_DONE);
        free(f.in.data);
    }
}
static void lifecycle(void) {
    struct fixture f;init(&f,2,false,false);
    struct kosdec_adx_service copy=f.service;
    CHECK(kosdec_adx_service_init(&f.service,NULL,NULL,f.storage,128,64)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&copy,&f.service,sizeof(copy)));
    struct kosdec_adx_command c={.kind=(enum kosdec_adx_command_kind)99};
    CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_BAD_ARGUMENT);
    c.kind=KOSDEC_ADX_ENQUEUE;
    CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&copy,&f.service,sizeof(copy)));
    pause(&f,false);
    while(f.service.pipe.write_cursor<64)kosdec_adx_service_step(&f.service);
    pause(&f,true);
    c=(struct kosdec_adx_command){.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_MORE);
    unsigned calls=f.in.calls;kosdec_adx_service_request_cancel(&f.service);
    CHECK(kosdec_adx_service_step(&f.service).status==KOSDEC_ADX_CANCELLED && f.in.calls==calls);
    enum kosdec_adx_status result;
    CHECK(kosdec_adx_service_take(&f.service,&result) && result==KOSDEC_ADX_CANCELLED);
    int16_t pcm[128];struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&f.service.pipe,pcm,64);
    CHECK(r.frames==64 && r.drained && r.producer==KOSDEC_ADX_CANCELLED);
    for(unsigned j=0;j<64;++j)for(unsigned k=0;k<2;++k)CHECK(pcm[j*2+k]==golden[k][j]);
    free(f.in.data);
    init(&f,1,false,false);pause(&f,false);f.in.fault=true;
    CHECK(kosdec_adx_service_step(&f.service).status==KOSDEC_ADX_IO_ERROR);
    CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_IO_ERROR);free(f.in.data);
    init(&f,1,true,false);
    for(unsigned n=1;n<16;++n)command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_ENQUEUE,.source=source(&f)},KOSDEC_ADX_MORE);
    command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_ENQUEUE,.source=source(&f)},KOSDEC_ADX_NEED_OUTPUT);
    command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_SEQUENCE_REPEAT,.enabled=true},KOSDEC_ADX_MORE);
    CHECK(f.sequence.repeat && f.sequence.count==16);
    command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_SEQUENCE_REPEAT},KOSDEC_ADX_MORE);
    CHECK(!f.sequence.repeat);free(f.in.data);
    /* A completed receipt survives closure and still reports the applied result. */
    init(&f,1,false,false);CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_MORE);
    kosdec_adx_service_step(&f.service);kosdec_adx_service_request_cancel(&f.service);
    kosdec_adx_service_step(&f.service);
    CHECK(kosdec_adx_service_take(&f.service,&result) && result==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_post(&f.service,&c)==KOSDEC_ADX_CANCELLED);free(f.in.data);
}
static void loop_release(void) {
    struct fixture f;init(&f,2,false,true);pause(&f,false);
    size_t total=0;bool released=false,done=false;
    for(unsigned n=0;n<10000;++n) {
        if(f.stream.wraps==2 && !released) {
            command(&f,(struct kosdec_adx_command){.kind=KOSDEC_ADX_RELEASE_LOOP},KOSDEC_ADX_MORE);
            released=true;
        }
        kosdec_adx_service_step(&f.service);int16_t pcm[14];
        struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&f.service.pipe,pcm,7);
        for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<2;++k)
            CHECK(total+j<131 && pcm[j*2+k]==vectors[4].pcm[k][total+j]);
        total+=r.frames;if(r.drained){CHECK(r.producer==KOSDEC_ADX_DONE);done=true;break;}
    }
    CHECK(done && released && total==123);free(f.in.data);
}
struct racing { struct fixture *f;size_t frames; };
static void *worker(void *arg) {
    struct racing *r=arg;
    while(kosdec_adx_service_step(&r->f->service).status<KOSDEC_ADX_DONE)sched_yield();
    return NULL;
}
static void *consumer(void *arg) {
    struct racing *race=arg;int16_t pcm[38];size_t total=0;
    for(;;) {
        struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&race->f->service.pipe,pcm,19);
        for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<2;++k)
            CHECK(pcm[j*2+k]==golden[k][(total+j)%65]);
        total+=r.frames;if(r.drained)break;
        if(!r.frames)sched_yield();
    }
    race->frames=total;return NULL;
}
static void races(void) {
    size_t commands=0,frames=0;
    for(unsigned trial=0;trial<200;++trial) {
        struct fixture f;init(&f,2,true,false);
        if(trial%2)kosdec_adx_sequence_repeat(&f.sequence,true);
        struct racing r={.f=&f};pthread_t producer,reader;
        CHECK(!pthread_create(&producer,NULL,worker,&r));
        CHECK(!pthread_create(&reader,NULL,consumer,&r));
        for(unsigned n=0;;++n) {
            struct kosdec_adx_command c={.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=n%3==1};
            enum kosdec_adx_status posted=kosdec_adx_service_post(&f.service,&c),result;
            if(posted>=KOSDEC_ADX_DONE)break;
            CHECK(posted==KOSDEC_ADX_MORE);++commands;
            if(n==1000)kosdec_adx_service_request_cancel(&f.service);
            while(!kosdec_adx_service_take(&f.service,&result))sched_yield();
            CHECK(result==KOSDEC_ADX_MORE || result==KOSDEC_ADX_DONE || result==KOSDEC_ADX_CANCELLED);
            if(result>=KOSDEC_ADX_DONE)break;
            sched_yield();
        }
        CHECK(!pthread_join(producer,NULL) && !pthread_join(reader,NULL));
        if(!(trial%2))CHECK(r.frames==65 || kosdec_adx_service_get_state(&f.service).status==KOSDEC_ADX_CANCELLED);
        frames+=r.frames;free(f.in.data);
    }
    printf("SERVICE_RACES_PASS trials=200 commands=%zu frames=%zu\n",commands,frames);
}
int main(void) {
    combinations();lifecycle();loop_release();races();
    puts("SERVICE_PASS controls, PCM continuity, close/cancel and concurrent receipts");
    return 0;
}
