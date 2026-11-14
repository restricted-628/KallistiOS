/* Original generated-prefix and encoder-padding contracts; no SDK assets. */
#include <kosdec/adx_memory.h>
#include <kosdec/adx_stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#include "memory_vectors.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"INIT_FAIL line=%u\n",(unsigned)__LINE__); abort(); } } while(0)
static unsigned cases;
struct provider { const uint8_t *data; size_t bytes, fragment; unsigned calls; };
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t offset,uint8_t *out,size_t capacity) {
    struct provider *p=ctx;
    if(++p->calls%3==0) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(offset<p->bytes && capacity<=512);
    size_t n=p->bytes-offset;if(n>capacity)n=capacity;if(n>p->fragment)n=p->fragment;
    memcpy(out,p->data+offset,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void padding(void) {
    const unsigned fragments[]={1,17,512};
    for(unsigned ch=1;ch<=2;++ch) for(unsigned ins=0;ins<32;++ins)
    for(unsigned repeat=0;repeat<2;++repeat) for(unsigned f=0;f<3;++f) {
        size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);p[21]=(uint8_t)ins;
        struct kosdec_adx_info info;
        CHECK(kosdec_adx_probe_header(p,n,&info)==KOSDEC_ADX_DONE);
        CHECK(info.inserted_frames==ins && info.loop.start_frame==32 && info.total_frames==65);
        struct kosdec_adx_memory m;kosdec_adx_memory_init(&m,p,n,repeat);
        struct provider provider={p,n,fragments[f],0};
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,read_at,&provider,(uint32_t)n,repeat);
        size_t mi=0,si=0;bool mdone=false,sdone=false;
        unsigned v=0;while(vectors[v].start!=32 || vectors[v].end!=61)++v;
        for(unsigned calls=0;calls<20000 && (!mdone || !sdone);++calls) {
            if(m.wraps==2)kosdec_adx_memory_release_loop(&m);
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            int16_t out[64];
            if(!mdone) {
                struct kosdec_adx_memory_result r=kosdec_adx_memory_read(&m,out,7);
                CHECK(r.status<=KOSDEC_ADX_DONE);
                for(size_t i=0;i<r.frames;++i)for(unsigned c=0;c<ch;++c) {
                    CHECK(mi+i<(repeat?123u:65u));
                    CHECK(out[i*ch+c]==(repeat?vectors[v].pcm[c][mi+i]:golden[c][mi+i]));
                }
                mi+=r.frames;mdone=r.status==KOSDEC_ADX_DONE;
            }
            if(!sdone) {
                unsigned before=provider.calls;
                struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&s,out,1);
                CHECK(r.status<=KOSDEC_ADX_DONE && provider.calls-before<=1);
                for(size_t i=0;i<r.frames;++i)for(unsigned c=0;c<ch;++c) {
                    CHECK(si+i<(repeat?123u:65u));
                    CHECK(out[i*ch+c]==(repeat?vectors[v].pcm[c][si+i]:golden[c][si+i]));
                }
                si+=r.frames;sdone=r.status==KOSDEC_ADX_DONE;
            }
        }
        CHECK(mdone && sdone && mi==si && mi==(repeat?123u:65u));
        CHECK(m.wraps==(repeat?2u:0u) && s.wraps==m.wraps);
        p[21]=32;CHECK(kosdec_adx_probe_header(p,n,&info)==KOSDEC_ADX_UNSUPPORTED);
        p[21]=1;put32(p+28,0);put32(p+32,50);
        CHECK(kosdec_adx_probe_header(p,n,&info)==KOSDEC_ADX_INVALID);
        p[21]=0;put32(p+28,31);
        CHECK(kosdec_adx_probe_header(p,n,&info)==KOSDEC_ADX_UNSUPPORTED);
        free(p);++cases;
    }
}
static void sfa(void) {
    const uint32_t rates[]={1000,8000,11025,16000,22050,24000,32000,44100,48000};
    const uint32_t frames[]={0,1,31,32,33,65,KOSDEC_ADX_MAX_FRAMES};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r)
    for(unsigned f=0;f<sizeof(frames)/sizeof(*frames);++f) {
        uint8_t buffer[KOSDEC_ADX_SFA_HEADER_BYTES+2];memset(buffer,0xa5,sizeof(buffer));
        for(size_t capacity=0;capacity<KOSDEC_ADX_SFA_HEADER_BYTES;++capacity) {
            CHECK(kosdec_adx_make_sfa_header(buffer+1,capacity,ch,rates[r],frames[f])==KOSDEC_ADX_NEED_OUTPUT);
            for(size_t i=0;i<sizeof(buffer);++i)CHECK(buffer[i]==0xa5);
        }
        CHECK(kosdec_adx_make_sfa_header(buffer+1,sizeof(buffer)-1,ch,rates[r],frames[f])==KOSDEC_ADX_DONE);
        CHECK(buffer[0]==0xa5 && buffer[sizeof(buffer)-1]==0xa5);
        struct kosdec_adx_info info;
        CHECK(kosdec_adx_probe_header(buffer+1,KOSDEC_ADX_SFA_HEADER_BYTES,&info)==KOSDEC_ADX_DONE);
        CHECK(info.header_bytes==288 && info.sample_rate==rates[r] && info.total_frames==frames[f]);
        CHECK(info.channels==ch && info.cutoff==500 && info.version==3 && !info.loop.present && !info.inserted_frames);
        CHECK(info.encoding==3 && info.block_bytes==18 && info.sample_bits==4);++cases;
    }
    uint8_t buffer[KOSDEC_ADX_SFA_HEADER_BYTES];memset(buffer,0x5a,sizeof(buffer));
    CHECK(kosdec_adx_make_sfa_header(NULL,0,1,44100,1)==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(kosdec_adx_make_sfa_header(NULL,288,1,44100,1)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_make_sfa_header(buffer,sizeof(buffer),0,44100,1)==KOSDEC_ADX_UNSUPPORTED);
    CHECK(kosdec_adx_make_sfa_header(buffer,sizeof(buffer),257,44100,1)==KOSDEC_ADX_UNSUPPORTED);
    const uint32_t badrates[]={0,999,48001,UINT32_MAX};
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_make_sfa_header(buffer,sizeof(buffer),1,badrates[i],1)==KOSDEC_ADX_UNSUPPORTED);
    CHECK(kosdec_adx_make_sfa_header(buffer,sizeof(buffer),1,44100,UINT32_MAX)==KOSDEC_ADX_UNSUPPORTED);
    for(size_t i=0;i<sizeof(buffer);++i)CHECK(buffer[i]==0x5a);
    /* Prefix + original synthetic body/endcode reaches exact PCM and EOF. */
    for(unsigned ch=1;ch<=2;++ch) {
        size_t n;uint8_t *p=stream(288,18,ch,32,61,&n);
        CHECK(kosdec_adx_make_sfa_header(p,n,ch,44100,65)==KOSDEC_ADX_DONE);
        struct kosdec_adx_decoder d;kosdec_adx_init(&d);size_t used=0,at=0;bool done=false;
        for(unsigned calls=0;calls<1000;++calls) {
            size_t take=n-used;if(take>7)take=7;int16_t pcm[64];
            struct kosdec_adx_result x=kosdec_adx_decode(&d,p+used,take,pcm,32,used+take==n);
            CHECK(x.status<=KOSDEC_ADX_DONE);used+=x.consumed;
            for(size_t i=0;i<x.frames;++i)for(unsigned c=0;c<ch;++c)CHECK(pcm[i*ch+c]==golden[c][at+i]);
            at+=x.frames;if(x.status==KOSDEC_ADX_DONE){done=true;break;}
        }
        CHECK(done && at==65 && used==n);free(p);++cases;
    }
}
int main(void) { padding();sfa();printf("ADX_INITIALIZATION cases=%u failures=0\n",cases);return 0; }
