#include <kosdec/adx_au.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"au:%d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct input { uint8_t *allocation,*data; uint32_t bytes,at; size_t fragment; unsigned calls,wait,fail,mode; };
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(24-8*i)); }
/* Independent segment-base representation of the G.711 reconstruction levels. */
static int expected(unsigned encoding,unsigned frame,unsigned ch) {
    unsigned code=(frame+ch*97)&255;
    if(encoding==1) {
        static const unsigned base[]={0,132,396,924,1980,4092,8316,16764};
        unsigned segment=7-((code>>4)&7), interval=15-(code&15);
        int value=(int)(base[segment]+interval*(8u<<segment));
        return code&128?value:-value;
    }
    if(encoding==2)return (code<128?(int)code:(int)code-256)*256;
    unsigned value=(frame+ch*39173)&65535;
    return value<32768?(int)value:(int)value-65536;
}
static struct input fixture(unsigned encoding,unsigned ch,unsigned frames,unsigned extra) {
    struct input in={.fragment=512};in.at=24+extra;in.bytes=in.at+frames*ch*(encoding==3?2:1);
    in.allocation=calloc((size_t)in.bytes+17,1);CHECK(in.allocation);in.data=in.allocation+1;
    memcpy(in.data,".snd",4);put32(in.data+4,in.at);put32(in.data+8,in.bytes-in.at);
    put32(in.data+12,encoding);put32(in.data+16,32000);put32(in.data+20,ch);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<ch;++c) {
        unsigned at=in.at+(f*ch+c)*(encoding==3?2:1);
        if(encoding==3) { unsigned v=(unsigned)expected(encoding,f,c)&65535;in.data[at]=(uint8_t)(v>>8);in.data[at+1]=(uint8_t)v; }
        else in.data[at]=(uint8_t)(f+c*97);
    }
    return in;
}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;++in->calls;
    CHECK(cap && cap<=512 && off<=in->bytes && cap<=in->bytes-off);
    if(in->calls==in->fail) {
        switch(in->mode) {
        case 0:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        case 1:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
        case 2:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,1};
        case 3:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,0};
        default:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap+1};
        }
    }
    if(in->wait && in->calls%in->wait==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=cap;if(n>in->fragment)n=in->fragment;memcpy(dst,in->data+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static enum kosdec_adx_status drain(struct kosdec_adx_au *a,struct input *in,unsigned enc,unsigned ch,unsigned begin,unsigned end,bool filtered) {
    unsigned frames=begin;
    for(unsigned step=0;step<300000;++step) {
        int16_t out[66];for(unsigned i=0;i<66;++i)out[i]=1234;
        unsigned calls=in->calls;size_t cap=step%7?32:1;
        struct kosdec_adx_au_result r=kosdec_adx_au_read(a,out+1,cap);
        CHECK(in->calls-calls<=1 && r.frames<=32 && r.frames<=cap);
        CHECK(out[0]==1234 && out[1+r.frames*ch]==1234);
        for(size_t f=0;f<r.frames;++f)for(unsigned c=0;c<ch;++c)
            CHECK(frames+f<end && out[1+f*ch+c]==(filtered?expected(enc,frames+(unsigned)f,c)/2:expected(enc,frames+(unsigned)f,c)));
        frames+=(unsigned)r.frames;
        if(r.status>=KOSDEC_ADX_DONE) { if(r.status==KOSDEC_ADX_DONE)CHECK(frames==end);return r.status; }
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
static void matrix(void) {
    const size_t fragments[]={1,3,7,511,512};const unsigned counts[]={0,1,31,32,33,257};
    for(unsigned enc=1;enc<=3;++enc)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned variant=0;variant<3;++variant)for(unsigned f=0;f<5;++f)for(unsigned n=0;n<6;++n) {
        struct input in=fixture(enc,ch,counts[n],variant?113:0);in.fragment=fragments[f];in.wait=3;
        if(variant==1)memcpy(in.data,".sd\0",4);
        if(variant==2)put32(in.data+8,UINT32_MAX);
        struct kosdec_adx_au a;CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        struct kosdec_adx_au before=a;CHECK(kosdec_adx_au_read(&a,NULL,0).status==KOSDEC_ADX_NEED_OUTPUT && !in.calls && !memcmp(&a,&before,sizeof(a)));
        struct kosdec_adx_au_info info,binfo;memset(&info,0xa5,sizeof(info));binfo=info;
        CHECK(kosdec_adx_au_get_info(&a,&info)==KOSDEC_ADX_NEED_INPUT && !memcmp(&info,&binfo,sizeof(info)));
        CHECK(drain(&a,&in,enc,ch,0,counts[n],false)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_au_get_info(&a,&info)==KOSDEC_ADX_DONE);
        CHECK(info.sample_rate==32000 && info.channels==ch && info.encoding==enc && info.sample_bits==(enc==3?16:8) && info.total_frames==counts[n] && info.data_offset==in.at && info.data_bytes==in.bytes-in.at);
        unsigned positions[]={0,counts[n]/2,counts[n]};
        for(unsigned i=0;i<3;++i) {
            unsigned calls=in.calls;CHECK(kosdec_adx_au_seek(&a,positions[i])==KOSDEC_ADX_DONE && in.calls==calls);
            CHECK(drain(&a,&in,enc,ch,positions[i],counts[n],false)==KOSDEC_ADX_DONE);
        }
        before=a;CHECK(kosdec_adx_au_seek(&a,counts[n]+1)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&a,&before,sizeof(a)));
        CHECK(kosdec_adx_au_seek(&a,0)==KOSDEC_ADX_DONE);kosdec_adx_au_cancel(&a);
        CHECK(kosdec_adx_au_read(&a,NULL,0).status==(counts[n]?KOSDEC_ADX_CANCELLED:KOSDEC_ADX_DONE));
        CHECK(kosdec_adx_au_get_info(&a,&info)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_au_seek(&a,0)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,ch,0,counts[n],false)==KOSDEC_ADX_DONE);
        free(in.allocation);
    }
    for(unsigned enc=1;enc<=3;++enc) {
        unsigned count=enc==3?65536:256;struct input in=fixture(enc,2,count,1);struct kosdec_adx_au a;
        CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,enc,2,0,count,false)==KOSDEC_ADX_DONE);free(in.allocation);
    }
}
static void malformed(void) {
    for(unsigned mode=0;mode<10;++mode) {
        struct input in=fixture(3,2,33,0);enum kosdec_adx_status want=KOSDEC_ADX_INVALID;
        switch(mode) {
        case 0:in.data[0]='X';want=KOSDEC_ADX_UNSUPPORTED;break;
        case 1:put32(in.data+4,23);break;
        case 2:put32(in.data+4,UINT32_MAX);break;
        case 3:put32(in.data+8,in.bytes);break;
        case 4:put32(in.data+8,3);break;
        case 5:put32(in.data+12,0);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 6:put32(in.data+12,4);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 7:put32(in.data+16,0);break;
        case 8:put32(in.data+20,0);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 9:put32(in.data+20,3);want=KOSDEC_ADX_UNSUPPORTED;break;
        }
        struct kosdec_adx_au a;CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,3,2,0,33,false)==want);
        unsigned calls=in.calls;CHECK(kosdec_adx_au_read(&a,NULL,0).status==want && in.calls==calls);free(in.allocation);
    }
    struct input in=fixture(2,1,33,0);put32(in.data+8,1);struct kosdec_adx_au a;
    CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&a,&in,2,1,0,1,false)==KOSDEC_ADX_DONE);free(in.allocation);
}
static void faults(void) {
    for(unsigned mode=0;mode<5;++mode)for(unsigned call=1;call<=25;++call) {
        struct input in=fixture(3,2,257,1);in.fragment=7;in.mode=mode;in.fail=call;
        struct kosdec_adx_au a;CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,3,2,0,257,false)==(mode?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_TRUNCATED));
        if(a.header_ready) { in.fail=0;CHECK(kosdec_adx_au_seek(&a,100)==KOSDEC_ADX_DONE);CHECK(drain(&a,&in,3,2,100,257,false)==KOSDEC_ADX_DONE); }
        free(in.allocation);
    }
    struct kosdec_adx_au a;memset(&a,0xa5,sizeof(a));struct kosdec_adx_au before=a;
    CHECK(kosdec_adx_au_init(&a,NULL,NULL,24)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&a,&before,sizeof(a)));
    CHECK(kosdec_adx_au_init(NULL,source,NULL,24)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_au_init(&a,source,NULL,23)==KOSDEC_ADX_TRUNCATED);
    int16_t out[64];CHECK(kosdec_adx_au_read(&a,out,32).status==KOSDEC_ADX_TRUNCATED);
    CHECK(kosdec_adx_au_read(NULL,out,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_au_read(&a,NULL,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_au_read(&a,(int16_t *)((uint8_t *)out+1),1).status==KOSDEC_ADX_BAD_ARGUMENT);
}
struct filter_state { unsigned calls,frames[2]; };
static void filter(void *ctx,unsigned ch,int16_t *p,size_t frames,unsigned stride) {
    struct filter_state *s=ctx;CHECK(ch<2 && stride==2 && frames && frames<=32);
    ++s->calls;s->frames[ch]+=(unsigned)frames;for(size_t i=0;i<frames;++i)p[i*stride]/=2;
}
static void filters(void) {
    for(unsigned enc=1;enc<=3;++enc) {
        struct input in=fixture(enc,2,257,0);in.fragment=3;struct kosdec_adx_au a;struct filter_state s={0};
        CHECK(kosdec_adx_au_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_au_set_filter(&a,filter,&s)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,2,0,257,true)==KOSDEC_ADX_DONE && s.frames[0]==257 && s.frames[1]==257);
        CHECK(kosdec_adx_au_set_filter(&a,NULL,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
        CHECK(kosdec_adx_au_seek(&a,100)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,2,100,257,true)==KOSDEC_ADX_DONE && s.frames[0]==414 && s.frames[1]==414);
        CHECK(kosdec_adx_au_seek(&a,0)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_au_set_filter(&a,NULL,&s)==KOSDEC_ADX_DONE && !a.filter_context);
        CHECK(drain(&a,&in,enc,2,0,257,false)==KOSDEC_ADX_DONE);free(in.allocation);
    }
}
int main(void) {matrix();malformed();faults();filters();printf("AU PCM/mu-law: %u checks passed\n",checks);return 0;}
