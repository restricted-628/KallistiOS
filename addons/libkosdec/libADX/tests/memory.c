/* Original reader lifecycle and numerical boundary tests. */
#include <kosdec/adx_memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_vectors.h"
#include "profile_vectors.h"
static unsigned failures, cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("MEMORY_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
#include "stream_fixture.h"
static enum kosdec_adx_status drain(struct kosdec_adx_memory *r) {
    for(unsigned i=0;i<1000;++i) {
        int16_t out[64];struct kosdec_adx_memory_result z=kosdec_adx_memory_read(r,out,32);
        if(z.status>=KOSDEC_ADX_DONE)return z.status;
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
int main(void) {
    const unsigned capacities[]={1,7,31,32,64},headers[]={50,2048,65539},tails[]={18,1724};
    for(unsigned v=0;v<sizeof(vectors)/sizeof(vectors[0]);++v)
    for(unsigned ch=1;ch<=2;++ch)for(unsigned c=0;c<5;++c)
    for(unsigned h=0;h<3;++h)for(unsigned t=0;t<2;++t)
    for(unsigned version=3;version<=4;++version) {
        unsigned header=version==4 && h==0 ? 62 : headers[h];
        ++cases;size_t n;
        uint8_t *p=stream_version(version,header,tails[t],ch,vectors[v].start,vectors[v].end,&n);
        struct kosdec_adx_memory r;kosdec_adx_memory_init(&r,p,n,true);
        size_t emitted=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned calls=0;calls<1000;++calls) {
            if(r.wraps==2)kosdec_adx_memory_release_loop(&r);
            /* Pause freezes header, cache, loop and footer state. */
            kosdec_adx_memory_pause(&r,true);struct kosdec_adx_memory saved=r;
            struct kosdec_adx_memory_result z=kosdec_adx_memory_read(&r,NULL,0);
            CHECK(z.status==KOSDEC_ADX_MORE && !z.frames && !memcmp(&saved,&r,sizeof(r)));
            kosdec_adx_memory_pause(&r,false);
            if(r.prepared && r.source_frame<65) {
                saved=r;z=kosdec_adx_memory_read(&r,NULL,0);
                CHECK(z.status==KOSDEC_ADX_NEED_OUTPUT && !memcmp(&saved,&r,sizeof(r)));
            }
            int16_t out[130];for(unsigned i=0;i<130;++i)out[i]=0x5a5a;
            z=kosdec_adx_memory_read(&r,out,capacities[c]);
            CHECK(z.frames<=32 && z.frames<=capacities[c]);
            for(size_t i=0;i<z.frames;++i)for(unsigned channel=0;channel<ch;++channel) {
                CHECK(emitted+i<131);
                if(emitted+i<131)CHECK(out[i*ch+channel]==vectors[v].pcm[channel][emitted+i]);
            }
            for(size_t i=z.frames*ch;i<130;++i)CHECK(out[i]==0x5a5a);
            emitted+=z.frames;CHECK(r.returned_frames==emitted);
            if(z.status>=KOSDEC_ADX_DONE){status=z.status;break;}
        }
        CHECK(status==KOSDEC_ADX_DONE && r.wraps==2 && r.source_frame==65);
        CHECK(emitted==65+2*(vectors[v].end-vectors[v].start));
        struct kosdec_adx_memory saved=r;kosdec_adx_memory_cancel(&r);kosdec_adx_memory_pause(&r,true);
        kosdec_adx_memory_release_loop(&r);CHECK(!memcmp(&saved,&r,sizeof(r)));
        CHECK(kosdec_adx_memory_read(&r,NULL,0).status==KOSDEC_ADX_DONE);free(p);
    }
    size_t n;uint8_t *p=stream(50,18,2,32,61,&n);struct kosdec_adx_memory r;
    /* Release before start, at endpoint, and while holding a decoded tail. */
    const unsigned release_at[]={0,1,32,60,61};
    for(unsigned k=0;k<5;++k) {
        kosdec_adx_memory_init(&r,p,n,true);size_t at=0;
        for(unsigned i=0;i<100;++i) {
            if(at==release_at[k])kosdec_adx_memory_release_loop(&r);
            int16_t out[2];struct kosdec_adx_memory_result z=kosdec_adx_memory_read(&r,out,1);
            if(z.frames){CHECK(at<65);if(at<65){CHECK(out[0]==golden[0][at]);CHECK(out[1]==golden[1][at]);}++at;}
            if(z.status>=KOSDEC_ADX_DONE){CHECK(z.status==KOSDEC_ADX_DONE);break;}
        }
        CHECK(at==65 && !r.wraps);
    }
    /* Every truncated prefix, malformed scale/footer, unsupported repeat. */
    for(size_t bytes=0;bytes<n;++bytes) {
        kosdec_adx_memory_init(&r,p,bytes,false);CHECK(drain(&r)==KOSDEC_ADX_TRUNCATED);
    }
    p[n-1]=1;kosdec_adx_memory_init(&r,p,n,false);CHECK(drain(&r)==KOSDEC_ADX_INVALID);p[n-1]=0;
    kosdec_adx_memory_init(&r,p,n+1,false);CHECK(drain(&r)==KOSDEC_ADX_INVALID);
    p[68]=0x80;kosdec_adx_memory_init(&r,p,n,false);CHECK(drain(&r)==KOSDEC_ADX_INVALID);
    CHECK(!r.returned_frames && !r.decoder.history[0][0]);p[68]=0;
    memset(p+20,0,24);kosdec_adx_memory_init(&r,p,n,true);CHECK(drain(&r)==KOSDEC_ADX_UNSUPPORTED);
    kosdec_adx_memory_init(&r,p,n,false);
    int16_t out[64];CHECK(kosdec_adx_memory_read(&r,out,1).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_memory_read(&r,out,1).frames==1);
    struct kosdec_adx_memory saved=r;
    CHECK(kosdec_adx_memory_read(&r,NULL,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_memory_read(&r,(int16_t *)((uint8_t *)out+1),1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&saved,&r,sizeof(r)));
    kosdec_adx_memory_cancel(&r);saved=r;CHECK(drain(&r)==KOSDEC_ADX_CANCELLED);
    CHECK(!memcmp(&saved,&r,sizeof(r)));
    kosdec_adx_memory_init(&r,NULL,1,false);CHECK(drain(&r)==KOSDEC_ADX_BAD_ARGUMENT);
    kosdec_adx_memory_init(&r,NULL,0,false);CHECK(drain(&r)==KOSDEC_ADX_TRUNCATED);
    /* Empty supported file reaches footer without output capacity. */
    put32(p+12,0);memcpy(p+50,p+n-18,18);kosdec_adx_memory_init(&r,p,68,false);
    CHECK(kosdec_adx_memory_read(&r,NULL,0).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_memory_read(&r,NULL,0).status==KOSDEC_ADX_DONE);
    free(p);printf("ADX_MEMORY cases=%u failures=%u\n",cases,failures);return failures?1:0;
}
