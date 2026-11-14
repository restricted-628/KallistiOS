#include <kosdec/adx_request.h>
#include <kosdec/adx_joint_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#define STOP KOSDEC_ADX_REQUEST_STOPPED
#define READ KOSDEC_ADX_REQUEST_READING
#define END KOSDEC_ADX_REQUEST_READEND
#define WAIT KOSDEC_ADX_REQUEST_WAIT
#define SPACE KOSDEC_ADX_REQUEST_NEED_OUTPUT
#define FREE KOSDEC_ADX_JOINT_FREE
#define DATA KOSDEC_ADX_JOINT_DATA
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"request_joint:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
struct input {uint32_t bytes;size_t fragment;unsigned calls,fail;const uint8_t *data;};
static uint8_t value(uint32_t n) {return (uint8_t)(n*17u+(n>>9));}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;++in->calls;CHECK(cap && cap<=512 && off<in->bytes && cap<=in->bytes-off);
    if(in->calls==in->fail)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(in->calls%5==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    size_t n=cap;if(n>in->fragment)n=in->fragment;
    for(size_t i=0;i<n;++i)dst[i]=in->data?in->data[off+i]:value(off+(uint32_t)i);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static struct kosdec_adx_request_info info(struct kosdec_adx_request *r) {
    struct kosdec_adx_request_info s;CHECK(kosdec_adx_request_get_info(r,&s));return s;
}
static void init_joint(struct kosdec_adx_joint *j,unsigned kind,uint8_t *buf,size_t size,
                       struct kosdec_adx_joint_node *nodes,size_t count) {
    if(!kind)OK(kosdec_adx_joint_ring_init(j,buf,size,size));
    else {OK(kosdec_adx_joint_queue_init(j,nodes,count,kind==1?KOSDEC_ADX_JOINT_JOIN:KOSDEC_ADX_JOINT_SEPARATE));OK(kosdec_adx_joint_seed(j,FREE,buf,size));}
}
static void matrix(void) {
    size_t capacities[]={1,7,511,512,2048,4096},fragments[]={1,7,512};
    uint32_t lengths[]={1,511,2047,2048,2049,12345};
    for(unsigned kind=0;kind<3;++kind)for(unsigned ci=0;ci<6;++ci)
    for(unsigned fi=0;fi<3;++fi)for(unsigned li=0;li<6;++li) {
        size_t cap=capacities[ci];uint8_t *buf=malloc(cap*2+2);CHECK(buf);memset(buf,0xa5,cap*2+2);
        struct kosdec_adx_joint j;struct kosdec_adx_joint_node nodes[8];init_joint(&j,kind,buf+1,cap,nodes,8);
        struct input in={.bytes=lengths[li],.fragment=fragments[fi]};struct kosdec_adx_request r;
        CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
        CHECK(kosdec_adx_request_start_joint(&r,UINT32_MAX,&j)==READ);
        size_t received=0;bool ended=false;
        for(unsigned step=0;step<100000;++step) {
            unsigned calls=in.calls;enum kosdec_adx_request_status s=kosdec_adx_request_step(&r);
            CHECK(in.calls-calls<=1);CHECK(s==READ || s==END || s==WAIT || s==SPACE);
            if(s==END)ended=true;
            if(step%3 || ended || s==SPACE) {
                struct kosdec_adx_chunk c;OK(kosdec_adx_joint_get(&j,DATA,kind?SIZE_MAX:13,&c));
                for(size_t i=0;i<c.bytes;++i)CHECK(received+i<in.bytes && c.data[i]==value((uint32_t)(received+i)));
                received+=c.bytes;OK(kosdec_adx_joint_put(&j,&c));
            }
            if(ended && received==in.bytes)break;
            CHECK(step<99999);
        }
        struct kosdec_adx_request_info st=info(&r);CHECK(st.output_bytes==in.bytes && st.source_bytes==in.bytes);
        CHECK(st.completed==(in.bytes+2047)/2048 && st.cursor==st.completed && !r.held.bytes && !r.joint);
        struct kosdec_adx_joint_info ji;OK(kosdec_adx_joint_query(&j,&ji));CHECK(!ji.sealed && ji.available[FREE]==cap && !ji.borrowed[FREE]);
        CHECK(buf[0]==0xa5 && buf[cap*2+1]==0xa5);
        CHECK(kosdec_adx_request_close(&r)==STOP);OK(kosdec_adx_joint_close(&j));free(buf);
    }
}
static void retirement(void) {
    for(unsigned kind=0;kind<3;++kind)for(unsigned how=0;how<4;++how) {
        uint8_t buf[8192];struct kosdec_adx_joint j;struct kosdec_adx_joint_node nodes[4];init_joint(&j,kind,buf,4096,nodes,4);
        struct input in={.bytes=5000,.fragment=7};struct kosdec_adx_request r;
        CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
        CHECK(kosdec_adx_request_start_joint(&r,3,&j)==READ);
        CHECK(kosdec_adx_request_step(&r)==READ && r.held.bytes && r.held_used==7);
        CHECK(kosdec_adx_joint_close(&j)==KOSDEC_ADX_JOINT_BUSY);
        unsigned calls=in.calls;
        if(how==0)CHECK(kosdec_adx_request_stop(&r)==STOP);
        if(how==1)CHECK(kosdec_adx_request_seek(&r,1,KOSDEC_ADX_REQUEST_SET)==STOP);
        if(how==2)CHECK(kosdec_adx_request_close(&r)==STOP);
        if(how==3){in.fail=in.calls+1;CHECK(kosdec_adx_request_step(&r)==KOSDEC_ADX_REQUEST_IO_ERROR);++calls;}
        CHECK(!r.held.bytes && in.calls==calls);
        struct kosdec_adx_joint_info ji;OK(kosdec_adx_joint_query(&j,&ji));CHECK(ji.available[FREE]==4096 && !ji.available[DATA] && !ji.borrowed[FREE]);
        if(how!=2){CHECK(info(&r).output_bytes==0 && info(&r).source_bytes==7);CHECK(kosdec_adx_request_close(&r)==STOP);}
        OK(kosdec_adx_joint_close(&j));
    }
    /* One SEPARATE node cannot split a short final head: hold it without further
       reads, then stop must return it even with no spare node. */
    uint8_t buf[2000];struct kosdec_adx_joint j;struct kosdec_adx_joint_node node;
    init_joint(&j,2,buf,2000,&node,1);struct input in={.bytes=1000,.fragment=512};struct kosdec_adx_request r;
    CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
    CHECK(kosdec_adx_request_start_joint(&r,1,&j)==READ);
    CHECK(kosdec_adx_request_step(&r)==READ);CHECK(kosdec_adx_request_step(&r)==SPACE);
    CHECK(info(&r).source_bytes==1000 && !info(&r).output_bytes && r.held.bytes==2000);
    unsigned calls=in.calls;for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_request_step(&r)==SPACE && in.calls==calls);
    CHECK(kosdec_adx_request_stop(&r)==STOP && !r.held.bytes);OK(kosdec_adx_joint_close(&j));CHECK(kosdec_adx_request_close(&r)==STOP);
}
static void admission(void) {
    uint8_t buf[4096];struct kosdec_adx_joint j;struct kosdec_adx_request r;
    struct input in={.bytes=4096,.fragment=512};CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
    OK(kosdec_adx_joint_memory_init(&j,buf,2048));struct kosdec_adx_request before=r;
    CHECK(kosdec_adx_request_start_joint(&r,1,&j)==KOSDEC_ADX_REQUEST_BAD_ARGUMENT && !memcmp(&r,&before,sizeof(r)));
    OK(kosdec_adx_joint_close(&j));OK(kosdec_adx_joint_ring_init(&j,buf,2048,2048));
    struct kosdec_adx_chunk c;OK(kosdec_adx_joint_get(&j,FREE,1,&c));
    CHECK(kosdec_adx_request_start_joint(&r,1,&j)==KOSDEC_ADX_REQUEST_BUSY && !memcmp(&r,&before,sizeof(r)));OK(kosdec_adx_joint_unget(&j,&c));
    OK(kosdec_adx_joint_finish(&j));CHECK(kosdec_adx_request_start_joint(&r,1,&j)==KOSDEC_ADX_REQUEST_BAD_ARGUMENT);
    OK(kosdec_adx_joint_reset(&j));CHECK(kosdec_adx_request_start_joint(&r,0,&j)==END && !in.calls);
    CHECK(kosdec_adx_request_start_joint(&r,2,&j)==READ);
    enum kosdec_adx_request_status s;do{s=kosdec_adx_request_step(&r);}while(s==READ || s==WAIT);
    CHECK(s==SPACE && info(&r).output_bytes==2048);unsigned calls=in.calls;
    CHECK(kosdec_adx_request_step(&r)==SPACE && in.calls==calls);
    CHECK(kosdec_adx_request_stop(&r)==STOP);struct kosdec_adx_joint_info ji;OK(kosdec_adx_joint_query(&j,&ji));CHECK(ji.available[DATA]==2048);
    CHECK(kosdec_adx_request_close(&r)==STOP);OK(kosdec_adx_joint_close(&j));
}
static void playback(void) {
    for(unsigned kind=0;kind<3;++kind)for(unsigned ch=1;ch<=2;++ch) {
        size_t bytes;uint8_t *data=stream(50,18,ch,32,65,&bytes),buf[134];
        struct kosdec_adx_joint j;struct kosdec_adx_joint_node nodes[8];init_joint(&j,kind,buf,67,nodes,8);
        struct input in={.bytes=(uint32_t)bytes,.fragment=3,.data=data};struct kosdec_adx_request r;
        CHECK(kosdec_adx_request_init(&r,source,&in,in.bytes)==STOP);
        CHECK(kosdec_adx_request_start_joint(&r,1,&j)==READ);
        struct kosdec_adx_joint_reader reader;OK(kosdec_adx_joint_reader_init(&reader,&j));
        unsigned frames=0;bool done=false;
        for(unsigned i=0;i<10000;++i) {
            enum kosdec_adx_request_status s=kosdec_adx_request_step(&r);CHECK(s==READ || s==WAIT || s==SPACE || s==END);
            if(s==END)OK(kosdec_adx_joint_finish(&j));
            int16_t pcm[64];struct kosdec_adx_joint_read_result got=kosdec_adx_joint_reader_read(&reader,pcm,32);
            CHECK(got.joint_status==KOSDEC_ADX_JOINT_OK);
            for(size_t f=0;f<got.frames;++f)for(unsigned c=0;c<ch;++c)CHECK(frames+f<65 && pcm[f*ch+c]==golden[c][frames+f]);
            frames+=(unsigned)got.frames;
            if(got.status==KOSDEC_ADX_DONE){done=true;break;}
            CHECK(got.status<KOSDEC_ADX_DONE);
        }
        CHECK(done && frames==65 && info(&r).output_bytes==bytes);
        OK(kosdec_adx_joint_reader_close(&reader));CHECK(kosdec_adx_request_close(&r)==STOP);OK(kosdec_adx_joint_close(&j));free(data);
    }
}
int main(void) {matrix();retirement();admission();playback();printf("joint requests: %u checks passed\n",checks);return 0;}
