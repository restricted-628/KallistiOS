/* Exact suffix PCM and bounded replay, using independently generated oracles. */
#include <kosdec/adx_pipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile_vectors.h"
#include "rate_vectors.h"
#include "stream_fixture.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"SEEK_FAIL line=%u\n",(unsigned)__LINE__);abort();}}while(0)
struct input {uint8_t *data;size_t bytes,fragment;unsigned calls;bool wait,fail;};
static struct kosdec_adx_input_result input(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *p=ctx;++p->calls;CHECK(cap<=512 && off<p->bytes && cap<=p->bytes-off);
    if(p->fail)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(p->wait && p->calls%2==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment)cap=p->fragment;memcpy(dst,p->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void seek_to(struct kosdec_adx_stream *s,struct input *p,unsigned target) {
    unsigned calls=p->calls;CHECK(kosdec_adx_stream_seek_begin(s,target)==KOSDEC_ADX_MORE);
    CHECK(p->calls==calls && s->seeking && !s->returned_frames);
    int16_t out[64];struct kosdec_adx_stream saved=*s;
    CHECK(kosdec_adx_stream_read(s,out,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&saved,s,sizeof(saved)));
    for(unsigned n=0;n<10000;++n) {
        uint32_t before=s->source_frame;calls=p->calls;
        enum kosdec_adx_status r=kosdec_adx_stream_seek_step(s);
        CHECK(p->calls-calls<=1 && s->source_frame-before<=32 && !s->returned_frames);
        if(r==KOSDEC_ADX_DONE){CHECK(!s->seeking && s->source_frame==target);return;}
        CHECK(r<KOSDEC_ADX_DONE);
    }
    CHECK(false);
}
static void suffix(struct kosdec_adx_stream *s,struct input *p,unsigned target,unsigned ch,unsigned rate) {
    size_t at=target;
    for(unsigned n=0;n<10000;++n) {
        unsigned calls=p->calls;int16_t pcm[66];for(unsigned j=0;j<66;++j)pcm[j]=12345;
        struct kosdec_adx_stream_result r=kosdec_adx_stream_read(s,pcm,7);
        CHECK(p->calls-calls<=1 && r.frames<=7 && at+r.frames<=65);
        for(size_t j=0;j<r.frames;++j)for(unsigned c=0;c<ch;++c)
            CHECK(pcm[j*ch+c]==(rate?rate_vectors[5].pcm[c][at+j]:golden[c][at+j]));
        for(size_t j=r.frames*ch;j<66;++j)CHECK(pcm[j]==12345);
        at+=r.frames;CHECK(s->returned_frames==at-target);
        if(r.status>=KOSDEC_ADX_DONE){CHECK(r.status==KOSDEC_ADX_DONE && at==65);return;}
    }
    CHECK(false);
}
int main(void) {
    unsigned cases=0;const size_t fragments[]={1,17,512};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned rate=0;rate<2;++rate)
    for(unsigned f=0;f<3;++f)for(unsigned wait=0;wait<2;++wait) {
        struct input p={0};p.data=stream(1025,564,ch,32,61,&p.bytes);p.fragment=fragments[f];p.wait=wait;
        put32(p.data+8,rate?22050:44100);
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,input,&p,(uint32_t)p.bytes,false);
        /* Reuse a terminal reader, seek backwards and forwards at every offset. */
        for(unsigned target=0;target<=65;++target) {
            seek_to(&s,&p,target);suffix(&s,&p,target,ch,rate);++cases;
        }
        struct kosdec_adx_stream saved=s;
        CHECK(kosdec_adx_stream_seek_begin(&s,66)==KOSDEC_ADX_BAD_ARGUMENT);
        CHECK(!memcmp(&saved,&s,sizeof(s)));
        free(p.data);
    }
    struct input p={0};p.data=stream(50,18,2,32,61,&p.bytes);p.fragment=512;
    struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,input,&p,(uint32_t)p.bytes,true);
    struct kosdec_adx_stream saved=s;
    CHECK(kosdec_adx_stream_seek_begin(&s,20)==KOSDEC_ADX_UNSUPPORTED && !memcmp(&s,&saved,sizeof(s)));
    kosdec_adx_stream_release_loop(&s);seek_to(&s,&p,20);suffix(&s,&p,20,2,0);
    kosdec_adx_stream_init(&s,input,&p,(uint32_t)p.bytes,false);
    CHECK(kosdec_adx_stream_seek_begin(&s,66)==KOSDEC_ADX_MORE);
    enum kosdec_adx_status r;
    do {r=kosdec_adx_stream_seek_step(&s);}while(r<KOSDEC_ADX_DONE);
    CHECK(r==KOSDEC_ADX_BAD_ARGUMENT && !s.returned_frames);
    CHECK(kosdec_adx_stream_seek_begin(&s,32)==KOSDEC_ADX_MORE);
    kosdec_adx_stream_pause(&s,true);saved=s;
    CHECK(kosdec_adx_stream_seek_step(&s)==KOSDEC_ADX_MORE && !memcmp(&s,&saved,sizeof(s)));
    kosdec_adx_stream_cancel(&s);CHECK(kosdec_adx_stream_seek_step(&s)==KOSDEC_ADX_CANCELLED);
    seek_to(&s,&p,33);suffix(&s,&p,33,2,0);
    CHECK(kosdec_adx_stream_seek_begin(&s,32)==KOSDEC_ADX_MORE);p.fail=true;
    CHECK(kosdec_adx_stream_seek_step(&s)==KOSDEC_ADX_IO_ERROR);p.fail=false;
    p.data[p.bytes-1]=1;seek_to(&s,&p,65);
    CHECK(kosdec_adx_stream_read(&s,(int16_t[64]){0},32).status==KOSDEC_ADX_INVALID);
    p.data[p.bytes-1]=0;
    /* Producer automatically seeks; discarded samples never enter the PCM pipe. */
    for(unsigned target=0;target<=65;++target) {
        CHECK(kosdec_adx_stream_seek_begin(&s,target)==KOSDEC_ADX_MORE);
        int16_t storage[64],out[64];struct kosdec_adx_pipe pipe;
        CHECK(kosdec_adx_pipe_init(&pipe,&s,NULL,storage,64,32)==KOSDEC_ADX_MORE);
        size_t at=target;bool done=false;
        for(unsigned n=0;n<1000;++n) {
            CHECK(kosdec_adx_pipe_pump(&pipe).status<=KOSDEC_ADX_DONE);
            struct kosdec_adx_pipe_result got=kosdec_adx_pipe_read(&pipe,out,32);
            for(size_t j=0;j<got.frames;++j)for(unsigned c=0;c<2;++c)CHECK(out[j*2+c]==golden[c][at+j]);
            at+=got.frames;
            if(got.drained){CHECK(got.producer==KOSDEC_ADX_DONE && at==65);done=true;break;}
        }
        CHECK(done);
    }
    free(p.data);
    p.data=stream(50,18,2,32,61,&p.bytes);memset(p.data+20,0,24);put32(p.data+12,0);
    memmove(p.data+50,p.data+p.bytes-18,18);p.bytes=68;
    kosdec_adx_stream_init(&s,input,&p,(uint32_t)p.bytes,false);seek_to(&s,&p,0);
    CHECK(kosdec_adx_stream_read(&s,(int16_t[64]){0},32).status==KOSDEC_ADX_DONE);
    free(p.data);printf("SEEK_PASS suffix_cases=%u pipe_offsets=66 budgets/errors/cancel/footer\n",cases);return 0;
}
