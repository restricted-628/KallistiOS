#include <kosdec/adx_wave.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"wave:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct input {uint8_t *allocation,*data;uint32_t bytes,data_at;size_t fragment;unsigned calls,wait_every,fail,mode;};
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static int16_t sample(unsigned frame,unsigned ch,unsigned bits) {
    if(bits==8)return (int16_t)(((int)((frame+ch*97)%256)-128)*256);
    static const int v[]={-32768,-32767,-1,0,1,32766,32767,12345,-23456};
    return (int16_t)v[(frame+ch*5)%9];
}
static struct input fixture(unsigned bits,unsigned ch,unsigned frames,unsigned extra) {
    struct input in={.fragment=512};in.data_at=44+(extra?14:0);
    in.bytes=in.data_at+frames*ch*(bits/8);if(in.bytes&1)++in.bytes;
    in.allocation=calloc((size_t)in.bytes+17,1);CHECK(in.allocation);in.data=in.allocation+1;
    memcpy(in.data,"RIFF",4);put32(in.data+4,in.bytes-8);memcpy(in.data+8,"WAVE",4);
    unsigned fmt=12;if(extra){memcpy(in.data+12,"JUNK",4);put32(in.data+16,3);memcpy(in.data+20,"xyz",3);fmt=24;}
    memcpy(in.data+fmt,"fmt ",4);put32(in.data+fmt+4,extra?17:16);
    put16(in.data+fmt+8,1);put16(in.data+fmt+10,ch);put32(in.data+fmt+12,32000);
    put32(in.data+fmt+16,32000*ch*(bits/8));put16(in.data+fmt+20,ch*(bits/8));put16(in.data+fmt+22,bits);
    memcpy(in.data+in.data_at-8,"data",4);put32(in.data+in.data_at-4,frames*ch*(bits/8));
    for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<ch;++c) {
        int16_t v=sample(i,c,bits);unsigned at=in.data_at+(i*ch+c)*(bits/8);
        if(bits==8)in.data[at]=(uint8_t)(v/256+128);else put16(in.data+at,(uint16_t)v);
    }
    return in;
}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;++in->calls;CHECK(cap && cap<=512 && off<=in->bytes && cap<=in->bytes-off);
    if(in->calls==in->fail) {
        switch(in->mode) {
        case 0:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        case 1:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
        case 2:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,1};
        case 3:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,0};
        default:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap+1};
        }
    }
    if(in->wait_every && in->calls%in->wait_every==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=cap;if(n>in->fragment)n=in->fragment;memcpy(dst,in->data+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static enum kosdec_adx_status drain(struct kosdec_adx_wave *w,struct input *in,
                                   unsigned bits,unsigned ch,unsigned begin,unsigned total) {
    unsigned frames=begin;
    for(unsigned step=0;step<20000;++step) {
        int16_t pcm[66];for(unsigned i=0;i<66;++i)pcm[i]=1234;
        unsigned calls=in->calls;size_t cap=step%7?32:1;
        struct kosdec_adx_wave_result r=kosdec_adx_wave_read(w,pcm+1,cap);
        CHECK(in->calls-calls<=1 && r.frames<=32 && r.frames<=cap);
        CHECK(pcm[0]==1234 && pcm[1+r.frames*ch]==1234);
        for(size_t f=0;f<r.frames;++f)for(unsigned c=0;c<ch;++c)CHECK(frames+f<total && pcm[1+f*ch+c]==sample(frames+(unsigned)f,c,bits));
        frames+=(unsigned)r.frames;
        if(r.status>=KOSDEC_ADX_DONE){if(r.status==KOSDEC_ADX_DONE)CHECK(frames==total);return r.status;}
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
static void matrix(void) {
    size_t fragments[]={1,3,7,511,512};unsigned counts[]={0,1,31,32,33,257};
    for(unsigned bits=8;bits<=16;bits+=8)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned ext=0;ext<2;++ext)for(unsigned f=0;f<5;++f)for(unsigned n=0;n<6;++n) {
        struct input in=fixture(bits,ch,counts[n],ext);in.fragment=fragments[f];in.wait_every=3;
        struct kosdec_adx_wave w;CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        struct kosdec_adx_wave before=w;CHECK(kosdec_adx_wave_read(&w,NULL,0).status==KOSDEC_ADX_NEED_OUTPUT && !memcmp(&w,&before,sizeof(w)) && !in.calls);
        CHECK(drain(&w,&in,bits,ch,0,counts[n])==KOSDEC_ADX_DONE);
        struct kosdec_adx_wave_info info;CHECK(kosdec_adx_wave_get_info(&w,&info)==KOSDEC_ADX_DONE);
        CHECK(info.total_frames==counts[n] && info.channels==ch && info.sample_bits==bits && info.sample_rate==32000 && info.data_offset==in.data_at);
        unsigned positions[]={0,counts[n]/2,counts[n]};
        for(unsigned i=0;i<3;++i) {
            unsigned calls=in.calls;CHECK(kosdec_adx_wave_seek(&w,positions[i])==KOSDEC_ADX_DONE && in.calls==calls);
            CHECK(drain(&w,&in,bits,ch,positions[i],counts[n])==KOSDEC_ADX_DONE);
        }
        before=w;CHECK(kosdec_adx_wave_seek(&w,counts[n]+1)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&w,&before,sizeof(w)));
        CHECK(kosdec_adx_wave_seek(&w,0)==KOSDEC_ADX_DONE);kosdec_adx_wave_cancel(&w);
        if(counts[n])CHECK(kosdec_adx_wave_read(&w,NULL,0).status==KOSDEC_ADX_CANCELLED);
        CHECK(kosdec_adx_wave_get_info(&w,&info)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_wave_seek(&w,0)==KOSDEC_ADX_DONE);CHECK(drain(&w,&in,bits,ch,0,counts[n])==KOSDEC_ADX_DONE);
        free(in.allocation);
    }
}
static void malformed(void) {
    for(unsigned mode=0;mode<16;++mode) {
        struct input in=fixture(16,2,33,0);enum kosdec_adx_status expected=KOSDEC_ADX_INVALID;
        switch(mode) {
        case 0:in.data[0]='X';expected=KOSDEC_ADX_UNSUPPORTED;break;
        case 1:in.data[8]='X';expected=KOSDEC_ADX_UNSUPPORTED;break;
        case 2:put32(in.data+4,UINT32_MAX);break;
        case 3:put32(in.data+4,3);break;
        case 4:put32(in.data+16,15);break;
        case 5:put16(in.data+20,3);expected=KOSDEC_ADX_UNSUPPORTED;break;
        case 6:put16(in.data+22,3);expected=KOSDEC_ADX_UNSUPPORTED;break;
        case 7:put16(in.data+34,12);expected=KOSDEC_ADX_UNSUPPORTED;break;
        case 8:put32(in.data+24,0);break;
        case 9:put32(in.data+28,1);break;
        case 10:put16(in.data+32,1);break;
        case 11:put32(in.data+40,131);break;
        case 12:put32(in.data+40,UINT32_MAX);break;
        case 13:memcpy(in.data+12,"data",4);break;
        case 14:memcpy(in.data+36,"fmt ",4);break;
        case 15:memcpy(in.data+36,"JUNK",4);break;
        }
        struct kosdec_adx_wave w;CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&w,&in,16,2,0,33)==expected);unsigned calls=in.calls;
        int16_t out[64];CHECK(kosdec_adx_wave_read(&w,out,32).status==expected && in.calls==calls);
        struct kosdec_adx_wave_info info={.sample_rate=123};CHECK(kosdec_adx_wave_get_info(&w,&info)==expected && info.sample_rate==123);
        free(in.allocation);
    }
    /* Missing odd-chunk padding must not move parser outside RIFF. */
    struct input in=fixture(8,1,1,0);--in.bytes;put32(in.data+4,in.bytes-8);
    struct kosdec_adx_wave w;CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&w,&in,8,1,0,1)==KOSDEC_ADX_INVALID);free(in.allocation);
}
static void faults(void) {
    for(unsigned mode=0;mode<5;++mode)for(unsigned call=1;call<=25;++call) {
        struct input in=fixture(16,2,257,1);in.fragment=7;in.mode=mode;in.fail=call;
        struct kosdec_adx_wave w;CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&w,&in,16,2,0,257)==(mode?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_TRUNCATED));
        if(w.header_ready){in.fail=0;CHECK(kosdec_adx_wave_seek(&w,100)==KOSDEC_ADX_DONE);CHECK(drain(&w,&in,16,2,100,257)==KOSDEC_ADX_DONE);}
        free(in.allocation);
    }
    struct kosdec_adx_wave w;memset(&w,0xa5,sizeof(w));struct kosdec_adx_wave before=w;
    CHECK(kosdec_adx_wave_init(&w,NULL,NULL,12)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&w,&before,sizeof(w)));
    CHECK(kosdec_adx_wave_init(&w,source,NULL,11)==KOSDEC_ADX_TRUNCATED);
    int16_t out[64];CHECK(kosdec_adx_wave_read(&w,out,32).status==KOSDEC_ADX_TRUNCATED);
}
int main(void) {matrix();malformed();faults();printf("PCM WAV: %u checks passed\n",checks);return 0;}
