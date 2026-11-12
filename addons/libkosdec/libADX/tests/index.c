/* Original indexed-input bounds and end-to-end PCM tests. */
#include <kosdec/adx_index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "memory_vectors.h"
static unsigned failures,cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("INDEX_FAIL line=%u\n",(unsigned)__LINE__); } } while(0)
static void field(uint8_t *p,uint32_t value,enum kosdec_adx_container format) {
    if(format==KOSDEC_ADX_ACX)put32(p,value);
    else for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8));
}
struct input {
    uint8_t *data;uint32_t bytes,base,length;
    unsigned fragment,calls,wait,fault;
    bool payload;
};
static struct kosdec_adx_input_result provider(void *ctx,uint32_t offset,uint8_t *dst,size_t capacity) {
    struct input *p=ctx;++p->calls;
    CHECK(offset<=p->bytes && capacity<=p->bytes-offset);
    if(p->payload)CHECK(offset>=p->base && offset<=p->base+p->length && capacity<=p->base+p->length-offset);
    else CHECK(capacity<=8);
    if(p->fault) {
        switch(p->fault) {
        case 1:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        case 2:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
        case 3:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,0};
        case 4:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,capacity+1};
        case 5:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,1};
        default:return (struct kosdec_adx_input_result){(enum kosdec_adx_input_status)99,0};
        }
    }
    if(p->wait && p->calls%2==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=capacity;if(n>p->fragment)n=p->fragment;
    memcpy(dst,p->data+offset,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static enum kosdec_adx_status lookup(struct kosdec_adx_index *x) {
    for(unsigned i=0;i<64;++i) {
        enum kosdec_adx_status s=kosdec_adx_index_step(x);
        if(s>=KOSDEC_ADX_DONE)return s;
    }CHECK(0);return KOSDEC_ADX_INVALID;
}
static struct input container(enum kosdec_adx_container fmt,unsigned ch,unsigned v) {
    size_t n;uint8_t *audio=stream(50,18,ch,vectors[v].start,vectors[v].end,&n);
    struct input p={0};p.base=37;p.length=(uint32_t)n;p.bytes=p.base+p.length+19;p.fragment=512;
    p.data=malloc(p.bytes);if(!p.data)exit(2);memset(p.data,0xcd,p.bytes);
    memcpy(p.data,fmt==KOSDEC_ADX_AFS?"AFS\0":"\0\0\0\0",4);
    field(p.data+4,2,fmt);field(p.data+8,24,fmt);field(p.data+12,8,fmt);
    field(p.data+16,p.base,fmt);field(p.data+20,p.length,fmt);
    memcpy(p.data+p.base,audio,n);free(audio);return p;
}
static struct kosdec_adx_input_result sparse(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    unsigned *calls=ctx;++*calls;CHECK(cap<=8);
    if(off==0){memcpy(dst,"AFS\0",4);field(dst+4,(UINT32_MAX-8)/8,KOSDEC_ADX_AFS);CHECK(cap==8);}
    else if(off==UINT32_MAX-15){field(dst,UINT32_MAX-7,KOSDEC_ADX_AFS);field(dst+4,7,KOSDEC_ADX_AFS);CHECK(cap==8);}
    else{CHECK(off==UINT32_MAX-1 && cap==1);dst[0]=0x7b;}
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
int main(void) {
    const unsigned fragments[]={1,7,512},caps[]={1,32};
    for(unsigned fmt=0;fmt<2;++fmt)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned v=0;v<6;++v)for(unsigned f=0;f<3;++f)
    for(unsigned c=0;c<2;++c)for(unsigned wait=0;wait<2;++wait) {
        ++cases;struct input p=container((enum kosdec_adx_container)fmt,ch,v);
        p.fragment=fragments[f];p.wait=wait;struct kosdec_adx_index x;
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);
        CHECK(lookup(&x)==KOSDEC_ADX_DONE && x.count==2 && x.member.offset==p.base && x.member.bytes==p.length);
        unsigned calls=p.calls;kosdec_adx_index_cancel(&x);CHECK(kosdec_adx_index_step(&x)==KOSDEC_ADX_DONE && calls==p.calls);
        struct kosdec_adx_view view=x.member;p.payload=true;
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,kosdec_adx_view_read_at,&view,view.bytes,true);
        size_t at=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned i=0;i<5000;++i) {
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            int16_t pcm[66];for(unsigned j=0;j<66;++j)pcm[j]=0x5a5a;
            struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,caps[c]);
            for(size_t j=0;j<z.frames;++j)for(unsigned k=0;k<ch;++k) {
                CHECK(at+j<131);if(at+j<131)CHECK(pcm[j*ch+k]==vectors[v].pcm[k][at+j]);
            }
            for(size_t j=z.frames*ch;j<66;++j)CHECK(pcm[j]==0x5a5a);
            at+=z.frames;if(z.status>=KOSDEC_ADX_DONE){status=z.status;break;}
        }
        CHECK(status==KOSDEC_ADX_DONE && s.wraps==2 && at==65+2*(vectors[v].end-vectors[v].start));
        uint8_t b=0x12;calls=p.calls;
        CHECK(kosdec_adx_view_read_at(&view,view.bytes,&b,1).status==KOSDEC_ADX_INPUT_END && p.calls==calls && b==0x12);
        CHECK(kosdec_adx_view_read_at(&view,view.bytes+1,&b,1).status==KOSDEC_ADX_INPUT_ERROR && p.calls==calls);
        CHECK(kosdec_adx_view_read_at(&view,0,NULL,1).status==KOSDEC_ADX_INPUT_ERROR && p.calls==calls);
        CHECK(kosdec_adx_view_read_at(&view,0,&b,0).status==KOSDEC_ADX_INPUT_ERROR && p.calls==calls);
        free(p.data);
    }
    for(unsigned fmt=0;fmt<2;++fmt) {
        struct input p=container((enum kosdec_adx_container)fmt,2,4);struct kosdec_adx_index x;
        for(uint32_t n=0;n<8;++n) {
            unsigned before=p.calls;kosdec_adx_index_init(&x,provider,&p,n,(enum kosdec_adx_container)fmt,0);
            CHECK(lookup(&x)==KOSDEC_ADX_TRUNCATED && p.calls==before);
        }
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,2);
        CHECK(lookup(&x)==KOSDEC_ADX_NOT_FOUND && !x.member.magic);
        field(p.data+4,0,(enum kosdec_adx_container)fmt);
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,0);CHECK(lookup(&x)==KOSDEC_ADX_NOT_FOUND);
        field(p.data+4,UINT32_MAX,(enum kosdec_adx_container)fmt);
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,0);CHECK(lookup(&x)==KOSDEC_ADX_INVALID);
        field(p.data+4,2,(enum kosdec_adx_container)fmt);
        const uint32_t offsets[]={0,23,UINT32_MAX,p.bytes,p.bytes-1};
        for(unsigned i=0;i<5;++i) {
            field(p.data+16,offsets[i],(enum kosdec_adx_container)fmt);
            kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);
            CHECK(lookup(&x)==KOSDEC_ADX_INVALID && !x.member.magic);
        }
        field(p.data+16,p.base,(enum kosdec_adx_container)fmt);field(p.data+20,UINT32_MAX,(enum kosdec_adx_container)fmt);
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);CHECK(lookup(&x)==KOSDEC_ADX_INVALID);
        field(p.data+20,0,(enum kosdec_adx_container)fmt);
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);CHECK(lookup(&x)==KOSDEC_ADX_UNSUPPORTED);
        field(p.data+20,p.length,(enum kosdec_adx_container)fmt);
        p.data[3]=1;kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);CHECK(lookup(&x)==KOSDEC_ADX_UNSUPPORTED);p.data[3]=0;
        if(fmt==KOSDEC_ADX_AFS){p.data[0]='X';kosdec_adx_index_init(&x,provider,&p,p.bytes,KOSDEC_ADX_AFS,1);CHECK(lookup(&x)==KOSDEC_ADX_INVALID);p.data[0]='A';}
        for(unsigned fault=1;fault<=6;++fault) {
            p.fault=fault;kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);
            CHECK(lookup(&x)==(fault==1?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_IO_ERROR));
            unsigned before=p.calls;enum kosdec_adx_status term=x.terminal;CHECK(lookup(&x)==term && p.calls==before);
        }
        p.fault=0;p.fragment=1;kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);
        CHECK(kosdec_adx_index_step(&x)==KOSDEC_ADX_MORE);unsigned before=p.calls;
        kosdec_adx_index_cancel(&x);CHECK(lookup(&x)==KOSDEC_ADX_CANCELLED && before==p.calls);
        kosdec_adx_index_init(&x,provider,&p,p.bytes,(enum kosdec_adx_container)fmt,1);CHECK(lookup(&x)==KOSDEC_ADX_DONE);
        p.payload=true;uint8_t dst[512];for(unsigned fault=1;fault<=6;++fault) {
            p.fault=fault;CHECK(kosdec_adx_view_read_at(&x.member,0,dst,sizeof(dst)).status==(fault==1?KOSDEC_ADX_INPUT_END:KOSDEC_ADX_INPUT_ERROR));
        }
        free(p.data);
    }
    struct kosdec_adx_index x;unsigned calls=0;
    kosdec_adx_index_init(&x,sparse,&calls,UINT32_MAX,KOSDEC_ADX_AFS,(UINT32_MAX-8)/8-1);
    CHECK(lookup(&x)==KOSDEC_ADX_DONE && calls==2);uint8_t b;
    CHECK(kosdec_adx_view_read_at(&x.member,6,&b,512).bytes==1 && b==0x7b && calls==3);
    CHECK(kosdec_adx_view_read_at(&x.member,7,&b,512).status==KOSDEC_ADX_INPUT_END && calls==3);
    kosdec_adx_index_init(&x,NULL,NULL,8,KOSDEC_ADX_ACX,0);CHECK(lookup(&x)==KOSDEC_ADX_BAD_ARGUMENT);
    kosdec_adx_index_init(&x,sparse,&calls,8,(enum kosdec_adx_container)99,0);CHECK(lookup(&x)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_index_step(NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_view_read_at(NULL,0,&b,1).status==KOSDEC_ADX_INPUT_ERROR);
    printf("ADX_INDEX cases=%u failures=%u\n",cases,failures);return failures?1:0;
}
