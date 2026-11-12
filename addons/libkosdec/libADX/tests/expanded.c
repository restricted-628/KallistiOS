/* Original multi-rate codec/loop/seek and extreme-history numerical checks. */
#include <kosdec/adx.h>
#include <kosdec/adx_memory.h>
#include <kosdec/adx_stream.h>
#include "adx_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expanded_vectors.h"
#include "stream_fixture.h"
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define CHECK(x) do {if(!(x)){printf("EXPANDED_FAIL line=%u\n",(unsigned)__LINE__);return 1;}}while(0)
struct input {const uint8_t *data;size_t bytes,fragment;unsigned calls,wait;};
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *p=ctx;++p->calls;
    if(off>=p->bytes || cap>512 || cap>p->bytes-off) abort();
    if(p->wait && p->calls%2==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment)cap=p->fragment;
    memcpy(dst,p->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
int main(void) {
    unsigned cases=0;
    const size_t fragments[]={1,17,512},caps[]={1,7,32};
    const unsigned targets[]={0,1,31,32,33,65};
    for(unsigned v=0;v<COUNT(expanded);++v)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned f=0;f<COUNT(fragments);++f) {
        size_t bytes;uint8_t *data=stream(50,564,ch,32,65,&bytes);
        put32(data+8,expanded[v].rate);
        data[16]=(uint8_t)(expanded[v].cutoff>>8);data[17]=(uint8_t)expanded[v].cutoff;
        for(unsigned version=3;version<=5;version+=2) {
            ++cases;data[18]=(uint8_t)version;memset(data+20,0,24);
            struct kosdec_adx_decoder d;kosdec_adx_init(&d);
            size_t at=0,frames=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
            for(unsigned call=0;call<10000 && status<KOSDEC_ADX_DONE;++call) {
                size_t n=bytes-at;if(n>fragments[f])n=fragments[f];
                int16_t pcm[66];for(unsigned i=0;i<66;++i)pcm[i]=12345;
                struct kosdec_adx_result r=kosdec_adx_decode(&d,data+at,n,pcm,32,at+n==bytes);
                CHECK(r.consumed<=n && r.frames<=32 && frames+r.frames<=65);
                for(size_t j=0;j<r.frames;++j)for(unsigned c=0;c<ch;++c)
                    CHECK(pcm[j*ch+c]==expanded[v].pcm[c][frames+j]);
                for(size_t j=r.frames*ch;j<66;++j)CHECK(pcm[j]==12345);
                at+=r.consumed;frames+=r.frames;status=r.status;
            }
            CHECK(status==KOSDEC_ADX_DONE && frames==65 && at==bytes && d.sample_rate==expanded[v].rate);
        }
        data[18]=3;put32(data+20,1);put32(data+24,1);put32(data+28,32);
        put32(data+32,50+18*ch);put32(data+36,65);put32(data+40,50+54*ch);
        for(unsigned wait=0;wait<2;++wait) {
            for(unsigned cap=0;cap<COUNT(caps);++cap) {
                ++cases;struct input in={data,bytes,fragments[f],0,wait};
                struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,read_at,&in,(uint32_t)bytes,true);
                struct kosdec_adx_memory m;kosdec_adx_memory_init(&m,data,bytes,true);
                for(unsigned resident=0;resident<2;++resident) {
                    size_t frames=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
                    for(unsigned call=0;call<20000 && status<KOSDEC_ADX_DONE;++call) {
                        int16_t pcm[64];size_t n;unsigned before=in.calls;
                        if(resident) {
                            if(m.wraps==2)kosdec_adx_memory_release_loop(&m);
                            struct kosdec_adx_memory_result r=kosdec_adx_memory_read(&m,pcm,caps[cap]);
                            n=r.frames;status=r.status;
                        } else {
                            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
                            struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&s,pcm,caps[cap]);
                            n=r.frames;status=r.status;
                        }
                        CHECK(in.calls-before<=1 && n<=caps[cap] && frames+n<=131);
                        for(size_t j=0;j<n;++j)for(unsigned c=0;c<ch;++c)
                            CHECK(pcm[j*ch+c]==expanded[v].loop[c][frames+j]);
                        frames+=n;
                    }
                    CHECK(status==KOSDEC_ADX_DONE && frames==131);
                }
                CHECK(s.wraps==2 && m.wraps==2 && !memcmp(s.decoder.history,m.decoder.history,sizeof(m.decoder.history)));
            }
            for(unsigned target=0;target<COUNT(targets);++target) {
                ++cases;struct input in={data,bytes,fragments[f],0,wait};
                struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,read_at,&in,(uint32_t)bytes,false);
                CHECK(kosdec_adx_stream_seek_begin(&s,targets[target])==KOSDEC_ADX_MORE);
                enum kosdec_adx_status status=KOSDEC_ADX_MORE;
                for(unsigned call=0;call<10000 && status<KOSDEC_ADX_DONE;++call) {
                    unsigned before=in.calls;uint32_t frame=s.source_frame;
                    status=kosdec_adx_stream_seek_step(&s);
                    CHECK(in.calls-before<=1 && s.source_frame-frame<=32);
                }
                CHECK(status==KOSDEC_ADX_DONE && !s.seeking && !s.returned_frames);
                size_t frame=targets[target];status=KOSDEC_ADX_MORE;
                for(unsigned call=0;call<10000 && status<KOSDEC_ADX_DONE;++call) {
                    int16_t pcm[14];struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&s,pcm,7);
                    CHECK(r.frames<=7 && frame+r.frames<=65);
                    for(size_t j=0;j<r.frames;++j)for(unsigned c=0;c<ch;++c)
                        CHECK(pcm[j*ch+c]==expanded[v].pcm[c][frame+j]);
                    frame+=r.frames;status=r.status;
                }
                CHECK(status==KOSDEC_ADX_DONE && frame==65);
            }
        }
        free(data);
    }
    for(unsigned v=0;v<COUNT(stress_vectors);++v)for(unsigned stride=1;stride<=2;++stride) {
        ++cases;int16_t q[2],history[2],pcm[66];
        CHECK(!media_adx_coefficients(stress_vectors[v].rate,stress_vectors[v].cutoff,q));
        memcpy(history,stress_vectors[v].initial,sizeof(history));
        for(unsigned i=0;i<66;++i)pcm[i]=12345;
        int clips=media_adx_channel_group(stress_vectors[v].block,stress_vectors[v].frames,q,history,pcm,stride);
        CHECK(clips==(int)stress_vectors[v].clips && !memcmp(history,stress_vectors[v].final,sizeof(history)));
        for(unsigned i=0;i<66;++i)
            CHECK(pcm[i]==(i%stride==0 && i/stride<stress_vectors[v].frames?stress_vectors[v].pcm[i/stride]:12345));
    }
    printf("ADX_EXPANDED cases=%u failures=0\n",cases);
    return 0;
}
