#include <kosdec/adx_joint_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
static unsigned checks, cases, filtered;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"joint_reader:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
#define DATA KOSDEC_ADX_JOINT_DATA
#define FREE KOSDEC_ADX_JOINT_FREE
static void filter(void *context,unsigned channel,int16_t *pcm,size_t frames,unsigned stride) {
    CHECK(context==&filtered && channel<stride);
    for(size_t i=0;i<frames;++i) pcm[i*stride]=(int16_t)-pcm[i*stride];
    filtered+=(unsigned)frames;
}
static void playback(unsigned kind,unsigned ch,unsigned header,unsigned tail,
                     unsigned fragment,int effect) {
    size_t size;
    uint8_t *data=stream(header,tail,ch,32,65,&size),ring[100];
    struct kosdec_adx_joint j;
    struct kosdec_adx_joint_node nodes[8];
    if(kind==0) OK(kosdec_adx_joint_memory_init(&j,data,size));
    else if(kind==1) OK(kosdec_adx_joint_ring_init(&j,ring,67,33));
    else OK(kosdec_adx_joint_queue_init(&j,nodes,8,
        kind==2 ? KOSDEC_ADX_JOINT_JOIN : KOSDEC_ADX_JOINT_SEPARATE));
    struct kosdec_adx_joint_reader reader;
    OK(kosdec_adx_joint_reader_init(&reader,&j));
    filtered=0;
    if(effect) CHECK(kosdec_adx_joint_reader_set_filter(&reader,filter,&filtered)==KOSDEC_ADX_DONE);
    size_t supplied=kind==0 ? size : 0, frames=0;
    int end_wait=0,done=0;
    for(unsigned step=0;step<20000;++step) {
        struct kosdec_adx_joint_info info;
        OK(kosdec_adx_joint_query(&j,&info));
        if(kind>=2 && !info.sealed) {
            struct kosdec_adx_chunk free;
            OK(kosdec_adx_joint_get(&j,FREE,SIZE_MAX,&free));
            OK(kosdec_adx_joint_detach(&j,&free));
        }
        if(supplied<size && step%5) {
            size_t n=size-supplied; if(n>fragment)n=fragment;
            if(kind==1) {
                struct kosdec_adx_chunk c;
                OK(kosdec_adx_joint_get(&j,FREE,n,&c));
                if(c.bytes) memcpy(c.data,data+supplied,c.bytes);
                OK(kosdec_adx_joint_put(&j,&c)); supplied+=c.bytes;
            } else {
                enum kosdec_adx_joint_status status=kosdec_adx_joint_seed(&j,DATA,data+supplied,n);
                CHECK(status==KOSDEC_ADX_JOINT_OK || status==KOSDEC_ADX_JOINT_LIMIT);
                if(status==KOSDEC_ADX_JOINT_OK) supplied+=n;
            }
        }
        int16_t pcm[64];
        size_t capacity=step%9==0 ? 0 : step%7==0 ? 1 : 32;
        uint64_t before=reader.consumed_bytes;
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,capacity);
        CHECK(r.joint_status==KOSDEC_ADX_JOINT_OK);
        CHECK(reader.consumed_bytes-before<=512 && r.frames<=32);
        if(!capacity) CHECK(reader.consumed_bytes==before);
        for(size_t f=0;f<r.frames;++f)for(unsigned c=0;c<ch;++c) {
            CHECK(frames+f<65);
            int expected=golden[c][frames+f]; if(effect)expected=-expected;
            CHECK(pcm[f*ch+c]==expected);
        }
        frames+=r.frames;
        if(r.status==KOSDEC_ADX_NEED_END) {
            CHECK(supplied==size && frames==65); ++end_wait;
            /* Repeat starvation once to show empty is not an automatic EOF. */
            if(end_wait==2) OK(kosdec_adx_joint_finish(&j));
        }
        if(r.status==KOSDEC_ADX_DONE) {done=1;break;}
        CHECK(r.status<KOSDEC_ADX_DONE);
    }
    CHECK(done && end_wait>=2 && frames==65 && reader.consumed_bytes==size);
    CHECK(filtered==(effect ? 65*ch : 0));
    CHECK(kosdec_adx_joint_reader_set_filter(&reader,NULL,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    OK(kosdec_adx_joint_reader_close(&reader)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void endings(unsigned kind,unsigned fault) {
    size_t size; uint8_t *data=stream(fault ? 50 : 1042,fault ? 18 : 1042,2,32,65,&size);
    enum kosdec_adx_status expected=KOSDEC_ADX_DONE;
    if(fault==1) {--size;expected=KOSDEC_ADX_TRUNCATED;}
    if(fault==2) {size=19;expected=KOSDEC_ADX_TRUNCATED;}
    if(fault==3) {++size;expected=KOSDEC_ADX_INVALID;}
    if(fault==4) {data[50]|=0x80;expected=KOSDEC_ADX_INVALID;}
    struct kosdec_adx_joint j; struct kosdec_adx_joint_node node;
    if(kind==0) OK(kosdec_adx_joint_memory_init(&j,data,size));
    else {
        /* A sealed full pool cannot depend on a spare node to decode. */
        OK(kosdec_adx_joint_queue_init(&j,&node,1,
            kind==1 ? KOSDEC_ADX_JOINT_JOIN : KOSDEC_ADX_JOINT_SEPARATE));
        OK(kosdec_adx_joint_seed(&j,DATA,data,size));
    }
    OK(kosdec_adx_joint_finish(&j)); OK(kosdec_adx_joint_finish(&j));
    struct kosdec_adx_joint_reader r; OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64]; enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<20 && status<KOSDEC_ADX_DONE;++i)
        status=kosdec_adx_joint_reader_read(&r,pcm,32).status;
    CHECK(status==expected);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==expected);
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void controls(void) {
    size_t size; uint8_t *data=stream(50,18,1,32,65,&size);
    struct kosdec_adx_joint j,before;
    struct kosdec_adx_joint_reader r,save;
    struct kosdec_adx_joint_node nodes[3];
    struct kosdec_adx_chunk c,a,b;
    struct kosdec_adx_joint_info info;
    OK(kosdec_adx_joint_memory_init(&j,data,size));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64];
    save=r;
    CHECK(kosdec_adx_joint_reader_read(&r,NULL,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&save,&r,sizeof(r)));
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_MORE);
    CHECK(r.held.bytes && r.used==50);
    CHECK(kosdec_adx_joint_reader_init(&save,&j)==KOSDEC_ADX_JOINT_BUSY);
    /* Inject a stale descriptor to verify a failed release does not erase the
       real borrow. Direct field changes are not a supported caller operation. */
    uint64_t token=r.held.token; ++r.held.token; save=r;
    CHECK(kosdec_adx_joint_reader_close(&r)==KOSDEC_ADX_JOINT_STALE);
    CHECK(!memcmp(&save,&r,sizeof(r)));
    r.held.token=token;
    kosdec_adx_joint_reader_cancel(&r);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_CANCELLED);
    CHECK(kosdec_adx_joint_reset(&j)==KOSDEC_ADX_JOINT_BUSY);
    OK(kosdec_adx_joint_reader_close(&r));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(!info.available[DATA]);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    OK(kosdec_adx_joint_reset(&j));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));

    uint8_t buffer[64];
    OK(kosdec_adx_joint_ring_init(&j,buffer,32,32));
    OK(kosdec_adx_joint_get(&j,FREE,1,&c)); before=j;
    CHECK(kosdec_adx_joint_finish(&j)==KOSDEC_ADX_JOINT_BUSY);
    CHECK(!memcmp(&before,&j,sizeof(j)));
    OK(kosdec_adx_joint_unget(&j,&c)); OK(kosdec_adx_joint_finish(&j));
    CHECK(kosdec_adx_joint_get(&j,FREE,1,&c)==KOSDEC_ADX_JOINT_SEALED);
    OK(kosdec_adx_joint_reset(&j)); OK(kosdec_adx_joint_close(&j));

    OK(kosdec_adx_joint_queue_init(&j,nodes,3,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,buffer,16));
    OK(kosdec_adx_joint_get(&j,DATA,16,&c));
    OK(kosdec_adx_chunk_split(&c,6,&a,&b));
    OK(kosdec_adx_joint_detach(&j,&a));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.capacity==10 && info.borrowed[DATA]==10);
    OK(kosdec_adx_joint_seed(&j,FREE,buffer,6)); /* Detached prefix is reusable. */
    OK(kosdec_adx_joint_finish(&j)); before=j;
    CHECK(kosdec_adx_joint_seed(&j,DATA,buffer+16,1)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(kosdec_adx_joint_move(&j,DATA,&b)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_AUX2,1,&a)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(!memcmp(&before,&j,sizeof(j)));
    OK(kosdec_adx_joint_unget(&j,&b));
    OK(kosdec_adx_joint_get(&j,DATA,10,&c)); OK(kosdec_adx_joint_detach(&j,&c));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.capacity==6 && info.spare_nodes==2);
    OK(kosdec_adx_joint_reset(&j)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void late_tail(void) {
    size_t size; uint8_t *data=stream(50,18,1,32,65,&size);
    struct kosdec_adx_joint j; struct kosdec_adx_joint_node nodes[2];
    struct kosdec_adx_joint_reader r;
    OK(kosdec_adx_joint_queue_init(&j,nodes,2,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,data,size));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64]; enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<20 && status!=KOSDEC_ADX_NEED_END;++i) {
        status=kosdec_adx_joint_reader_read(&r,pcm,32).status;
        CHECK(status<KOSDEC_ADX_DONE);
    }
    CHECK(status==KOSDEC_ADX_NEED_END);
    OK(kosdec_adx_joint_seed(&j,DATA,data+size,1));
    OK(kosdec_adx_joint_finish(&j));
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_INVALID);
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
int main(void) {
    const unsigned fragments[]={1,17,63,512,4096};
    controls(); late_tail();
    for(unsigned kind=0;kind<3;++kind)for(unsigned fault=0;fault<5;++fault) endings(kind,fault);
    for(unsigned kind=0;kind<4;++kind)for(unsigned ch=1;ch<=2;++ch)
        for(unsigned large=0;large<2;++large)for(unsigned f=0;f<5;++f)
            playback(kind,ch,large ? 1042 : 50,large ? 1042 : 18,fragments[f],f%2);
    printf("joint_reader: %u cases, %u checks passed\n",cases,checks);
    return 0;
}
