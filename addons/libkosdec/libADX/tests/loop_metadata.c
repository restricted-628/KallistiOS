/* Original loop-header/end-record tests. No repeat-playback semantics asserted. */
#include <kosdec/adx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile_vectors.h"
static unsigned failures, cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("LOOP_META_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
#include "stream_fixture.h"
static enum kosdec_adx_status run(const uint8_t *p,size_t n,size_t fragment,struct kosdec_adx_decoder *d) {
    const int16_t (*expected)[65] = n > 0x18 && p[18] == 4 && p[0x18] ? v4_golden : golden;
    kosdec_adx_init(d);size_t at=0;
    for(size_t calls=0;calls<n+256;++calls) {
        size_t bytes=n-at;if(bytes>fragment)bytes=fragment;
        int16_t out[66];for(unsigned i=0;i<66;++i)out[i]=0x5a5a;
        uint32_t before=d->frames_done;
        struct kosdec_adx_result r=kosdec_adx_decode(d,p+at,bytes,out,32,at+bytes==n);
        CHECK(r.consumed<=bytes && r.consumed<=512 && r.frames<=32);at+=r.consumed;
        for(unsigned i=0;i<r.frames;++i)for(unsigned c=0;c<d->channels;++c)
            CHECK(out[i*d->channels+c]==expected[c][before+i]);
        for(size_t i=r.frames*d->channels;i<66;++i)CHECK(out[i]==0x5a5a);
        if(r.status>=KOSDEC_ADX_DONE)return r.status;
        if(!r.consumed&&!r.frames){CHECK(0);return r.status;}
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
static void check_v4(void) {
    struct kosdec_adx_decoder d;
    struct kosdec_adx_info info;
    for(unsigned channels=1;channels<=2;++channels) {
        for(unsigned header=38;header<=64;header+=26) {
            bool loop=header==64;
            size_t bytes;
            uint8_t *data=stream_version(4,header,18,channels,32,loop?61:32,&bytes);
            put32(data+0x18,0x0140ff4c);
            if(channels==2) put32(data+0x1c,0xfda602c0);
            for(unsigned i=0;i<header;++i)
                CHECK(kosdec_adx_probe_header(data,i,&info)==KOSDEC_ADX_NEED_INPUT);
            CHECK(kosdec_adx_probe_header(data,header,&info)==KOSDEC_ADX_DONE);
            CHECK(info.version==4 && info.channels==channels && info.header_bytes==header);
            CHECK(info.loop.present==loop && !info.inserted_frames);
            if(loop) {
                CHECK(info.loop.start_frame==32 && info.loop.end_frame==61);
                CHECK(info.loop.start_byte==header+18*channels && info.loop.end_byte==header+36*channels);
                data[0x21]=16;  /* Encoded insertion count, separate from v4 histories. */
                CHECK(kosdec_adx_probe_header(data,header,&info)==KOSDEC_ADX_DONE && info.inserted_frames==16);
                put32(data+0x2c,header+1);
                CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_INVALID && !d.header_ready && !d.history[0][0]);
                put32(data+0x2c,header+18*channels);
                put32(data+0x34,header+36*channels+1);
                CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_INVALID && !d.header_ready);
                put32(data+0x34,header+36*channels);
            }
            CHECK(run(data,bytes,1,&d)==KOSDEC_ADX_DONE && d.frames_done==65);
            data[19]=8;
            CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_UNSUPPORTED && !d.header_ready);
            data[19]=9;
            CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_UNSUPPORTED && !d.header_ready);
            data[19]=0;
            data[0x14]=1;
            CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_UNSUPPORTED && !d.header_ready);
            data[0x14]=0;
            if(!loop) {
                data[3]=(uint8_t)(header-5);
                CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_INVALID && !d.header_ready);
            } else {
                data[3]=54; /* 58-byte data start leaves the end offset outside the extension. */
                memcpy(data+52,"(c)CRI",6);
                CHECK(run(data,bytes,7,&d)==KOSDEC_ADX_UNSUPPORTED && !d.header_ready);
            }
            ++cases;
            free(data);
        }
    }
}
int main(void) {
    check_v4();
    const unsigned headers[]={50,1360,2048,65539},tails[]={18,332,564,1724,65539},fragments[]={1,37,512,8192};
    struct kosdec_adx_decoder d;
    for(unsigned h=0;h<4;++h)for(unsigned t=0;t<5;++t)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned variant=0;variant<3;++variant) {
        unsigned start=variant?32:0,end=variant==2?65:61;size_t n;
        uint8_t *p=stream(headers[h],tails[t],ch,start,end,&n);
        for(unsigned f=0;f<4;++f) {
            ++cases;CHECK(run(p,n,fragments[f],&d)==KOSDEC_ADX_DONE);
            CHECK(d.loop.present && d.loop.start_frame==start && d.loop.end_frame==end);
            CHECK(d.loop.start_byte==headers[h]+start/32*18*ch);
            CHECK(d.loop.end_byte==headers[h]+(end+31)/32*18*ch);
            CHECK(d.frames_done==65 && d.footer_bytes==tails[t] && !d.clips);
        }
        free(p);
    }
    size_t n;uint8_t *p=stream(50,18,2,32,61,&n);
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
    p=stream(50,65539,1,0,61,&n);kosdec_adx_init(&d);size_t at=0;
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
