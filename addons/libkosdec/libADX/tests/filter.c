/* Original PCM hook tests: output changes never feed predictor history. */
#include <kosdec/adx_memory.h>
#include <kosdec/adx_pipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#include "memory_vectors.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FILTER_FAIL line=%u\n",(unsigned)__LINE__); abort(); } } while(0)
static unsigned cases;
struct context { unsigned channels,calls,vector;size_t frames[2],origin;bool loop,active; };
static int16_t expected(struct context *c,unsigned ch,size_t pos) {
    return c->loop?vectors[c->vector].pcm[ch][pos]:golden[ch][(pos+c->origin)%65];
}
static int16_t changed(int16_t sample,unsigned ch) {return (int16_t)(sample/2+(int)ch*37);}
static void filter(void *ctx,unsigned ch,int16_t *pcm,size_t frames,unsigned stride) {
    struct context *c=ctx;
    CHECK(c->active && ch==c->calls%c->channels && stride==c->channels && frames && frames<=32);
    for(size_t i=0;i<frames;++i) {
        CHECK(pcm[i*stride]==expected(c,ch,c->frames[ch]+i));
        pcm[i*stride]=changed(pcm[i*stride],ch);
    }
    c->frames[ch]+=frames;++c->calls;
}
static struct context context(unsigned channels,bool loop) {
    struct context c={.channels=channels,.loop=loop};
    while(vectors[c.vector].start!=32 || vectors[c.vector].end!=61)++c.vector;
    return c;
}
struct input { uint8_t *p;size_t bytes,fragment;unsigned calls; };
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;
    if(++in->calls%3==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(off<in->bytes);size_t n=in->bytes-off;
    if(n>cap)n=cap;if(n>in->fragment)n=in->fragment;memcpy(dst,in->p+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void output(struct context *c,const int16_t *pcm,size_t at,size_t frames) {
    for(size_t i=0;i<frames;++i)for(unsigned ch=0;ch<c->channels;++ch)
        CHECK(pcm[i*c->channels+ch]==changed(expected(c,ch,at+i),ch));
}
static void codec(void) {
    for(unsigned ch=1;ch<=2;++ch)for(unsigned fragment=1;fragment<=513;fragment+=16) {
        size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);
        struct kosdec_adx_decoder d,plain;kosdec_adx_init(&d);kosdec_adx_init(&plain);
        struct context c=context(ch,false);CHECK(kosdec_adx_set_filter(&d,filter,&c)==KOSDEC_ADX_DONE);
        size_t used=0,at=0;bool done=false;
        for(unsigned calls=0;calls<1000;++calls) {
            size_t take=n-used;if(take>fragment)take=fragment;int16_t pcm[64],original[64];
            unsigned before=c.calls;
            if(d.header_ready && d.frames_done<d.total_frames) {
                CHECK(kosdec_adx_decode(&d,p+used,take,NULL,0,false).status==KOSDEC_ADX_NEED_OUTPUT);
                CHECK(c.calls==before);
            }
            c.active=true;
            struct kosdec_adx_result a=kosdec_adx_decode(&d,p+used,take,pcm,32,used+take==n);
            c.active=false;
            struct kosdec_adx_result b=kosdec_adx_decode(&plain,p+used,take,original,32,used+take==n);
            CHECK(a.status==b.status && a.frames==b.frames && a.consumed==b.consumed);
            CHECK(!memcmp(d.history,plain.history,sizeof(d.history)) && d.clips==plain.clips);
            CHECK(c.calls-before==(a.frames?ch:0));output(&c,pcm,at,a.frames);
            used+=a.consumed;at+=a.frames;
            if(a.status==KOSDEC_ADX_DONE){done=true;break;}
            CHECK(a.status<KOSDEC_ADX_DONE);
        }
        CHECK(done && used==n && at==65 && c.frames[0]==65);
        CHECK(kosdec_adx_set_filter(&d,NULL,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
        free(p);++cases;
    }
}
static void readers(void) {
    const unsigned caps[]={1,7,32};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned repeat=0;repeat<2;++repeat)
    for(unsigned cap=0;cap<3;++cap)for(unsigned fragment=1;fragment<=37;fragment+=18) {
        size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);
        struct kosdec_adx_memory m;kosdec_adx_memory_init(&m,p,n,repeat);
        struct input in={p,n,fragment,0};struct kosdec_adx_stream s;
        kosdec_adx_stream_init(&s,read_at,&in,(uint32_t)n,repeat);
        struct context cm=context(ch,repeat),cs=context(ch,repeat);
        CHECK(kosdec_adx_set_filter(&m.decoder,filter,&cm)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_set_filter(&s.decoder,filter,&cs)==KOSDEC_ADX_DONE);
        size_t mi=0,si=0;bool md=false,sd=false;
        for(unsigned call=0;call<2000 && (!md || !sd);++call) {
            if(m.wraps==2)kosdec_adx_memory_release_loop(&m);
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            int16_t pcm[64];
            if(!md) {
                kosdec_adx_memory_pause(&m,true);unsigned before=cm.calls;
                CHECK(!kosdec_adx_memory_read(&m,pcm,32).frames && before==cm.calls);
                kosdec_adx_memory_pause(&m,false);cm.active=true;
                struct kosdec_adx_memory_result z=kosdec_adx_memory_read(&m,pcm,caps[cap]);cm.active=false;
                CHECK(z.status<=KOSDEC_ADX_DONE);output(&cm,pcm,mi,z.frames);mi+=z.frames;md=z.status==KOSDEC_ADX_DONE;
            }
            if(!sd) {
                kosdec_adx_stream_pause(&s,true);unsigned before=cs.calls;
                CHECK(!kosdec_adx_stream_read(&s,pcm,32).frames && before==cs.calls);
                kosdec_adx_stream_pause(&s,false);cs.active=true;
                struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,caps[cap]);cs.active=false;
                CHECK(z.status<=KOSDEC_ADX_DONE);output(&cs,pcm,si,z.frames);si+=z.frames;sd=z.status==KOSDEC_ADX_DONE;
            }
        }
        CHECK(md && sd && mi==si && mi==(repeat?123u:65u));
        CHECK(cm.frames[0]==mi && cs.frames[0]==si && !memcmp(m.decoder.history,s.decoder.history,sizeof(m.decoder.history)));
        if(!repeat) {
            cs=context(ch,false);cs.origin=17;
            CHECK(kosdec_adx_stream_seek_begin(&s,17)==KOSDEC_ADX_MORE);
            unsigned calls=0;
            while(s.seeking && calls++<1000)CHECK(kosdec_adx_stream_seek_step(&s)<=KOSDEC_ADX_DONE);
            CHECK(!s.seeking && !cs.calls && s.decoder.filter_context==&cs);
            si=0;sd=false;
            for(unsigned call=0;call<1000;++call) {
                int16_t pcm[64];cs.active=true;
                struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,7);cs.active=false;
                CHECK(z.status<=KOSDEC_ADX_DONE);output(&cs,pcm,si,z.frames);si+=z.frames;
                if(z.status==KOSDEC_ADX_DONE){sd=true;break;}
            }
            CHECK(sd && si==48 && cs.frames[0]==48);
        }
        free(p);++cases;
    }
}
static void pipes(void) {
    for(unsigned ch=1;ch<=2;++ch)for(unsigned sequence=0;sequence<2;++sequence) {
        size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);memset(p+20,0,24);
        struct input in={p,n,17,0};struct context c=context(ch,false);
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,read_at,&in,(uint32_t)n,false);
        struct kosdec_adx_sequence q;kosdec_adx_sequence_init(&q,false);
        if(sequence) {
            CHECK(kosdec_adx_sequence_set_filter(&q,filter,&c)==KOSDEC_ADX_DONE);
            struct kosdec_adx_source src={read_at,&in,(uint32_t)n,1};
            for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_sequence_enqueue(&q,&src)==KOSDEC_ADX_MORE);
            CHECK(kosdec_adx_sequence_start(&q)==KOSDEC_ADX_MORE);
        } else CHECK(kosdec_adx_set_filter(&s.decoder,filter,&c)==KOSDEC_ADX_DONE);
        int16_t ring[64];struct kosdec_adx_pipe pipe;
        CHECK(kosdec_adx_pipe_init(&pipe,sequence?NULL:&s,sequence?&q:NULL,ring,64,32)==KOSDEC_ADX_MORE);
        size_t at=0;bool done=false;
        for(unsigned calls=0;calls<4000;++calls) {
            c.active=true;CHECK(kosdec_adx_pipe_pump(&pipe).status<=KOSDEC_ADX_DONE);c.active=false;
            unsigned before=c.calls;int16_t out[14];struct kosdec_adx_pipe_result z=kosdec_adx_pipe_read(&pipe,out,7);
            CHECK(before==c.calls);output(&c,out,at,z.frames);at+=z.frames;
            if(z.drained){CHECK(z.producer==KOSDEC_ADX_DONE);done=true;break;}
        }
        CHECK(done && at==65*(sequence?2u:1u) && c.frames[0]==at);free(p);++cases;
    }
}
static void controls(void) {
    struct kosdec_adx_decoder d={0};struct kosdec_adx_sequence q={0};
    CHECK(kosdec_adx_set_filter(NULL,filter,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_set_filter(&d,filter,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_sequence_set_filter(NULL,filter,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_sequence_set_filter(&q,filter,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    kosdec_adx_init(&d);kosdec_adx_sequence_init(&q,false);
    CHECK(kosdec_adx_set_filter(&d,filter,&d)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_sequence_set_filter(&q,filter,&q)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_set_filter(&d,NULL,&d)==KOSDEC_ADX_DONE && !d.filter && !d.filter_context);
    CHECK(kosdec_adx_sequence_set_filter(&q,NULL,&q)==KOSDEC_ADX_DONE && !q.filter && !q.filter_context);
    kosdec_adx_cancel(&d);kosdec_adx_sequence_cancel(&q);
    CHECK(kosdec_adx_set_filter(&d,filter,&d)==KOSDEC_ADX_CANCELLED && !d.filter);
    CHECK(kosdec_adx_sequence_set_filter(&q,filter,&q)==KOSDEC_ADX_CANCELLED && !q.filter);
    kosdec_adx_init(&d);kosdec_adx_sequence_init(&q,false);
    CHECK(!d.filter && !q.filter);
    size_t n;uint8_t *p=stream(50,18,2,32,61,&n);struct context c=context(2,false);
    CHECK(kosdec_adx_set_filter(&d,filter,&c)==KOSDEC_ADX_DONE);
    int16_t pcm[64];struct kosdec_adx_result r=kosdec_adx_decode(&d,p,50,pcm,32,false);
    CHECK(r.status==KOSDEC_ADX_MORE && !c.calls);p[50]=0x80;
    r=kosdec_adx_decode(&d,p+50,n-50,pcm,32,true);
    CHECK(r.status==KOSDEC_ADX_INVALID && !r.frames && !c.calls);free(p);
}
int main(void) {codec();readers();pipes();controls();printf("ADX_FILTER cases=%u failures=0\n",cases);return 0;}
