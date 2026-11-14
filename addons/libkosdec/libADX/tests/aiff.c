#include <kosdec/adx_aiff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"aiff:%d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct input { uint8_t *allocation,*data; uint32_t bytes,at,comm,ssnd; size_t fragment; unsigned calls,wait,fail,mode; };
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(24-8*i)); }
static void put16(uint8_t *p,unsigned v) {p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void rate80(uint8_t *p,uint32_t rate) {
    unsigned e=0;for(uint32_t v=rate;v>1;v>>=1)++e;
    put16(p,16383+e);uint64_t m=(uint64_t)rate<<(63-e);
    for(unsigned i=0;i<8;++i)p[2+i]=(uint8_t)(m>>(56-8*i));
}
static int expected(unsigned bits,unsigned frame,unsigned ch) {
    if(bits==8) {unsigned c=(frame+ch*97)&255;return (c<128?(int)c:(int)c-256)*256;}
    unsigned value=(frame+ch*39173)&65535;
    return value<32768?(int)value:(int)value-65536;
}
static struct input fixture(unsigned bits,unsigned ch,unsigned frames,unsigned variant) {
    struct input in={.fragment=512};
    unsigned payload=frames*ch*(bits/8), skip=variant==2?3:0, tail=variant==2?5:0;
    unsigned sound_size=8+skip+payload+tail, sound_span=8+sound_size+(sound_size&1);
    unsigned comm_size=variant==2?19:18, comm_span=8+comm_size+(comm_size&1);
    unsigned start=variant==2?24:12;
    in.comm=variant==1?start+sound_span:start;
    in.ssnd=variant==1?start:start+comm_span;
    in.at=in.ssnd+16+skip;in.bytes=start+comm_span+sound_span;
    in.allocation=calloc((size_t)in.bytes+17,1);CHECK(in.allocation);in.data=in.allocation+1;
    memcpy(in.data,"FORM",4);put32(in.data+4,in.bytes-8);memcpy(in.data+8,"AIFF",4);
    if(variant==2) {memcpy(in.data+12,"JUNK",4);put32(in.data+16,3);memcpy(in.data+20,"abc",3);}
    memcpy(in.data+in.comm,"COMM",4);put32(in.data+in.comm+4,comm_size);
    put16(in.data+in.comm+8,ch);put32(in.data+in.comm+10,frames);put16(in.data+in.comm+14,bits);
    rate80(in.data+in.comm+16,32000);
    memcpy(in.data+in.ssnd,"SSND",4);put32(in.data+in.ssnd+4,sound_size);
    put32(in.data+in.ssnd+8,skip);put32(in.data+in.ssnd+12,variant==2?32:0);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<ch;++c) {
        unsigned at=in.at+(f*ch+c)*(bits/8),v=(unsigned)expected(bits,f,c)&65535;
        if(bits==16)put16(in.data+at,v);else in.data[at]=(uint8_t)(f+c*97);
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
static enum kosdec_adx_status drain(struct kosdec_adx_aiff *a,struct input *in,unsigned enc,unsigned ch,unsigned begin,unsigned end,bool filtered) {
    unsigned frames=begin;
    for(unsigned step=0;step<300000;++step) {
        int16_t out[66];for(unsigned i=0;i<66;++i)out[i]=1234;
        unsigned calls=in->calls;size_t cap=step%7?32:1;
        struct kosdec_adx_aiff_result r=kosdec_adx_aiff_read(a,out+1,cap);
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
    for(unsigned enc=8;enc<=16;enc+=8)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned variant=0;variant<3;++variant)for(unsigned f=0;f<5;++f)for(unsigned n=0;n<6;++n) {
        struct input in=fixture(enc,ch,counts[n],variant);in.fragment=fragments[f];in.wait=3;
        struct kosdec_adx_aiff a;CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        struct kosdec_adx_aiff before=a;CHECK(kosdec_adx_aiff_read(&a,NULL,0).status==KOSDEC_ADX_NEED_OUTPUT && !in.calls && !memcmp(&a,&before,sizeof(a)));
        struct kosdec_adx_aiff_info info,binfo;memset(&info,0xa5,sizeof(info));binfo=info;
        CHECK(kosdec_adx_aiff_get_info(&a,&info)==KOSDEC_ADX_NEED_INPUT && !memcmp(&info,&binfo,sizeof(info)));
        CHECK(drain(&a,&in,enc,ch,0,counts[n],false)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_aiff_get_info(&a,&info)==KOSDEC_ADX_DONE);
        CHECK(info.sample_rate==32000 && info.channels==ch && info.sample_bits==enc && info.total_frames==counts[n] && info.data_offset==in.at && info.data_bytes==counts[n]*ch*(enc/8));
        unsigned positions[]={0,counts[n]/2,counts[n]};
        for(unsigned i=0;i<3;++i) {
            unsigned calls=in.calls;CHECK(kosdec_adx_aiff_seek(&a,positions[i])==KOSDEC_ADX_DONE && in.calls==calls);
            CHECK(drain(&a,&in,enc,ch,positions[i],counts[n],false)==KOSDEC_ADX_DONE);
        }
        before=a;CHECK(kosdec_adx_aiff_seek(&a,counts[n]+1)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&a,&before,sizeof(a)));
        CHECK(kosdec_adx_aiff_seek(&a,0)==KOSDEC_ADX_DONE);kosdec_adx_aiff_cancel(&a);
        CHECK(kosdec_adx_aiff_read(&a,NULL,0).status==(counts[n]?KOSDEC_ADX_CANCELLED:KOSDEC_ADX_DONE));
        CHECK(kosdec_adx_aiff_get_info(&a,&info)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_aiff_seek(&a,0)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,ch,0,counts[n],false)==KOSDEC_ADX_DONE);
        free(in.allocation);
    }
    for(unsigned enc=8;enc<=16;enc+=8) {
        unsigned count=enc==16?65536:256;struct input in=fixture(enc,2,count,1);struct kosdec_adx_aiff a;
        CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,enc,2,0,count,false)==KOSDEC_ADX_DONE);free(in.allocation);
    }
}
static void malformed(void) {
    for(unsigned mode=0;mode<14;++mode) {
        struct input in=fixture(16,2,33,0);enum kosdec_adx_status want=KOSDEC_ADX_INVALID;
        switch(mode) {
        case 0:in.data[0]='X';want=KOSDEC_ADX_UNSUPPORTED;break;
        case 1:in.data[11]='C';want=KOSDEC_ADX_UNSUPPORTED;break;
        case 2:put32(in.data+4,UINT32_MAX);break;
        case 3:put32(in.data+4,3);break;
        case 4:put32(in.data+in.comm+4,17);break;
        case 5:put16(in.data+in.comm+8,3);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 6:put16(in.data+in.comm+14,24);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 7:put32(in.data+in.comm+10,UINT32_MAX);break;
        case 8:put32(in.data+in.ssnd+4,7);break;
        case 9:put32(in.data+in.ssnd+8,UINT32_MAX);break;
        case 10:memcpy(in.data+in.comm,"JUNK",4);break;
        case 11:memcpy(in.data+in.ssnd,"JUNK",4);break;
        case 12:put32(in.data+in.ssnd+4,UINT32_MAX);break;
        case 13:put32(in.data+in.ssnd+4,9);break;
        }
        struct kosdec_adx_aiff a;CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,16,2,0,33,false)==want);
        unsigned calls=in.calls;CHECK(kosdec_adx_aiff_read(&a,NULL,0).status==want && in.calls==calls);free(in.allocation);
    }
    /* Empty COMM without SSND is a complete empty sound. */
    struct input in=fixture(8,1,0,0);in.bytes=in.ssnd;put32(in.data+4,in.bytes-8);
    struct kosdec_adx_aiff a;CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
    CHECK(drain(&a,&in,8,1,0,0,false)==KOSDEC_ADX_DONE);free(in.allocation);
}
static void rates(void) {
    const uint32_t valid[]={1,8000,11025,16000,22050,24000,32000,44100,48000,65537,UINT32_MAX};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);++i) {
        struct input in=fixture(16,1,1,0);rate80(in.data+in.comm+16,valid[i]);struct kosdec_adx_aiff a;
        CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,16,1,0,1,false)==KOSDEC_ADX_DONE && a.info.sample_rate==valid[i]);free(in.allocation);
    }
    for(unsigned i=0;i<7;++i) {
        struct input in=fixture(16,1,1,0);uint8_t *p=in.data+in.comm+16;
        enum kosdec_adx_status want=KOSDEC_ADX_INVALID;
        switch(i) {
        case 0:p[0]|=128;break;
        case 1:memset(p,0,10);break;
        case 2:put16(p,0x7fff);break;
        case 3:p[2]=0;break;
        case 4:put16(p,16382);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 5:put16(p,16415);want=KOSDEC_ADX_UNSUPPORTED;break;
        case 6:p[9]=1;want=KOSDEC_ADX_UNSUPPORTED;break;
        }
        struct kosdec_adx_aiff a;CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,16,1,0,1,false)==want);free(in.allocation);
    }
}
static void faults(void) {
    for(unsigned mode=0;mode<5;++mode)for(unsigned call=1;call<=25;++call) {
        struct input in=fixture(16,2,257,1);in.fragment=7;in.mode=mode;in.fail=call;
        struct kosdec_adx_aiff a;CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(drain(&a,&in,16,2,0,257,false)==(mode?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_TRUNCATED));
        if(a.header_ready) { in.fail=0;CHECK(kosdec_adx_aiff_seek(&a,100)==KOSDEC_ADX_DONE);CHECK(drain(&a,&in,16,2,100,257,false)==KOSDEC_ADX_DONE); }
        free(in.allocation);
    }
    struct kosdec_adx_aiff a;memset(&a,0xa5,sizeof(a));struct kosdec_adx_aiff before=a;
    CHECK(kosdec_adx_aiff_init(&a,NULL,NULL,24)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&a,&before,sizeof(a)));
    CHECK(kosdec_adx_aiff_init(NULL,source,NULL,24)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_aiff_init(&a,source,NULL,11)==KOSDEC_ADX_TRUNCATED);
    int16_t out[64];CHECK(kosdec_adx_aiff_read(&a,out,32).status==KOSDEC_ADX_TRUNCATED);
    CHECK(kosdec_adx_aiff_read(NULL,out,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_aiff_read(&a,NULL,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_aiff_read(&a,(int16_t *)((uint8_t *)out+1),1).status==KOSDEC_ADX_BAD_ARGUMENT);
}
struct filter_state { unsigned calls,frames[2]; };
static void filter(void *ctx,unsigned ch,int16_t *p,size_t frames,unsigned stride) {
    struct filter_state *s=ctx;CHECK(ch<2 && stride==2 && frames && frames<=32);
    ++s->calls;s->frames[ch]+=(unsigned)frames;for(size_t i=0;i<frames;++i)p[i*stride]/=2;
}
static void filters(void) {
    for(unsigned enc=8;enc<=16;enc+=8) {
        struct input in=fixture(enc,2,257,0);in.fragment=3;struct kosdec_adx_aiff a;struct filter_state s={0};
        CHECK(kosdec_adx_aiff_init(&a,source,&in,in.bytes)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_aiff_set_filter(&a,filter,&s)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,2,0,257,true)==KOSDEC_ADX_DONE && s.frames[0]==257 && s.frames[1]==257);
        CHECK(kosdec_adx_aiff_set_filter(&a,NULL,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
        CHECK(kosdec_adx_aiff_seek(&a,100)==KOSDEC_ADX_DONE);
        CHECK(drain(&a,&in,enc,2,100,257,true)==KOSDEC_ADX_DONE && s.frames[0]==414 && s.frames[1]==414);
        CHECK(kosdec_adx_aiff_seek(&a,0)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_aiff_set_filter(&a,NULL,&s)==KOSDEC_ADX_DONE && !a.filter_context);
        CHECK(drain(&a,&in,enc,2,0,257,false)==KOSDEC_ADX_DONE);free(in.allocation);
    }
}
int main(void) {matrix();malformed();rates();faults();filters();printf("AIFF PCM: %u checks passed\n",checks);return 0;}
