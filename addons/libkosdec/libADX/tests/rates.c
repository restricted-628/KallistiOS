/* Original certified-profile setup and 22050Hz numerical/loop admission tests. */
#include <kosdec/adx.h>
#include <kosdec/adx_memory.h>
#include <kosdec/adx_stream.h>
#include "adx_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coefficient_vectors.h"
#include "rate_vectors.h"
#include "stream_fixture.h"
static unsigned failures,cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("RATE_FAIL line=%u\n",(unsigned)__LINE__); } } while(0)
struct input {const uint8_t *p;size_t bytes,fragment;unsigned calls,wait;};
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *s=ctx;++s->calls;CHECK(off<s->bytes && cap<=s->bytes-off);
    if(s->wait && s->calls%2==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=cap;if(n>s->fragment)n=s->fragment;
    memcpy(dst,s->p+off,n);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
int main(void) {
    int16_t q[2];
    for(unsigned i=0;i<sizeof(coefficients)/sizeof(coefficients[0]);++i) {
        CHECK(media_adx_coefficients(coefficients[i].rate,500,q)==0);
        CHECK(!memcmp(q,coefficients[i].q12,sizeof(q)));
    }
    const unsigned bad[][2]={{0,500},{1,500},{48001,500},{UINT32_MAX,500},
        {22050,11026},{44100,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        q[0]=123;q[1]=-456;CHECK(media_adx_coefficients(bad[i][0],bad[i][1],q)==-1);
        CHECK(q[0]==123 && q[1]==-456);
    }
    CHECK(media_adx_coefficients(22050,500,NULL)==-1);
    const unsigned fragments[]={1,7,512},caps[]={1,7,32};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned ver=3;ver<=5;ver+=2)
    for(unsigned f=0;f<3;++f) {
        ++cases;size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);
        put32(p+8,22050);p[18]=(uint8_t)ver;memset(p+20,0,24);
        struct kosdec_adx_decoder d;kosdec_adx_init(&d);size_t at=0,frames=0;
        enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned calls=0;calls<1000;++calls) {
            size_t bytes=n-at;if(bytes>fragments[f])bytes=fragments[f];int16_t pcm[66];
            for(unsigned i=0;i<66;++i)pcm[i]=0x5a5a;
            struct kosdec_adx_result z=kosdec_adx_decode(&d,p+at,bytes,pcm,32,at+bytes==n);
            CHECK(z.frames<=32 && z.consumed<=bytes);at+=z.consumed;
            for(size_t i=0;i<z.frames;++i)for(unsigned c=0;c<ch;++c){CHECK(frames+i<65);if(frames+i<65)CHECK(pcm[i*ch+c]==rate_golden[c][frames+i]);}
            for(size_t i=z.frames*ch;i<66;++i)CHECK(pcm[i]==0x5a5a);
            frames+=z.frames;if(z.status>=KOSDEC_ADX_DONE){status=z.status;break;}
        }
        CHECK(status==KOSDEC_ADX_DONE && frames==65 && d.sample_rate==22050 && d.coefficient[0]==6569 && d.coefficient[1]==-2634);
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            if(bad[i][1]>UINT16_MAX)continue;
            put32(p+8,bad[i][0]);p[16]=(uint8_t)(bad[i][1]>>8);p[17]=(uint8_t)bad[i][1];kosdec_adx_init(&d);
            int16_t pcm[64];struct kosdec_adx_result z=kosdec_adx_decode(&d,p,n,pcm,32,true);
            CHECK(z.status==KOSDEC_ADX_UNSUPPORTED && !d.header_ready && !d.frames_done);
        }
        free(p);
    }
    for(unsigned v=0;v<6;++v)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned f=0;f<3;++f)for(unsigned c=0;c<3;++c)for(unsigned wait=0;wait<2;++wait) {
        ++cases;size_t n;uint8_t *p=stream(50,564,ch,rate_vectors[v].start,rate_vectors[v].end,&n);put32(p+8,22050);
        struct input in={p,n,fragments[f],0,wait};struct kosdec_adx_stream s;
        kosdec_adx_stream_init(&s,read_at,&in,(uint32_t)n,true);
        size_t frames=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned calls=0;calls<5000;++calls) {
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            int16_t pcm[64];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,caps[c]);
            for(size_t i=0;i<z.frames;++i)for(unsigned channel=0;channel<ch;++channel){CHECK(frames+i<131);if(frames+i<131)CHECK(pcm[i*ch+channel]==rate_vectors[v].pcm[channel][frames+i]);}
            frames+=z.frames;if(z.status>=KOSDEC_ADX_DONE){status=z.status;break;}
        }
        CHECK(status==KOSDEC_ADX_DONE && frames==65+2*(rate_vectors[v].end-rate_vectors[v].start) && s.wraps==2);
        /* Resident reader carries the same whole-group histories. */
        struct kosdec_adx_memory m;kosdec_adx_memory_init(&m,p,n,true);size_t resident=0;
        for(unsigned calls=0;calls<1000;++calls) {
            if(m.wraps==2)kosdec_adx_memory_release_loop(&m);
            int16_t pcm[64];struct kosdec_adx_memory_result z=kosdec_adx_memory_read(&m,pcm,caps[c]);
            for(size_t i=0;i<z.frames;++i)for(unsigned channel=0;channel<ch;++channel){CHECK(resident+i<131);if(resident+i<131)CHECK(pcm[i*ch+channel]==rate_vectors[v].pcm[channel][resident+i]);}
            resident+=z.frames;if(z.status>=KOSDEC_ADX_DONE){CHECK(z.status==KOSDEC_ADX_DONE);break;}
        }
        CHECK(resident==frames && !memcmp(s.decoder.history,m.decoder.history,sizeof(m.decoder.history)));
        free(p);
    }
    printf("ADX_RATES cases=%u failures=%u\n",cases,failures);return failures?1:0;
}
