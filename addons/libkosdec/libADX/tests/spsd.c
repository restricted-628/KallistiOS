#include <kosdec/adx_spsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"spsd:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fixture {uint8_t raw[64+131072+2];unsigned bytes,calls,fragment,wait,fail,mode,filtered;struct kosdec_adx_spsd reader;};
static void put(uint8_t *p,uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[i]=(uint8_t)(n>>(8*i));}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *out,size_t n) {
    struct fixture *f=ctx;++f->calls;CHECK(n && n<=512 && off<f->bytes && n<=f->bytes-off);
    if(f->calls==f->fail) return (struct kosdec_adx_input_result){f->mode==0?KOSDEC_ADX_INPUT_END:f->mode==1?KOSDEC_ADX_INPUT_ERROR:KOSDEC_ADX_INPUT_DATA,f->mode==2?n+1:0};
    if(f->wait && !(f->calls%f->wait))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(n>f->fragment)n=f->fragment;memcpy(out,f->raw+1+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void init(struct fixture *f,unsigned frames,unsigned fragment,unsigned version,unsigned flags) {
    memset(f,0,sizeof(*f));f->bytes=64+frames*2;f->fragment=fragment;
    uint8_t *p=f->raw+1;memcpy(p,"SPSD",4);p[4]=(uint8_t)version;p[5]=1;p[7]=4;p[9]=(uint8_t)flags;
    put(p+12,frames*2,4);put(p+42,44100,2);
    for(unsigned i=0;i<frames;++i)put(p+64+2*i,i,2);
    CHECK(kosdec_adx_spsd_init(&f->reader,source,f,f->bytes)==KOSDEC_ADX_MORE);
}
static void filter(void *ctx,unsigned ch,int16_t *pcm,size_t frames,unsigned stride) {
    struct fixture *f=ctx;CHECK(!ch && stride==1 && frames<=32);f->filtered+=(unsigned)frames;
    for(size_t i=0;i<frames;++i)pcm[i]/=2;
}
static int16_t expected(unsigned frame,bool half){int v=frame<32768?(int)frame:(int)frame-65536;return (int16_t)(half?v/2:v);}
static enum kosdec_adx_status drain(struct fixture *f,unsigned from,unsigned end,bool half) {
    unsigned frames=from;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned iteration=0;status<KOSDEC_ADX_DONE;++iteration) {
        CHECK(iteration<1000000);int16_t out[35];for(unsigned i=0;i<35;++i)out[i]=12345;
        size_t cap=iteration%33+1;unsigned before=f->calls;
        struct kosdec_adx_spsd_result r=kosdec_adx_spsd_read(&f->reader,out+1,cap);
        CHECK(f->calls<=before+1 && r.frames<=cap && r.frames<=32 && out[0]==12345 && out[cap+1]==12345);
        CHECK(frames+r.frames<=end);
        for(size_t i=0;i<r.frames;++i)CHECK(out[i+1]==expected(frames+(unsigned)i,half));
        frames+=(unsigned)r.frames;status=r.status;
    }
    if(status==KOSDEC_ADX_DONE)CHECK(frames==end);
    return status;
}
static void conversions(void) {
    const unsigned counts[]={0,1,31,32,33,257,65536},fragments[]={1,3,7,511,512};
    for(unsigned v=0;v<2;++v)for(unsigned c=0;c<7;++c)for(unsigned n=0;n<5;++n) {
        struct fixture f;init(&f,counts[c],fragments[n],v,128);f.wait=5;
        CHECK(drain(&f,0,counts[c],false)==KOSDEC_ADX_DONE);
        struct kosdec_adx_spsd_info info;CHECK(kosdec_adx_spsd_get_info(&f.reader,&info)==KOSDEC_ADX_DONE);
        CHECK(info.total_frames==counts[c] && info.channels==1 && info.sample_bits==16 && info.sample_rate==44100 && info.data_offset==64);
        unsigned points[]={0,counts[c]/2,counts[c]};
        for(unsigned i=0;i<3;++i){unsigned before=f.calls;CHECK(kosdec_adx_spsd_seek(&f.reader,points[i])==KOSDEC_ADX_DONE && f.calls==before);CHECK(drain(&f,points[i],counts[c],false)==KOSDEC_ADX_DONE);}
        struct kosdec_adx_spsd old=f.reader;CHECK(kosdec_adx_spsd_seek(&f.reader,counts[c]+1)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&old,&f.reader,sizeof(old)));
        if(counts[c]){CHECK(kosdec_adx_spsd_seek(&f.reader,0)==KOSDEC_ADX_DONE);CHECK(kosdec_adx_spsd_set_filter(&f.reader,filter,&f)==KOSDEC_ADX_DONE);CHECK(drain(&f,0,counts[c],true)==KOSDEC_ADX_DONE && f.filtered==counts[c]);}
    }
}
static void faults(void) {
    for(unsigned mode=0;mode<3;++mode)for(unsigned fail=1;fail<=20;++fail) {
        struct fixture f;init(&f,257,7,0,0);f.fail=fail;f.mode=mode;
        enum kosdec_adx_status status=drain(&f,0,257,false);CHECK(status==(mode==0?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_IO_ERROR));
        unsigned before=f.calls;int16_t out[32];CHECK(kosdec_adx_spsd_read(&f.reader,out,32).status==status && f.calls==before);
        if(f.reader.header_ready){CHECK(kosdec_adx_spsd_seek(&f.reader,7)==KOSDEC_ADX_DONE);CHECK(drain(&f,7,257,false)==KOSDEC_ADX_DONE);}
    }
    for(unsigned off=0;off<64;++off) {
        struct fixture f;init(&f,0,512,1,0);CHECK(kosdec_adx_spsd_init(&f.reader,source,&f,off)==KOSDEC_ADX_TRUNCATED);CHECK(!f.calls);
    }
    const unsigned fields[]={0,4,5,6,7,8,9,10,11,12,42};
    const unsigned values[]={0,2,0,1,3,3,1,13,1,1,0};
    for(unsigned i=0;i<11;++i) {
        struct fixture f;init(&f,33,512,1,0);f.raw[1+fields[i]]=(uint8_t)values[i];if(fields[i]==42)f.raw[44]=0;
        int16_t pcm[32];enum kosdec_adx_status s=kosdec_adx_spsd_read(&f.reader,pcm,32).status;
        CHECK(s==(i<9?KOSDEC_ADX_UNSUPPORTED:KOSDEC_ADX_INVALID));
    }
    struct fixture f;init(&f,33,512,1,0);put(f.raw+1+12,UINT32_MAX-1,4);int16_t pcm[32];CHECK(kosdec_adx_spsd_read(&f.reader,pcm,32).status==KOSDEC_ADX_INVALID);
    init(&f,33,512,1,0);CHECK(kosdec_adx_spsd_read(&f.reader,pcm,0).status==KOSDEC_ADX_NEED_OUTPUT && !f.calls);
    CHECK(kosdec_adx_spsd_seek(&f.reader,0)==KOSDEC_ADX_BAD_ARGUMENT);CHECK(kosdec_adx_spsd_read(&f.reader,pcm,32).status==KOSDEC_ADX_MORE);
    kosdec_adx_spsd_cancel(&f.reader);CHECK(kosdec_adx_spsd_read(&f.reader,pcm,32).status==KOSDEC_ADX_CANCELLED);
    CHECK(kosdec_adx_spsd_seek(&f.reader,1)==KOSDEC_ADX_DONE);CHECK(drain(&f,1,33,false)==KOSDEC_ADX_DONE);
    init(&f,33,512,1,0);f.bytes+=2;CHECK(kosdec_adx_spsd_init(&f.reader,source,&f,f.bytes)==KOSDEC_ADX_MORE);CHECK(drain(&f,0,33,false)==KOSDEC_ADX_DONE);
}
int main(void){conversions();faults();printf("SPSD: %u checks passed\n",checks);return 0;}
