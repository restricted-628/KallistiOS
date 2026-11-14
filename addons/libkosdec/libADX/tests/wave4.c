#include <kosdec/adx_pipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wave4_vectors.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"wave4:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct input {uint8_t *base,*data;uint32_t bytes;unsigned ch,frames,calls,fail,mode,filtered;size_t fragment;};
static unsigned code(unsigned f,unsigned ch) {
    if(f<10)return ch?15:7;if(f==10)return ch?7:15;
    if(f<27)return ch?26-f:f-11;if(f<37)return ch?15:7;return ch?8:0;
}
static int expected(unsigned f,unsigned ch) {return wave4_pcm[f<128?f:127][ch];}
static void put(uint8_t *p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i));}
static struct input fixture(unsigned ch,unsigned frames) {
    struct input in={.ch=ch,.frames=frames,.fragment=512};in.bytes=44+frames*ch/2;
    in.base=calloc((size_t)in.bytes+17,1);CHECK(in.base);in.data=in.base+1;
    memcpy(in.data,"RIFF",4);put(in.data+4,in.bytes-8,4);memcpy(in.data+8,"WAVEfmt ",8);
    put(in.data+16,16,4);put(in.data+20,1,2);put(in.data+22,ch,2);put(in.data+24,32000,4);
    /* Packed format does not depend on PCM byte-rate/block-alignment hints. */
    put(in.data+28,0,4);put(in.data+32,0,2);put(in.data+34,4,2);
    memcpy(in.data+36,"data",4);put(in.data+40,in.bytes-44,4);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<ch;++c)
        in.data[44+(f/2)*ch+c]|=(uint8_t)(code(f,c)<<(4*(f&1)));
    return in;
}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *out,size_t cap) {
    struct input *in=ctx;++in->calls;CHECK(cap && cap<=512 && off<=in->bytes && cap<=in->bytes-off);
    if(in->fail && in->calls==in->fail) {
        if(in->mode==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        if(in->mode==1)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap+1};
    }
    if(in->calls%5==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>in->fragment)cap=in->fragment;memcpy(out,in->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void filter(void *ctx,unsigned ch,int16_t *pcm,size_t frames,unsigned stride) {
    struct input *in=ctx;CHECK(ch<in->ch && stride==in->ch && frames && frames<=32);
    in->filtered+=(unsigned)frames;for(size_t i=0;i<frames;++i)pcm[i*stride]/=2;
}
static enum kosdec_adx_status drain(struct kosdec_adx_wave *w,struct input *in,unsigned begin,bool filtered) {
    unsigned at=begin;
    for(unsigned i=0;i<100000;++i) {
        int16_t pcm[66];for(unsigned j=0;j<66;++j)pcm[j]=1234;
        unsigned calls=in->calls;size_t capacity=i%33+1;
        struct kosdec_adx_wave_result r=kosdec_adx_wave_read(w,pcm+1,capacity);
        CHECK(in->calls-calls<=1 && r.frames<=32 && r.frames<=capacity);
        CHECK(pcm[0]==1234 && pcm[1+r.frames*in->ch]==1234);
        for(size_t f=0;f<r.frames;++f)for(unsigned ch=0;ch<in->ch;++ch)
            CHECK(at+f<in->frames && pcm[1+f*in->ch+ch]==expected(at+(unsigned)f,ch)/(filtered?2:1));
        at+=(unsigned)r.frames;
        if(r.status>=KOSDEC_ADX_DONE) {if(r.status==KOSDEC_ADX_DONE)CHECK(at==in->frames);return r.status;}
    }CHECK(0);return KOSDEC_ADX_INVALID;
}
static void position(struct kosdec_adx_wave *w,struct input *in,unsigned frame) {
    unsigned calls=in->calls,filtered=in->filtered;
    enum kosdec_adx_status r=kosdec_adx_wave_seek(w,frame);
    CHECK(r==(frame && frame<in->frames?KOSDEC_ADX_MORE:KOSDEC_ADX_DONE) && in->calls==calls);
    for(unsigned i=0;r<KOSDEC_ADX_DONE && i<100000;++i) {
        calls=in->calls;uint32_t before=w->frames_done;r=kosdec_adx_wave_seek_step(w);
        CHECK(in->calls-calls<=1 && w->frames_done-before<=32 && in->filtered==filtered);
    }
    CHECK(r==KOSDEC_ADX_DONE && !w->seeking && w->frames_done==frame);
}
static void matrix(void) {
    const unsigned frames[]={0,4,8,12,32,128,1152};const size_t fragments[]={1,3,7,511,512};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned n=0;n<7;++n)for(unsigned f=0;f<5;++f) {
        struct input in=fixture(ch,frames[n]);in.fragment=fragments[f];struct kosdec_adx_wave w;
        CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&w,&in,0,false)==KOSDEC_ADX_DONE);
        struct kosdec_adx_wave_info info;CHECK(kosdec_adx_wave_get_info(&w,&info)==KOSDEC_ADX_DONE);
        CHECK(info.sample_bits==4 && info.total_frames==in.frames && info.channels==ch);
        unsigned positions[]={0,in.frames/2,in.frames?in.frames-1:0,in.frames};
        for(unsigned p=0;p<4;++p){position(&w,&in,positions[p]);CHECK(drain(&w,&in,positions[p],false)==KOSDEC_ADX_DONE);}
        if(in.frames) {
            position(&w,&in,0);CHECK(kosdec_adx_wave_set_filter(&w,filter,&in)==KOSDEC_ADX_DONE);
            position(&w,&in,in.frames/2);CHECK(drain(&w,&in,in.frames/2,true)==KOSDEC_ADX_DONE);
            CHECK(in.filtered==(in.frames-in.frames/2)*ch);
        }
        struct kosdec_adx_wave before=w;
        CHECK(kosdec_adx_wave_seek(&w,in.frames+1)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&before,&w,sizeof(w)));
        free(in.base);
    }
    /* All nonaligned positions through several history/quantizer transitions. */
    struct input in=fixture(2,128);struct kosdec_adx_wave w;
    CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&w,&in,0,false)==KOSDEC_ADX_DONE);
    for(unsigned p=0;p<=128;++p){position(&w,&in,p);CHECK(drain(&w,&in,p,false)==KOSDEC_ADX_DONE);}
    free(in.base);
}
static void faults(void) {
    for(unsigned mode=0;mode<3;++mode)for(unsigned call=1;call<=25;++call) {
        struct input in=fixture(2,1152);in.fragment=7;in.fail=call;in.mode=mode;struct kosdec_adx_wave w;
        CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&w,&in,0,false)==(mode?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_TRUNCATED));
        if(w.header_ready) {in.fail=0;position(&w,&in,51);CHECK(drain(&w,&in,51,false)==KOSDEC_ADX_DONE);}
        free(in.base);
    }
    struct input in=fixture(2,128);struct kosdec_adx_wave w;
    CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&w,&in,0,false)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_wave_seek(&w,51)==KOSDEC_ADX_MORE);kosdec_adx_wave_cancel(&w);
    unsigned calls=in.calls;CHECK(kosdec_adx_wave_seek_step(&w)==KOSDEC_ADX_CANCELLED && in.calls==calls);
    position(&w,&in,51);CHECK(drain(&w,&in,51,false)==KOSDEC_ADX_DONE);free(in.base);
    in=fixture(1,4);put(in.data+4,UINT32_MAX-8,4);put(in.data+40,UINT32_C(0x80000000),4);
    CHECK(kosdec_adx_wave_init(&w,source,&in,UINT32_MAX)==KOSDEC_ADX_MORE);
    CHECK(drain(&w,&in,0,false)==KOSDEC_ADX_INVALID);free(in.base);
    in=fixture(1,4);put(in.data+40,1,4);
    CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&w,&in,0,false)==KOSDEC_ADX_INVALID);free(in.base);
}
static void pipe_seek(void) {
    struct input in=fixture(2,1152);in.fragment=3;struct kosdec_adx_wave w;
    CHECK(kosdec_adx_wave_init(&w,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    int16_t pcm[64];while(!w.header_ready)CHECK(kosdec_adx_wave_read(&w,pcm,32).status<KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_wave_seek(&w,67)==KOSDEC_ADX_MORE);
    struct kosdec_adx_pipe p;int16_t ring[64];CHECK(kosdec_adx_pipe_init_wave(&p,&w,ring,64,32)==KOSDEC_ADX_MORE);
    unsigned at=67;
    for(unsigned i=0;i<100000;++i) {
        struct kosdec_adx_stream_result s=kosdec_adx_pipe_pump(&p);
        unsigned calls=in.calls;struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&p,pcm,7);
        CHECK(in.calls==calls);
        for(size_t f=0;f<r.frames;++f)for(unsigned ch=0;ch<2;++ch)CHECK(pcm[f*2+ch]==expected(at+(unsigned)f,ch));
        at+=(unsigned)r.frames;
        if(s.status==KOSDEC_ADX_DONE && at==1152)break;
        CHECK(s.status<KOSDEC_ADX_DONE || s.status==KOSDEC_ADX_DONE);
    }
    CHECK(at==1152);free(in.base);
}
int main(void){matrix();faults();pipe_seek();printf("WAV4: %u checks passed\n",checks);return 0;}
