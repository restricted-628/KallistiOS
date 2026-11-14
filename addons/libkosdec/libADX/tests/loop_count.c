#include <kosdec/adx_pipe.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED); if(!(x)) { \
    fprintf(stderr,"loop_count:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
struct fixture {
    struct kosdec_adx_stream stream;
    struct kosdec_adx_pipe pipe;
    uint8_t *data;size_t bytes;
    bool wait;
    int16_t pcm[64];
};
static struct kosdec_adx_input_result input(void *ctx,uint32_t off,uint8_t *dst,size_t n) {
    struct fixture *f=ctx;
    if(f->wait && off==86)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(off<f->bytes && n<=f->bytes-off);
    memcpy(dst,f->data+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void init(struct fixture *f) {
    memset(f,0,sizeof(*f));f->data=stream(50,18,2,32,63,&f->bytes);
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,true);
    CHECK(kosdec_adx_pipe_init(&f->pipe,&f->stream,NULL,f->pcm,64,32)==KOSDEC_ADX_MORE);
}
static uint32_t count(struct fixture *f) {
    uint32_t n;CHECK(kosdec_adx_pipe_get_loop_count(&f->pipe,&n));return n;
}
static void wait_release(void) {
    struct fixture f;init(&f);CHECK(count(&f)==0);
    uint32_t out=123;
    CHECK(!kosdec_adx_pipe_get_loop_count(NULL,&out) && out==123);
    CHECK(!kosdec_adx_pipe_get_loop_count(&f.pipe,NULL));
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_MORE); /* Header. */
    CHECK(kosdec_adx_pipe_pump(&f.pipe).frames==32);
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_NEED_OUTPUT && count(&f)==0);
    int16_t pcm[64];CHECK(kosdec_adx_pipe_read(&f.pipe,pcm,32).frames==32);
    CHECK(kosdec_adx_pipe_pump(&f.pipe).frames==31);
    f.wait=true;
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_NEED_INPUT && count(&f)==1);
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_NEED_INPUT && count(&f)==1);
    kosdec_adx_stream_release_loop(&f.stream);f.wait=false;
    struct kosdec_adx_pipe_result result={0};
    for(unsigned i=0;i<30 && !result.drained;++i) {
        CHECK(kosdec_adx_pipe_pump(&f.pipe).status<=KOSDEC_ADX_DONE);
        result=kosdec_adx_pipe_read(&f.pipe,pcm,32);CHECK(count(&f)==1);
    }
    CHECK(result.drained && result.producer==KOSDEC_ADX_DONE);
    kosdec_adx_pipe_request_cancel(&f.pipe);
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_DONE && count(&f)==1);
    free(f.data);
}
static void *produce(void *context) {
    struct fixture *f=context;
    for(unsigned i=0;i<1000000;++i) {
        if(f->stream.wraps==3)kosdec_adx_stream_release_loop(&f->stream);
        struct kosdec_adx_stream_result r=kosdec_adx_pipe_pump(&f->pipe);
        CHECK(r.status<=KOSDEC_ADX_DONE);
        if(r.status==KOSDEC_ADX_DONE)return NULL;
        sched_yield();
    }
    CHECK(0);return NULL;
}
static void concurrent(void) {
    struct fixture f;init(&f);pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,produce,&f));
    uint32_t previous=0;struct kosdec_adx_pipe_result result={0};
    int16_t pcm[62];
    for(unsigned i=0;i<1000000 && !result.drained;++i) {
        uint32_t n=count(&f);CHECK(n>=previous && n<=3);previous=n;
        result=kosdec_adx_pipe_read(&f.pipe,pcm,31);sched_yield();
    }
    CHECK(!pthread_join(thread,NULL));
    CHECK(result.drained && result.producer==KOSDEC_ADX_DONE && count(&f)==3);
    free(f.data);
}
static void overflow_cancel(void) {
    struct fixture f;init(&f);int16_t pcm[64];
    for(unsigned i=0;i<3;++i) {kosdec_adx_pipe_pump(&f.pipe);kosdec_adx_pipe_read(&f.pipe,pcm,32);}
    CHECK(f.stream.source_frame==63);
    /* Seed the producer-owned test counter to reach the public wrap boundary. */
    f.stream.wraps=UINT32_MAX;
    kosdec_adx_pipe_pump(&f.pipe);CHECK(count(&f)==0 && f.stream.wraps==UINT64_C(4294967296));
    kosdec_adx_pipe_request_cancel(&f.pipe);
    CHECK(kosdec_adx_pipe_pump(&f.pipe).status==KOSDEC_ADX_CANCELLED && count(&f)==0);
    free(f.data);
}
int main(void) {
    wait_release();concurrent();overflow_cancel();
    printf("loop_count: %u checks passed (includes concurrent snapshots)\n",checks);
    return 0;
}
