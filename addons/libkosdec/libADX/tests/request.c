#include <kosdec/adx_request.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STOP KOSDEC_ADX_REQUEST_STOPPED
#define READ KOSDEC_ADX_REQUEST_READING
#define END KOSDEC_ADX_REQUEST_READEND
#define WAIT KOSDEC_ADX_REQUEST_WAIT
#define BAD KOSDEC_ADX_REQUEST_BAD_ARGUMENT
#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"request:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct input {uint32_t bytes;size_t fragment;unsigned calls,wait_every,bad_call,mode;};
static uint8_t value(uint32_t off) {return (uint8_t)(off*17u+(off>>9));}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;++in->calls;CHECK(cap && cap<=512 && off<in->bytes);
    if(in->calls==in->bad_call) {
        switch(in->mode) {
        case 0:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
        case 1:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        case 2:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,1};
        case 3:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,0};
        case 4:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap+1};
        default:return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,1};
        }
    }
    if(in->wait_every && in->calls%in->wait_every==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=in->bytes-off;if(n>cap)n=cap;if(n>in->fragment)n=in->fragment;
    for(size_t i=0;i<n;++i)dst[i]=value(off+(uint32_t)i);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static struct kosdec_adx_request_info info(const struct kosdec_adx_request *r) {
    struct kosdec_adx_request_info out;CHECK(kosdec_adx_request_get_info(r,&out));return out;
}
static void matrix(void) {
    uint32_t sizes[]={0,1,511,512,513,2047,2048,2049,4095,4096,65537};
    size_t fragments[]={1,3,511,512};uint32_t asks[]={0,1,2,UINT32_MAX};
    for(unsigned si=0;si<sizeof(sizes)/sizeof(*sizes);++si)for(unsigned fi=0;fi<4;++fi)
    for(unsigned ai=0;ai<4;++ai)for(unsigned start=0;start<4;++start) {
        struct input in={.bytes=sizes[si],.fragment=fragments[fi],.wait_every=3};
        struct kosdec_adx_request r;CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
        uint32_t total=(in.bytes+2047)/2048,at=start<total?start:total;
        CHECK(kosdec_adx_request_seek(&r,start,KOSDEC_ADX_REQUEST_SET)==STOP);
        uint32_t count=asks[ai]<total-at?asks[ai]:total-at;size_t bytes=(size_t)count*SECTOR;
        uint8_t *buf=malloc(bytes+2);CHECK(buf);memset(buf,0xa5,bytes+2);
        CHECK(kosdec_adx_request_start(&r,asks[ai],buf+1,bytes)==(count?READ:END));
        CHECK(info(&r).requested==count && info(&r).cursor==at);
        enum kosdec_adx_request_status s;unsigned iterations=0;
        do {
            struct kosdec_adx_request_info before=info(&r);unsigned calls=in.calls;
            s=kosdec_adx_request_step(&r);struct kosdec_adx_request_info after=info(&r);
            CHECK(in.calls-calls<=1 && after.output_bytes-before.output_bytes<=512);
            CHECK(after.completed==after.output_bytes/SECTOR);
            CHECK(after.cursor==(s==END?at+count:at));
            if(s==WAIT)CHECK(after.output_bytes==before.output_bytes && after.state==READ);
            CHECK(++iterations<200000);
        }while(s==READ || s==WAIT);
        CHECK(s==END && !r.output);
        CHECK(buf[0]==0xa5 && buf[bytes+1]==0xa5);
        for(size_t i=0;i<bytes;++i) {
            uint64_t off=(uint64_t)at*SECTOR+i;
            CHECK(buf[i+1]==(off<in.bytes?value((uint32_t)off):0));
        }
        unsigned expected=at*SECTOR<in.bytes?in.bytes-at*SECTOR:0;if(expected>bytes)expected=(unsigned)bytes;
        CHECK(info(&r).source_bytes==expected);
        unsigned calls=in.calls;CHECK(kosdec_adx_request_step(&r)==END && in.calls==calls);
        CHECK(kosdec_adx_request_close(&r)==STOP);CHECK(kosdec_adx_request_step(&r)==BAD);free(buf);
    }
}
static void transitions(void) {
    struct input in={.bytes=8193,.fragment=512};struct kosdec_adx_request r;
    uint8_t out[8192];CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
    CHECK(kosdec_adx_request_start(&r,3,out,sizeof(out))==READ);
    for(unsigned i=0;i<5;++i)CHECK(kosdec_adx_request_step(&r)==READ);
    CHECK(info(&r).completed==1 && info(&r).cursor==0);
    struct kosdec_adx_request before=r;
    CHECK(kosdec_adx_request_start(&r,1,out,sizeof(out))==KOSDEC_ADX_REQUEST_BUSY && !memcmp(&r,&before,sizeof(r)));
    CHECK(kosdec_adx_request_seek(&r,0,(enum kosdec_adx_request_origin)99)==BAD && !memcmp(&r,&before,sizeof(r)));
    CHECK(kosdec_adx_request_stop(&r)==STOP && !r.output && info(&r).cursor==0 && info(&r).completed==1);
    unsigned calls=in.calls;CHECK(kosdec_adx_request_step(&r)==STOP && in.calls==calls);
    CHECK(kosdec_adx_request_start(&r,1,out,sizeof(out))==READ);
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_request_step(&r)==(i==3?END:READ));
    CHECK(info(&r).cursor==1 && info(&r).source_bytes==2048);
    CHECK(kosdec_adx_request_start(&r,3,out,sizeof(out))==READ);
    CHECK(kosdec_adx_request_step(&r)==READ);
    CHECK(kosdec_adx_request_seek(&r,1,KOSDEC_ADX_REQUEST_CUR)==STOP && info(&r).cursor==2 && !r.output);
    CHECK(kosdec_adx_request_seek(&r,-1,KOSDEC_ADX_REQUEST_END)==STOP && info(&r).cursor==4);
    for(unsigned origin=0;origin<3;++origin) {
        CHECK(kosdec_adx_request_seek(&r,INT64_MIN,(enum kosdec_adx_request_origin)origin)==STOP && info(&r).cursor==0);
        CHECK(kosdec_adx_request_seek(&r,INT64_MAX,(enum kosdec_adx_request_origin)origin)==STOP && info(&r).cursor==5);
    }
    CHECK(kosdec_adx_request_start(&r,UINT32_MAX,NULL,0)==END && in.calls==calls+5);
    CHECK(kosdec_adx_request_seek(&r,0,KOSDEC_ADX_REQUEST_SET)==STOP);
    before=r;CHECK(kosdec_adx_request_start(&r,1,out,2047)==BAD && !memcmp(&r,&before,sizeof(r)));
    CHECK(kosdec_adx_request_start(&r,1,NULL,2048)==BAD && !memcmp(&r,&before,sizeof(r)));
    CHECK(kosdec_adx_request_start(&r,1,out,2048)==READ);
    calls=in.calls;CHECK(kosdec_adx_request_close(&r)==STOP && in.calls==calls);
    CHECK(kosdec_adx_request_init(&r,NULL,&in,1)==BAD);
    CHECK(kosdec_adx_request_init(&r,source,&in,0)==STOP);
    CHECK(kosdec_adx_request_start(&r,1,NULL,0)==END && in.calls==calls);
    CHECK(kosdec_adx_request_close(&r)==STOP);
}
static void errors(void) {
    for(unsigned mode=0;mode<6;++mode)for(unsigned call=1;call<=12;++call) {
        struct input in={.bytes=8192,.fragment=511,.bad_call=call,.mode=mode};struct kosdec_adx_request r;
        uint8_t buf[8194];memset(buf,0xa5,sizeof(buf));
        CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
        CHECK(kosdec_adx_request_start(&r,4,buf+1,8192)==READ);
        for(unsigned i=1;i<call;++i)CHECK(kosdec_adx_request_step(&r)==READ);
        enum kosdec_adx_request_status expected=mode==1?KOSDEC_ADX_REQUEST_TRUNCATED:KOSDEC_ADX_REQUEST_IO_ERROR;
        CHECK(kosdec_adx_request_step(&r)==expected && !r.output);
        struct kosdec_adx_request_info st=info(&r);
        CHECK(st.source_bytes==(call-1)*511 && st.completed==st.source_bytes/2048 && st.cursor==st.completed);
        CHECK(buf[0]==0xa5 && buf[8193]==0xa5);
        CHECK(kosdec_adx_request_step(&r)==expected && in.calls==call);
        CHECK(kosdec_adx_request_start(&r,1,buf+1,2048)==READ);
        enum kosdec_adx_request_status s;do{s=kosdec_adx_request_step(&r);}while(s==READ);
        CHECK(s==END && info(&r).cursor==st.cursor+1);
        CHECK(kosdec_adx_request_close(&r)==STOP);
    }
}
static void huge_and_independent(void) {
    struct input in[2]={{.bytes=UINT32_MAX,.fragment=512},{.bytes=4096,.fragment=3}};
    struct kosdec_adx_request r[2];uint8_t out[2][2048];
    for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_request_init(r+i,source,in+i,in[i].bytes)==STOP);
    CHECK(info(r).sectors==2097152);
    CHECK(kosdec_adx_request_seek(r,-1,KOSDEC_ADX_REQUEST_END)==STOP);
    for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_request_start(r+i,UINT32_MAX,out[i],2048)==(i?BAD:READ));
    CHECK(kosdec_adx_request_start(r+1,1,out[1],2048)==READ);
    for(unsigned i=0;i<1000;++i) {
        enum kosdec_adx_request_status a=kosdec_adx_request_step(r),b=kosdec_adx_request_step(r+1);
        if(a==END && b==END)break;CHECK(i<999);
    }
    CHECK(info(r).source_bytes==2047 && out[0][2047]==0 && info(r).cursor==2097152);
    for(unsigned i=0;i<2047;++i)CHECK(out[0][i]==value(UINT32_MAX-2047+i));
    CHECK(info(r+1).source_bytes==2048 && info(r+1).cursor==1);
    for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_request_close(r+i)==STOP);
}
int main(void) {matrix();transitions();errors();huge_and_independent();printf("sector requests: %u checks passed\n",checks);return 0;}
