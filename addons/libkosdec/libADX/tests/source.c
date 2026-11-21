#include <kosdec/adx_source.h>
#include <kosdec/adx_joint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"source:%u: %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
struct provider { const uint8_t *data; uint32_t bytes; unsigned calls, fragment; bool wait; };
static struct kosdec_adx_input_result read_at(void *context,uint32_t offset,uint8_t *out,size_t cap) {
    struct provider *p=context;
    ++p->calls;
    CHECK(offset<=p->bytes && cap<=p->bytes-offset && cap<=512);
    if(p->wait && p->calls%2) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment) cap=p->fragment;
    memcpy(out,p->data+offset,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void field(uint8_t *p,uint32_t n,bool afs) {
    if(!afs) put32(p,n);
    else for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i));
}
static void test(bool afs,bool resident,unsigned fragment) {
    size_t bytes;
    uint8_t *audio=stream(50,18,2,32,65,&bytes);
    memset(audio+20,0,24);
    uint32_t size=(uint32_t)bytes+41;
    uint8_t *allocation=calloc(1,size+1),*archive=allocation+1;
    if(afs) memcpy(archive,"AFS\0",4);
    field(archive+4,1,afs);field(archive+8,29,afs);field(archive+12,(uint32_t)bytes,afs);
    memcpy(archive+29,audio,bytes);
    struct provider p={archive,size,0,fragment,true};
    struct kosdec_adx_source_handle source;
    CHECK((resident?kosdec_adx_source_init_memory(&source,archive,size):
        kosdec_adx_source_init(&source,read_at,&p,size))==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_select_range(&source,size+1,0)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_source_index_begin(&source,afs?KOSDEC_ADX_AFS:KOSDEC_ADX_ACX,0)==KOSDEC_ADX_MORE);
    struct kosdec_adx_source loan={0};
    CHECK(kosdec_adx_source_borrow(&source,&loan,7)==KOSDEC_ADX_NEED_OUTPUT);
    enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<100 && status<KOSDEC_ADX_DONE;++i) {
        unsigned before=p.calls;
        status=kosdec_adx_source_index_step(&source);
        CHECK(p.calls-before<=1);
    }
    CHECK(status==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_borrow(&source,&loan,7)==KOSDEC_ADX_DONE);
    CHECK(loan.bytes==bytes && loan.tag==7);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(kosdec_adx_source_select_range(&source,0,size)==KOSDEC_ADX_NEED_OUTPUT);
    struct kosdec_adx_source copied=loan;
    CHECK(kosdec_adx_source_return(&source,&copied)==KOSDEC_ADX_BAD_ARGUMENT);
    struct kosdec_adx_joint joint = {.capacity=91};
    CHECK(kosdec_adx_source_init_memory_joint(&source,&copied,&joint)==KOSDEC_ADX_BAD_ARGUMENT &&
          joint.capacity==91);
    if(resident) {
        CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&joint)==KOSDEC_ADX_DONE);
        struct kosdec_adx_joint_info info;
        CHECK(kosdec_adx_joint_query(&joint,&info)==KOSDEC_ADX_JOINT_OK &&
              info.kind==KOSDEC_ADX_JOINT_MEMORY && info.capacity==bytes && info.sealed);
        struct kosdec_adx_chunk data;
        CHECK(kosdec_adx_joint_get(&joint,KOSDEC_ADX_JOINT_DATA,bytes+1,&data)==KOSDEC_ADX_JOINT_OK &&
              data.bytes==bytes && data.data==archive+29);
        CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_BUSY);
        CHECK(kosdec_adx_joint_put(&joint,&data)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
    }
    else CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&joint)==KOSDEC_ADX_UNSUPPORTED &&
               joint.capacity==91);
    struct kosdec_adx_stream decoder;
    kosdec_adx_stream_init(&decoder,loan.read_at,loan.context,loan.bytes,false);
    unsigned frames=0;
    int16_t output[64];
    for(unsigned i=0;i<5000;++i) {
        struct kosdec_adx_stream_result got=kosdec_adx_stream_read(&decoder,output,32);
        frames+=(unsigned)got.frames;
        if(got.status>=KOSDEC_ADX_DONE) { CHECK(got.status==KOSDEC_ADX_DONE);break; }
    }
    CHECK(frames==65);
    uint8_t guard=0xa5;
    CHECK(loan.read_at(loan.context,loan.bytes,&guard,1).status==KOSDEC_ADX_INPUT_END && guard==0xa5);
    CHECK(kosdec_adx_source_return(&source,&loan)==KOSDEC_ADX_DONE && !loan.read_at);
    CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&joint)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(copied.read_at(copied.context,0,&guard,1).status==KOSDEC_ADX_INPUT_ERROR);
    CHECK(kosdec_adx_source_index_begin(&source,afs?KOSDEC_ADX_AFS:KOSDEC_ADX_ACX,1)==KOSDEC_ADX_MORE);
    do { status=kosdec_adx_source_index_step(&source); } while(status<KOSDEC_ADX_DONE);
    CHECK(status==KOSDEC_ADX_NOT_FOUND);
    CHECK(kosdec_adx_source_borrow(&source,&loan,0)==KOSDEC_ADX_DONE && loan.bytes==bytes);
    CHECK(kosdec_adx_source_return(&source,&loan)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_index_begin(&source,afs?KOSDEC_ADX_AFS:KOSDEC_ADX_ACX,0)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_source_index_cancel(&source)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_select_range(&source,size,0)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_borrow(&source,&loan,0)==KOSDEC_ADX_DONE && !loan.bytes);
    CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&joint)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(loan.read_at(loan.context,0,&guard,1).status==KOSDEC_ADX_INPUT_END);
    CHECK(kosdec_adx_source_return(&source,&loan)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_borrow(&source,&loan,0)==KOSDEC_ADX_BAD_ARGUMENT);
    free(allocation);free(audio);
}
int main(void) {
    for(unsigned afs=0;afs<2;++afs) for(unsigned resident=0;resident<2;++resident)
        for(unsigned fragment=1;fragment<=17;++fragment) test(afs,resident,fragment);
    CHECK(kosdec_adx_source_init_memory(NULL,NULL,0)==KOSDEC_ADX_BAD_ARGUMENT);
    struct kosdec_adx_source_handle source;
    CHECK(kosdec_adx_source_init_memory(&source,NULL,1)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_source_init_memory(&source,NULL,0)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_DONE);
    printf("ADX_SOURCE checks=%u failures=0\n",checks);
    return 0;
}
