/* Original loop-header/end-record tests. No repeat-playback semantics asserted. */
#include <kosdec/adx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile_vectors.h"
static unsigned failures, cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("LOOP_META_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(24-i*8));
}
static uint8_t *make_stream(unsigned hdr,unsigned tail,unsigned ch,unsigned start,unsigned end,size_t *size) {
    *size=hdr+3*18*ch+tail;
    uint8_t *p=calloc(*size+1,1); if(!p) exit(2);
    p[0]=0x80; p[2]=(uint8_t)((hdr-4)>>8); p[3]=(uint8_t)(hdr-4);
    p[4]=3;p[5]=18;p[6]=4;p[7]=(uint8_t)ch;put32(p+8,44100);put32(p+12,65);
    p[16]=1;p[17]=0xf4;p[18]=3;
    put32(p+20,1);put32(p+24,1);put32(p+28,start);
    put32(p+32,hdr+start/32*18*ch);put32(p+36,end);put32(p+40,hdr+(end+31)/32*18*ch);
    memcpy(p+hdr-6,"(c)CRI",6);
    for(unsigned g=0;g<3;++g) for(unsigned c=0;c<ch;++c) {
        uint8_t *b=p+hdr+(g*ch+c)*18;b[1]=(uint8_t)(c+1);
        for(unsigned i=2;i<18;++i)b[i]=(uint8_t)(i*7+g*3+c*19);
    }
    uint8_t *f=p+*size-tail;f[0]=0x80;f[1]=1;f[2]=(uint8_t)((tail-4)>>8);f[3]=(uint8_t)(tail-4);
    return p;
}
static enum kosdec_adx_status run(const uint8_t *p,size_t n,size_t fragment,struct kosdec_adx_decoder *d) {
    kosdec_adx_init(d);size_t at=0;
    for(size_t calls=0;calls<n+256;++calls) {
        size_t bytes=n-at;if(bytes>fragment)bytes=fragment;
        int16_t out[66];for(unsigned i=0;i<66;++i)out[i]=0x5a5a;
        uint32_t before=d->frames_done;
        struct kosdec_adx_result r=kosdec_adx_decode(d,p+at,bytes,out,32,at+bytes==n);
        CHECK(r.consumed<=bytes && r.consumed<=512 && r.frames<=32);at+=r.consumed;
        for(unsigned i=0;i<r.frames;++i)for(unsigned c=0;c<d->channels;++c)
            CHECK(out[i*d->channels+c]==golden[c][before+i]);
        for(size_t i=r.frames*d->channels;i<66;++i)CHECK(out[i]==0x5a5a);
        if(r.status>=KOSDEC_ADX_DONE)return r.status;
        if(!r.consumed&&!r.frames){CHECK(0);return r.status;}
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
int main(void) {
    const unsigned headers[]={50,1360,2048,65539},tails[]={18,332,564,1724,65539},fragments[]={1,37,512,8192};
    struct kosdec_adx_decoder d;
    for(unsigned h=0;h<4;++h)for(unsigned t=0;t<5;++t)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned variant=0;variant<3;++variant) {
        unsigned start=variant?32:0,end=variant==2?65:61;size_t n;
        uint8_t *p=make_stream(headers[h],tails[t],ch,start,end,&n);
        for(unsigned f=0;f<4;++f) {
            ++cases;CHECK(run(p,n,fragments[f],&d)==KOSDEC_ADX_DONE);
            CHECK(d.loop.present && d.loop.start_frame==start && d.loop.end_frame==end);
            CHECK(d.loop.start_byte==headers[h]+start/32*18*ch);
            CHECK(d.loop.end_byte==headers[h]+(end+31)/32*18*ch);
            CHECK(d.frames_done==65 && d.footer_bytes==tails[t] && !d.clips);
        }
        free(p);
    }
    size_t n;uint8_t *p=make_stream(50,18,2,32,61,&n);
    /* Every truncated prefix, including all loop fields. */
    for(size_t end=0;end<n;++end)CHECK(run(p,end,7,&d)==KOSDEC_ADX_TRUNCATED);
    const struct {unsigned offset;uint32_t value;enum kosdec_adx_status status;} bad[]={
        {20,2,KOSDEC_ADX_UNSUPPORTED},{20,0x200001,KOSDEC_ADX_UNSUPPORTED},
        {24,2,KOSDEC_ADX_UNSUPPORTED},{28,61,KOSDEC_ADX_INVALID},
        {28,31,KOSDEC_ADX_UNSUPPORTED},{36,66,KOSDEC_ADX_INVALID},
        {32,51,KOSDEC_ADX_INVALID},{40,51,KOSDEC_ADX_INVALID}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        uint8_t saved[4];memcpy(saved,p+bad[i].offset,4);put32(p+bad[i].offset,bad[i].value);
        CHECK(run(p,n,7,&d)==bad[i].status && !d.header_ready && !d.frames_done);
        memcpy(p+bad[i].offset,saved,4);
    }
    p[18]=5;CHECK(run(p,n,7,&d)==KOSDEC_ADX_UNSUPPORTED);p[18]=3;
    p[n-1]=1;CHECK(run(p,n,7,&d)==KOSDEC_ADX_INVALID);p[n-1]=0;
    CHECK(run(p,n+1,7,&d)==KOSDEC_ADX_INVALID);
    p[n-15]=13;CHECK(run(p,n,7,&d)==KOSDEC_ADX_UNSUPPORTED);free(p);
    /* Large footer budget plus cancellation must not consume another byte. */
    p=make_stream(50,65539,1,0,61,&n);kosdec_adx_init(&d);size_t at=0;
    while(d.frames_done<65) {
        int16_t pcm[32];struct kosdec_adx_result r=kosdec_adx_decode(&d,p+at,n-at,pcm,32,true);
        CHECK(r.status==KOSDEC_ADX_MORE);at+=r.consumed;
    }
    struct kosdec_adx_result r=kosdec_adx_decode(&d,p+at,n-at,NULL,0,true);
    CHECK(r.status==KOSDEC_ADX_MORE && r.consumed==512);at+=r.consumed;
    kosdec_adx_cancel(&d);r=kosdec_adx_decode(&d,p+at,n-at,NULL,0,true);
    CHECK(r.status==KOSDEC_ADX_CANCELLED && !r.consumed);free(p);
    printf("ADX_LOOP_METADATA cases=%u failures=%u\n",cases,failures);
    return failures?1:0;
}
