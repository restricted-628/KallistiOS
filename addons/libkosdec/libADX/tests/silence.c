#include <kosdec/adx_joint.h>
#include <kosdec/adx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) {fprintf(stderr,"silence:%d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void advance_empty(struct kosdec_adx_joint *j,size_t n) {
    while(n) {
        struct kosdec_adx_chunk c;
        CHECK(kosdec_adx_joint_get(j,KOSDEC_ADX_JOINT_FREE,n,&c)==KOSDEC_ADX_JOINT_OK && c.bytes);
        memset(c.data,0x71,c.bytes);size_t take=c.bytes;n-=take;
        CHECK(kosdec_adx_joint_put(j,&c)==KOSDEC_ADX_JOINT_OK);
        while(take) {
            CHECK(kosdec_adx_joint_get(j,KOSDEC_ADX_JOINT_DATA,take,&c)==KOSDEC_ADX_JOINT_OK && c.bytes);
            take-=c.bytes;CHECK(kosdec_adx_joint_put(j,&c)==KOSDEC_ADX_JOINT_OK);
        }
    }
}
static void matrix(void) {
    unsigned capacities[]={1,17,18,19,35,36,37,64,127,512,513,1024};
    uint32_t requests[]={0,1,31,32,33,65,UINT32_MAX};
    for(unsigned k=0;k<sizeof(capacities)/sizeof(*capacities);++k)
    for(unsigned ex=0;ex<3;++ex)for(unsigned ch=1;ch<=2;++ch) {
        size_t cap=capacities[k],extra=ex==0?0:ex==1?(cap<36?cap:36):cap;
        for(size_t at=0;at<cap;++at)for(unsigned q=0;q<7;++q) {
            uint8_t data[2048],old[2048];memset(data,0xa5,sizeof(data));struct kosdec_adx_joint j;
            CHECK(kosdec_adx_joint_ring_init(&j,data,cap,extra)==KOSDEC_ADX_JOINT_OK);
            advance_empty(&j,at);memcpy(old,data,sizeof(data));
            size_t group=18*ch,groups=requests[q]/32;
            if(groups>cap/group)groups=cap/group;if(groups>512/group)groups=512/group;
            size_t bytes=groups*group;uint32_t inserted=123;
            CHECK(kosdec_adx_joint_insert_silence(&j,ch,requests[q],&inserted)==KOSDEC_ADX_JOINT_OK);
            CHECK(inserted==groups*32);
            for(size_t i=0;i<sizeof(data);++i) {
                bool written=i<cap && ((i+cap-at)%cap)<bytes;
                if(i>=cap && i<cap+extra)written=(((i-cap)+cap-at)%cap)<bytes;
                CHECK(data[i]==(written?0:old[i]));
            }
            struct kosdec_adx_joint_info info;CHECK(kosdec_adx_joint_query(&j,&info)==KOSDEC_ADX_JOINT_OK);
            CHECK(info.available[0]==cap-bytes && info.available[1]==bytes && !info.borrowed[0]);
            size_t read=0;
            while(read<bytes) {
                struct kosdec_adx_chunk c;
                CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_DATA,bytes-read,&c)==KOSDEC_ADX_JOINT_OK && c.bytes);
                for(size_t i=0;i<c.bytes;++i)CHECK(!c.data[i]);read+=c.bytes;
                CHECK(kosdec_adx_joint_put(&j,&c)==KOSDEC_ADX_JOINT_OK);
            }
            CHECK(kosdec_adx_joint_close(&j)==KOSDEC_ADX_JOINT_OK);
        }
    }
}
static void failures_borrows(void) {
    uint8_t data[96],old[96];memset(data,0x5a,sizeof(data));struct kosdec_adx_joint j,before;
    uint32_t out=123;struct kosdec_adx_chunk c,held;
    CHECK(kosdec_adx_joint_ring_init(&j,data,64,32)==KOSDEC_ADX_JOINT_OK);
    before=j;memcpy(old,data,sizeof(data));
    CHECK(kosdec_adx_joint_insert_silence(&j,0,32,&out)==KOSDEC_ADX_JOINT_BAD_ARGUMENT && out==123);
    CHECK(kosdec_adx_joint_insert_silence(&j,1,32,NULL)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(data,old,sizeof(data)));
    CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_FREE,8,&c)==KOSDEC_ADX_JOINT_OK);
    before=j;
    CHECK(kosdec_adx_joint_insert_silence(&j,1,32,&out)==KOSDEC_ADX_JOINT_BUSY && out==123);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(data,old,sizeof(data)));
    memset(c.data,0x39,c.bytes);CHECK(kosdec_adx_joint_put(&j,&c)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_DATA,5,&held)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_insert_silence(&j,2,64,&out)==KOSDEC_ADX_JOINT_OK && out==32);
    for(size_t i=0;i<held.bytes;++i)CHECK(held.data[i]==0x39);
    CHECK(kosdec_adx_joint_put(&j,&held)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_DATA,64,&c)==KOSDEC_ADX_JOINT_OK && c.bytes==39);
    for(size_t i=0;i<c.bytes;++i)CHECK(c.data[i]==(i<3?0x39:0));
    CHECK(kosdec_adx_joint_put(&j,&c)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_finish(&j)==KOSDEC_ADX_JOINT_OK);before=j;memcpy(old,data,sizeof(data));out=123;
    CHECK(kosdec_adx_joint_insert_silence(&j,1,32,&out)==KOSDEC_ADX_JOINT_SEALED && out==123);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(data,old,sizeof(data)));
    CHECK(kosdec_adx_joint_close(&j)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_memory_init(&j,data,64)==KOSDEC_ADX_JOINT_OK);before=j;
    CHECK(kosdec_adx_joint_insert_silence(&j,1,32,&out)==KOSDEC_ADX_JOINT_BAD_ARGUMENT && out==123);
    CHECK(!memcmp(&j,&before,sizeof(j)));
}
static void decode(void) {
    for(unsigned ch=1;ch<=2;++ch)for(unsigned carried=0;carried<2;++carried) {
        size_t n;uint8_t *p=stream(50,18,ch,32,61,&n);struct kosdec_adx_joint j;
        uint8_t data[128];CHECK(kosdec_adx_joint_ring_init(&j,data,128,0)==KOSDEC_ADX_JOINT_OK);
        advance_empty(&j,127);uint32_t frames;
        CHECK(kosdec_adx_joint_insert_silence(&j,ch,96,&frames)==KOSDEC_ADX_JOINT_OK && frames==96);
        size_t bytes=3*18*ch,at=0;uint8_t body[108];
        while(at<bytes) {
            struct kosdec_adx_chunk c;CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_DATA,bytes-at,&c)==KOSDEC_ADX_JOINT_OK);
            CHECK(c.bytes);memcpy(body+at,c.data,c.bytes);at+=c.bytes;
            CHECK(kosdec_adx_joint_put(&j,&c)==KOSDEC_ADX_JOINT_OK);
        }
        memcpy(p+50+(carried?18*ch:0),body+(carried?18*ch:0),bytes-(carried?18*ch:0));
        struct kosdec_adx_decoder d;kosdec_adx_init(&d);size_t used=0,decoded=0;bool nonzero_tail=false,done=false;
        for(unsigned i=0;i<20;++i) {
            int16_t pcm[64];struct kosdec_adx_result r=kosdec_adx_decode(&d,p+used,n-used,pcm,32,true);
            for(size_t f=0;f<r.frames;++f)for(unsigned c=0;c<ch;++c) {
                if(!carried)CHECK(pcm[f*ch+c]==0);
                if(decoded+f>=32 && pcm[f*ch+c])nonzero_tail=true;
            }
            used+=r.consumed;decoded+=r.frames;
            if(r.status==KOSDEC_ADX_DONE){done=true;break;}CHECK(r.status<KOSDEC_ADX_DONE);
        }
        CHECK(done && decoded==65 && used==n);CHECK(nonzero_tail==(bool)carried);free(p);
    }
}
int main(void) {matrix();failures_borrows();decode();printf("silence: %u checks passed\n",checks);return 0;}
