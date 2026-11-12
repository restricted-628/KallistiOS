/* Original reader lifecycle and numerical boundary tests. */
#include <kosdec/adx_memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_vectors.h"
#include "profile_vectors.h"
static unsigned failures, cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("MEMORY_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(24-i*8));
}
static uint8_t *stream(unsigned hdr,unsigned tail,unsigned ch,unsigned start,unsigned end,size_t *size) {
    *size=hdr+3*18*ch+tail;
    uint8_t *p=calloc(*size+1,1); if(!p) exit(2);
    p[0]=0x80;p[2]=(uint8_t)((hdr-4)>>8);p[3]=(uint8_t)(hdr-4);
    p[4]=3;p[5]=18;p[6]=4;p[7]=(uint8_t)ch;put32(p+8,44100);put32(p+12,65);
    p[16]=1;p[17]=0xf4;p[18]=3;
    put32(p+20,1);put32(p+24,1);put32(p+28,start);
    put32(p+32,hdr+start/32*18*ch);put32(p+36,end);put32(p+40,hdr+(end+31)/32*18*ch);
    memcpy(p+hdr-6,"(c)CRI",6);
    for(unsigned g=0;g<3;++g)for(unsigned c=0;c<ch;++c) {
        uint8_t *b=p+hdr+(g*ch+c)*18;b[1]=(uint8_t)(c+1);
        for(unsigned i=2;i<18;++i)b[i]=(uint8_t)(i*7+g*3+c*19);
    }
    uint8_t *f=p+*size-tail;f[0]=0x80;f[1]=1;f[2]=(uint8_t)((tail-4)>>8);f[3]=(uint8_t)(tail-4);
    return p;
}
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
    for(unsigned h=0;h<3;++h)for(unsigned t=0;t<2;++t) {
        ++cases;size_t n;uint8_t *p=stream(headers[h],tails[t],ch,vectors[v].start,vectors[v].end,&n);
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
