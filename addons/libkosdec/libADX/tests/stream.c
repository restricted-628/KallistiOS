#include <kosdec/adx_stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_vectors.h"
#include "profile_vectors.h"
#include "stream_fixture.h"
static unsigned failures,cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("STREAM_FAIL line=%u\n",(unsigned)__LINE__); } } while(0)
struct provider {
    const uint8_t *data;size_t bytes,fragment;
    unsigned calls,wait_every,rewinds,last_offset;
    int fault;
};
static struct kosdec_adx_input_result read_at(void *context,uint32_t offset,uint8_t *dst,size_t capacity) {
    struct provider *p=context;++p->calls;
    CHECK(capacity && capacity<=512 && offset<p->bytes);
    if(offset<p->last_offset)++p->rewinds;
    p->last_offset=offset;
    memset(dst,0xcc,capacity); /* No old window data may survive a refill. */
    if(p->fault) {
        const struct kosdec_adx_input_result faults[]={
            {KOSDEC_ADX_INPUT_DATA,0},{KOSDEC_ADX_INPUT_DATA,513},
            {KOSDEC_ADX_INPUT_WAIT,1},{KOSDEC_ADX_INPUT_END,1},
            {KOSDEC_ADX_INPUT_ERROR,0},{(enum kosdec_adx_input_status)99,0},
            {KOSDEC_ADX_INPUT_END,0}};
        return faults[p->fault-1];
    }
    if(p->wait_every && p->calls%p->wait_every==0)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=p->bytes-offset;if(n>capacity)n=capacity;if(n>p->fragment)n=p->fragment;
    memcpy(dst,p->data+offset,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static enum kosdec_adx_status drain(struct kosdec_adx_stream *s) {
    for(unsigned i=0;i<10000;++i) {
        int16_t out[64];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(s,out,32);
        if(z.status>=KOSDEC_ADX_DONE)return z.status;
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
int main(void) {
    const unsigned fragments[]={1,17,37,512},caps[]={1,7,32},headers[]={50,2048};
    for(unsigned v=0;v<6;++v)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned f=0;f<4;++f)for(unsigned c=0;c<3;++c)
    for(unsigned h=0;h<2;++h)for(unsigned wait=0;wait<2;++wait) {
        ++cases;size_t n;uint8_t *data=stream(headers[h],564,ch,vectors[v].start,vectors[v].end,&n);
        struct provider p={data,n,fragments[f],0,wait?2u:0u,0,0,0};
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,true);
        size_t at=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned call=0;call<20000;++call) {
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            unsigned before=p.calls;
            kosdec_adx_stream_pause(&s,true);struct kosdec_adx_stream saved=s;
            struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,NULL,0);
            CHECK(z.status==KOSDEC_ADX_MORE && !memcmp(&s,&saved,sizeof(s)) && before==p.calls);
            kosdec_adx_stream_pause(&s,false);saved=s;
            z=kosdec_adx_stream_read(&s,NULL,0);
            CHECK(z.status==KOSDEC_ADX_NEED_OUTPUT && !memcmp(&s,&saved,sizeof(s)) && before==p.calls);
            int16_t pcm[66];for(unsigned i=0;i<66;++i)pcm[i]=0x5a5a;
            uint64_t wraps_before=s.wraps;
            z=kosdec_adx_stream_read(&s,pcm,caps[c]);
            if(s.wraps!=wraps_before)CHECK(p.calls==before+1 && p.last_offset==s.decoder.loop.start_byte);
            CHECK(p.calls-before<=1 && z.frames<=caps[c] && z.frames<=32);
            for(size_t i=0;i<z.frames;++i)for(unsigned channel=0;channel<ch;++channel) {
                CHECK(at+i<131);if(at+i<131)CHECK(pcm[i*ch+channel]==vectors[v].pcm[channel][at+i]);
            }
            for(size_t i=z.frames*ch;i<66;++i)CHECK(pcm[i]==0x5a5a);
            at+=z.frames;CHECK(s.returned_frames==at);
            if(z.status>=KOSDEC_ADX_DONE){status=z.status;break;}
        }
        CHECK(status==KOSDEC_ADX_DONE && s.wraps==2 && s.source_frame==65);
        CHECK(at==65+2*(vectors[v].end-vectors[v].start));
        unsigned before=p.calls;kosdec_adx_stream_cancel(&s);kosdec_adx_stream_pause(&s,true);
        CHECK(drain(&s)==KOSDEC_ADX_DONE && before==p.calls);free(data);
    }
    size_t n;uint8_t *data=stream(50,18,2,32,61,&n);
    struct provider p={data,n,1,0,0,0,0,0};struct kosdec_adx_stream s;
    for(unsigned release=0;release<=61;++release) {
        kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,true);size_t at=0;
        for(unsigned i=0;i<1000;++i) {
            if(at==release)kosdec_adx_stream_release_loop(&s);
            int16_t pcm[2];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,1);
            if(z.frames){CHECK(at<65);if(at<65)CHECK(pcm[0]==golden[0][at] && pcm[1]==golden[1][at]);++at;}
            if(z.status>=KOSDEC_ADX_DONE){CHECK(z.status==KOSDEC_ADX_DONE);break;}
        }
        CHECK(at==65 && !s.wraps);
    }
    for(size_t bytes=0;bytes<n;++bytes) {
        kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)bytes,false);
        CHECK(drain(&s)==KOSDEC_ADX_TRUNCATED);
    }
    for(int fault=1;fault<=7;++fault) {
        p.fault=fault;kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,false);
        CHECK(drain(&s)==(fault==7?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_IO_ERROR));
        unsigned calls=p.calls;CHECK(drain(&s)==s.terminal && p.calls==calls);
    }
    p.fault=0;kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,false);
    int16_t pcm[64];while(!s.decoder.block_used)CHECK(kosdec_adx_stream_read(&s,pcm,32).status<KOSDEC_ADX_DONE);
    struct kosdec_adx_stream saved=s;unsigned calls=p.calls;
    CHECK(kosdec_adx_stream_read(&s,NULL,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_stream_read(&s,(int16_t *)((uint8_t *)pcm+1),1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&saved,&s,sizeof(s)) && calls==p.calls);
    kosdec_adx_stream_cancel(&s);CHECK(drain(&s)==KOSDEC_ADX_CANCELLED && calls==p.calls);
    /* Errors after partial group input must not publish PCM/history. */
    kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,false);
    while(!s.decoder.block_used)CHECK(kosdec_adx_stream_read(&s,pcm,32).status<KOSDEC_ADX_DONE);
    p.fault=5;CHECK(drain(&s)==KOSDEC_ADX_IO_ERROR && !s.returned_frames && !s.decoder.history[0][0]);p.fault=0;
    data[n-1]=1;kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n,false);CHECK(drain(&s)==KOSDEC_ADX_INVALID);data[n-1]=0;
    p.bytes=n+1;kosdec_adx_stream_init(&s,read_at,&p,(uint32_t)n+1,false);CHECK(drain(&s)==KOSDEC_ADX_INVALID);p.bytes=n;
    kosdec_adx_stream_init(&s,NULL,NULL,0,false);CHECK(drain(&s)==KOSDEC_ADX_BAD_ARGUMENT);
    free(data);printf("ADX_STREAM cases=%u failures=%u\n",cases,failures);return failures?1:0;
}
