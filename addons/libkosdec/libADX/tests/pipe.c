/* Original PCM buffering, ownership and concurrent publication regressions. */
#include <kosdec/adx_pipe.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#include "memory_vectors.h"
#include "rate_vectors.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"PIPE_FAIL line=%u\n",(unsigned)__LINE__);exit(1); } } while(0)
struct input { uint8_t *data;size_t bytes;unsigned calls,fragment;bool wait,fault; };
static unsigned cases;
static struct kosdec_adx_input_result provider(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *p=ctx;++p->calls;
    CHECK(off<p->bytes && cap<=p->bytes-off && cap<=512);
    if(p->fault)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(p->wait && p->calls%3==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment)cap=p->fragment;
    memcpy(dst,p->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static struct input fixture(unsigned ch,unsigned rate) {
    struct input p={.fragment=512};
    p.data=stream(50,18,ch,32,65,&p.bytes);
    memset(p.data+20,0,24);put32(p.data+8,rate);
    return p;
}
static void bind(struct kosdec_adx_pipe *p,struct kosdec_adx_stream *s,
                 struct kosdec_adx_sequence *q,struct input *in,
                 int16_t *storage,unsigned capacity,bool sequence) {
    if(sequence) {
        kosdec_adx_sequence_init(q,false);
        struct kosdec_adx_source src={provider,in,(uint32_t)in->bytes,77};
        for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_sequence_enqueue(q,&src)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_sequence_start(q)==KOSDEC_ADX_MORE);
    }
    else kosdec_adx_stream_init(s,provider,in,(uint32_t)in->bytes,false);
    CHECK(kosdec_adx_pipe_init(p,sequence?NULL:s,sequence?q:NULL,
                             storage,2*capacity,capacity)==KOSDEC_ADX_MORE);
}
static void combinations(void) {
    const unsigned capacities[]={32,64,256},reads[]={1,7,97},fragments[]={1,17,512};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned rate=0;rate<2;++rate)
    for(unsigned seq=0;seq<2;++seq)for(unsigned c=0;c<3;++c)
    for(unsigned rd=0;rd<3;++rd)for(unsigned f=0;f<3;++f)
    for(unsigned wait=0;wait<2;++wait)for(unsigned wrap=0;wrap<2;++wrap) {
        ++cases;struct input in=fixture(ch,rate?22050:44100);
        in.fragment=fragments[f];in.wait=wait;
        struct kosdec_adx_stream s;struct kosdec_adx_sequence q;struct kosdec_adx_pipe p;
        int16_t storage[514];storage[0]=storage[513]=12345;
        bind(&p,&s,&q,&in,storage+1,capacities[c],seq);
        /* Test-only initialization while quiescent, crossing uint32 rollover. */
        if(wrap)p.write_cursor=p.read_cursor=UINT32_MAX-13;
        uint32_t hz=99;unsigned channels=99;
        CHECK(!kosdec_adx_pipe_get_format(&p,&hz,&channels) && hz==99 && channels==99);
        size_t total=0,produced=0;bool drained=false;
        for(unsigned step=0;step<20000;++step) {
            unsigned before=in.calls;
            if(step%29==0) {
                if(seq)kosdec_adx_sequence_pause(&q,true);else kosdec_adx_stream_pause(&s,true);
                struct kosdec_adx_stream_result pause=kosdec_adx_pipe_pump(&p);
                CHECK(!pause.frames && in.calls==before);
                if(seq)kosdec_adx_sequence_pause(&q,false);else kosdec_adx_stream_pause(&s,false);
            }
            struct kosdec_adx_stream_result z=kosdec_adx_pipe_pump(&p);
            CHECK(z.frames<=32 && in.calls<=before+1);produced+=z.frames;
            if(z.status==KOSDEC_ADX_NEED_OUTPUT)CHECK(in.calls==before);
            if(step%5==0)continue; /* Delayed consumer exercises backpressure. */
            int16_t out[196];for(unsigned k=0;k<196;++k)out[k]=12345;
            struct kosdec_adx_pipe_result poll=kosdec_adx_pipe_read(&p,NULL,0);
            CHECK(!poll.frames && !poll.starved);
            struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&p,out,reads[rd]);
            CHECK(r.frames<=reads[rd] && r.queued_frames<=capacities[c]);
            CHECK(r.starved==(r.producer==KOSDEC_ADX_MORE && r.frames<reads[rd]));
            if(r.frames)CHECK(kosdec_adx_pipe_get_format(&p,&hz,&channels) && channels==ch && hz==(rate?22050u:44100u));
            for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<ch;++k)
                CHECK(out[j*ch+k]==(rate?rate_golden[k][(total+j)%65]:golden[k][(total+j)%65]));
            for(size_t j=r.frames*ch;j<196;++j)CHECK(out[j]==12345);
            total+=r.frames;
            if(r.drained){CHECK(r.producer==KOSDEC_ADX_DONE);drained=true;break;}
        }
        CHECK(drained && total==65*(seq?3u:1u) && produced==total);
        CHECK(storage[0]==12345 && storage[513]==12345);
        unsigned before=in.calls;kosdec_adx_pipe_request_cancel(&p);
        CHECK(kosdec_adx_pipe_pump(&p).status==KOSDEC_ADX_DONE && in.calls==before);
        free(in.data);
    }
}
static void loops(void) {
    for(unsigned v=0;v<6;++v)for(unsigned ch=1;ch<=2;++ch)for(unsigned rate=0;rate<2;++rate) {
        ++cases;struct input in={.fragment=7,.wait=true};
        in.data=stream(50,18,ch,vectors[v].start,vectors[v].end,&in.bytes);
        put32(in.data+8,rate?22050:44100);
        struct kosdec_adx_stream s;struct kosdec_adx_pipe p;int16_t storage[128],out[14];
        kosdec_adx_stream_init(&s,provider,&in,(uint32_t)in.bytes,true);
        CHECK(kosdec_adx_pipe_init(&p,&s,NULL,storage,128,64)==KOSDEC_ADX_MORE);
        size_t total=0;bool drained=false;
        for(unsigned step=0;step<20000;++step) {
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            kosdec_adx_pipe_pump(&p);
            struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&p,out,7);
            for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<ch;++k) {
                CHECK(total+j<131);
                CHECK(out[j*ch+k]==(rate?rate_vectors[v].pcm[k][total+j]:vectors[v].pcm[k][total+j]));
            }
            total+=r.frames;
            if(r.drained){CHECK(r.producer==KOSDEC_ADX_DONE);drained=true;break;}
        }
        CHECK(drained && total==65+2*(vectors[v].end-vectors[v].start));free(in.data);
    }
}
static void lifecycle(void) {
    struct input in=fixture(2,44100);struct kosdec_adx_stream s;
    struct kosdec_adx_sequence q;struct kosdec_adx_pipe p,copy;int16_t storage[128],out[128];
    bind(&p,&s,&q,&in,storage,32,false);copy=p;
    CHECK(kosdec_adx_pipe_init(&p,&s,&q,storage,128,32)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_init(&p,NULL,NULL,storage,128,32)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_init(&p,&s,NULL,storage,63,32)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_init(&p,&s,NULL,storage,128,33)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_init(&p,&s,NULL,storage,128,16)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_init(&p,&s,NULL,storage,SIZE_MAX,SIZE_MAX)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&p,&copy,sizeof(p)));
    CHECK(kosdec_adx_pipe_read(&p,NULL,1).producer==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_read(&p,(int16_t *)((char *)out+1),1).producer==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&p,&copy,sizeof(p)));
    while(!kosdec_adx_pipe_pump(&p).frames)CHECK(in.calls<1000);
    unsigned before=in.calls;
    CHECK(kosdec_adx_pipe_pump(&p).status==KOSDEC_ADX_NEED_OUTPUT && in.calls==before);
    kosdec_adx_pipe_request_cancel(&p);
    CHECK(kosdec_adx_pipe_pump(&p).status==KOSDEC_ADX_CANCELLED && in.calls==before);
    struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&p,NULL,0);
    CHECK(r.producer==KOSDEC_ADX_CANCELLED && !r.drained && r.queued_frames==32);
    r=kosdec_adx_pipe_read(&p,out,64);
    CHECK(r.drained && r.frames==32 && !r.starved);
    for(unsigned j=0;j<32;++j)for(unsigned k=0;k<2;++k)CHECK(out[j*2+k]==golden[k][j]);
    bind(&p,&s,&q,&in,storage,64,false);in.fragment=18;
    while(!kosdec_adx_pipe_pump(&p).frames)CHECK(in.calls<1000);
    in.fault=true;size_t total=0;bool drained=false;
    for(unsigned i=0;i<100;++i) {
        kosdec_adx_pipe_pump(&p);r=kosdec_adx_pipe_read(&p,out,7);
        total+=r.frames;if(r.drained){CHECK(r.producer==KOSDEC_ADX_IO_ERROR);drained=true;break;}
    }
    CHECK(drained && total>=32 && total<=65);
    in.fault=false;bind(&p,&s,&q,&in,storage,64,true);q.started=false;
    CHECK(kosdec_adx_pipe_pump(&p).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_pipe_read(&p,NULL,0).drained);
    bind(&p,&s,&q,&in,storage,64,false);before=in.calls;
    kosdec_adx_pipe_request_cancel(&p);
    CHECK(kosdec_adx_pipe_pump(&p).status==KOSDEC_ADX_CANCELLED && in.calls==before);
    CHECK(kosdec_adx_pipe_read(&p,NULL,0).drained);
    free(in.data);
}
struct concurrent { struct kosdec_adx_pipe pipe;struct kosdec_adx_sequence sequence;bool cancel; };
static void *producer(void *ctx) {
    struct concurrent *c=ctx;
    for(;;) {
        if(!c->cancel && c->sequence.completed_entries==9997)kosdec_adx_sequence_repeat(&c->sequence,false);
        struct kosdec_adx_stream_result r=kosdec_adx_pipe_pump(&c->pipe);
        if(r.status>=KOSDEC_ADX_DONE){CHECK(r.status==(c->cancel?KOSDEC_ADX_CANCELLED:KOSDEC_ADX_DONE));return NULL;}
        if(!r.frames)sched_yield();
    }
}
static void concurrency(bool cancel) {
    struct concurrent c={.cancel=cancel};struct kosdec_adx_stream unused;int16_t storage[128],out[38];
    struct input in=fixture(2,44100);in.fragment=7;in.wait=true;
    bind(&c.pipe,&unused,&c.sequence,&in,storage,64,true);
    kosdec_adx_sequence_repeat(&c.sequence,true);
    c.pipe.write_cursor=c.pipe.read_cursor=UINT32_MAX-21;
    pthread_t thread;CHECK(!pthread_create(&thread,NULL,producer,&c));size_t total=0;
    for(;;) {
        struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&c.pipe,out,19);
        for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<2;++k)
            CHECK(out[j*2+k]==golden[k][(total+j)%65]);
        total+=r.frames;
        if(cancel && total>=5000)kosdec_adx_pipe_request_cancel(&c.pipe);
        if(r.drained){CHECK(r.producer==(cancel?KOSDEC_ADX_CANCELLED:KOSDEC_ADX_DONE));break;}
        if(!r.frames)sched_yield();
    }
    CHECK(!pthread_join(thread,NULL));
    if(cancel)CHECK(total>=5000 && total<=5115);
    else CHECK(total==650000 && c.sequence.completed_entries==10000);
    free(in.data);
}
int main(void) {
    combinations();loops();lifecycle();concurrency(false);concurrency(true);
    printf("PIPE_PASS cases=%u concurrent_frames=650000 cancel_race=PASS\n",cases);return 0;
}
