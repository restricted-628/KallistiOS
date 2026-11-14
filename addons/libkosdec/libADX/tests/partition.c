#include <kosdec/adx_partition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"partition:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct input {uint8_t *data;uint32_t bytes,next;size_t fragment;unsigned calls,wait_every,bad_call,mode;bool ordered;};
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static uint32_t offset(uint32_t count,uint32_t i){return i%17?8+count*8+i*3:0;}
static uint32_t length(uint32_t i){return i%17?1+i%3:0;}
static struct input fixture(uint32_t count,size_t fragment,unsigned wait) {
    struct input in={.bytes=8+count*11,.fragment=fragment,.wait_every=wait,.ordered=true};
    in.data=malloc((size_t)in.bytes+1);CHECK(in.data);memset(in.data,0,in.bytes+1);
    memcpy(in.data,"AFS",3);put(in.data+4,count);
    for(uint32_t i=0;i<count;++i){put(in.data+8+i*8,offset(count,i));put(in.data+12+i*8,length(i));}
    for(uint32_t i=8+count*8;i<in.bytes;++i)in.data[i]=(uint8_t)(i*29u);
    return in;
}
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *in=ctx;++in->calls;CHECK(cap && cap<=512 && off<=in->bytes);
    if(in->ordered)CHECK(off==in->next);
    if(in->bad_call==in->calls) {
        if(in->mode==1)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
        if(in->mode==2)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,1};
        if(in->mode==3)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap+1};
        if(in->mode==4)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,0};
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    }
    if(in->wait_every && in->calls%in->wait_every==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(off==in->bytes)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END,0};
    size_t n=in->bytes-off;if(n>cap)n=cap;if(n>in->fragment)n=in->fragment;
    memcpy(dst,in->data+off,n);in->next=off+(uint32_t)n;
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static enum kosdec_adx_status load(struct kosdec_adx_partition *p) {
    for(unsigned i=0;i<2000000;++i){enum kosdec_adx_status s=kosdec_adx_partition_step(p);
        if(s!=KOSDEC_ADX_MORE && s!=KOSDEC_ADX_NEED_INPUT)return s;
        struct kosdec_adx_range r={123,456};
        CHECK(kosdec_adx_partition_get_range(p,0,&r)==KOSDEC_ADX_PARTITION_NOT_READY && r.offset==123 && r.bytes==456);
    }
    CHECK(0);return KOSDEC_ADX_INVALID;
}
static void matrix(void) {
    uint32_t counts[]={0,1,63,64,65,137,65536};size_t fragments[]={1,3,7,64,511,512};
    for(unsigned k=0;k<7;++k)for(unsigned f=0;f<6;++f) {
        uint32_t n=counts[k];struct input in=fixture(n,fragments[f],3);
        struct kosdec_adx_range *entries=n?malloc(n*sizeof(*entries)):NULL;CHECK(!n || entries);
        struct kosdec_adx_partition p;CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,n)==KOSDEC_ADX_MORE);
        CHECK(load(&p)==KOSDEC_ADX_DONE);unsigned calls=in.calls;
        CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_DONE && in.calls==calls);
        struct kosdec_adx_partition_info info;CHECK(kosdec_adx_partition_get_info(&p,&info));
        CHECK(info.header_ready && info.count==n && info.loaded==n && info.work_bytes==n*sizeof(*entries));
        for(uint32_t i=0;i<n;++i){struct kosdec_adx_range r;CHECK(kosdec_adx_partition_get_range(&p,i,&r)==KOSDEC_ADX_PARTITION_OK);CHECK(r.offset==offset(n,i) && r.bytes==length(i));}
        struct kosdec_adx_range out={123,456};CHECK(kosdec_adx_partition_get_range(&p,n,&out)==KOSDEC_ADX_PARTITION_NOT_FOUND && out.offset==123 && out.bytes==456);
        CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_OK);CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_BAD_ARGUMENT);
        free(entries);free(in.data);
    }
}
static void malformed(void) {
    for(unsigned mode=0;mode<8;++mode) {
        struct input in=fixture(137,512,0);struct kosdec_adx_range entries[137];struct kosdec_adx_partition p;
        enum kosdec_adx_status expected=KOSDEC_ADX_INVALID;
        if(mode==0)in.data[0]='X';
        if(mode==1){in.data[3]=1;expected=KOSDEC_ADX_UNSUPPORTED;}
        if(mode==2){put(in.data+4,65537);expected=KOSDEC_ADX_UNSUPPORTED;}
        if(mode==3)put(in.data+4,1000);
        if(mode==4){put(in.data+8+136*8,1);put(in.data+12+136*8,1);}
        if(mode==5){put(in.data+8+136*8,in.bytes);put(in.data+12+136*8,1);}
        if(mode==6){put(in.data+8+136*8,UINT32_MAX);put(in.data+12+136*8,8);}
        if(mode==7){put(in.data+8+136*8,in.bytes+1);put(in.data+12+136*8,0);}
        CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,137)==KOSDEC_ADX_MORE);
        CHECK(load(&p)==expected);unsigned calls=in.calls;CHECK(kosdec_adx_partition_step(&p)==expected && in.calls==calls);
        struct kosdec_adx_range out={123,456};CHECK(kosdec_adx_partition_get_range(&p,1,&out)==KOSDEC_ADX_PARTITION_NOT_READY && out.offset==123);
        free(in.data);
    }
    for(unsigned mode=0;mode<5;++mode)for(unsigned call=1;call<=5;++call) {
        struct input in=fixture(137,3,0);in.mode=mode;in.bad_call=call;
        struct kosdec_adx_range entries[137];struct kosdec_adx_partition p;
        CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,137)==KOSDEC_ADX_MORE);
        CHECK(load(&p)==(mode==1?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_IO_ERROR));free(in.data);
    }
}
static void capacity_cancel(void) {
    struct input in=fixture(65,512,0);struct kosdec_adx_partition p;struct kosdec_adx_partition_info info;
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,NULL,0)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_NEED_OUTPUT);CHECK(kosdec_adx_partition_get_info(&p,&info));
    CHECK(info.header_ready && info.count==65 && info.loaded==0 && info.work_bytes==520);
    CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_NEED_OUTPUT && in.calls==1);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_OK);
    struct kosdec_adx_range entries[65];in.next=0;
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,65)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_MORE);CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_partition_get_info(&p,&info) && info.loaded==64);unsigned calls=in.calls;
    kosdec_adx_partition_cancel(&p);CHECK(load(&p)==KOSDEC_ADX_CANCELLED && in.calls==calls);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_OK);
    memset(&p,0xa5,sizeof(p));struct kosdec_adx_partition before=p;
    CHECK(kosdec_adx_partition_init(&p,NULL,&in,in.bytes,entries,65)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,NULL,65)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,65537)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&p,&before,sizeof(p)));
    for(unsigned bytes=0;bytes<8;++bytes){CHECK(kosdec_adx_partition_init(&p,read_at,&in,bytes,entries,65)==KOSDEC_ADX_TRUNCATED);CHECK(kosdec_adx_partition_step(&p)==KOSDEC_ADX_TRUNCATED && in.calls==calls);}
    CHECK(!kosdec_adx_partition_work_size(65537));free(in.data);
}
static void registry_views(void) {
    struct input in=fixture(3,512,0);struct kosdec_adx_range entries[3],entries2[3];
    struct kosdec_adx_partition p,q;struct kosdec_adx_partition_registry a,b;
    kosdec_adx_partition_registry_init(&a);kosdec_adx_partition_registry_init(&b);
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,3)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_partition_bind(&a,0,&p)==KOSDEC_ADX_PARTITION_NOT_READY);
    CHECK(load(&p)==KOSDEC_ADX_DONE);in.next=0;
    CHECK(kosdec_adx_partition_init(&q,read_at,&in,in.bytes,entries2,3)==KOSDEC_ADX_MORE);CHECK(load(&q)==KOSDEC_ADX_DONE);
    for(unsigned i=0;i<256;++i){CHECK(kosdec_adx_partition_bind(&a,i,&p)==KOSDEC_ADX_PARTITION_OK);CHECK(kosdec_adx_partition_bind(&a,i,&p)==KOSDEC_ADX_PARTITION_OK);}
    CHECK(p.registrations==256);CHECK(kosdec_adx_partition_bind(&b,7,&p)==KOSDEC_ADX_PARTITION_OK);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_BUSY && p.registrations==257);
    CHECK(kosdec_adx_partition_bind(&a,256,&q)==KOSDEC_ADX_PARTITION_BAD_ARGUMENT);
    CHECK(kosdec_adx_partition_bind(&a,0,&q)==KOSDEC_ADX_PARTITION_OK && p.registrations==256 && q.registrations==1);
    const struct kosdec_adx_partition *found=NULL;CHECK(kosdec_adx_partition_lookup(&a,0,&found)==KOSDEC_ADX_PARTITION_OK && found==&q);
    CHECK(kosdec_adx_partition_unbind(&a,0)==KOSDEC_ADX_PARTITION_OK && !q.registrations);
    CHECK(kosdec_adx_partition_lookup(&a,0,&found)==KOSDEC_ADX_PARTITION_NOT_FOUND && found==&q);
    CHECK(kosdec_adx_partition_unbind(&a,0)==KOSDEC_ADX_PARTITION_NOT_FOUND);
    struct kosdec_adx_view v,empty;CHECK(kosdec_adx_partition_open_view(&p,1,read_at,&in,&v)==KOSDEC_ADX_PARTITION_OK);
    CHECK(kosdec_adx_partition_open_view(&p,0,read_at,&in,&empty)==KOSDEC_ADX_PARTITION_OK);
    CHECK(kosdec_adx_partition_registry_close(&a)==KOSDEC_ADX_PARTITION_OK && p.registrations==1);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_BUSY);
    CHECK(kosdec_adx_partition_registry_close(&b)==KOSDEC_ADX_PARTITION_OK && !p.registrations);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_OK);CHECK(kosdec_adx_partition_close(&q)==KOSDEC_ADX_PARTITION_OK);
    in.ordered=false;uint8_t data[8];unsigned calls=in.calls;
    CHECK(kosdec_adx_view_read_at(&empty,0,data,8).status==KOSDEC_ADX_INPUT_END && in.calls==calls);
    struct kosdec_adx_input_result r=kosdec_adx_view_read_at(&v,0,data,8);
    CHECK(r.status==KOSDEC_ADX_INPUT_DATA && r.bytes==length(1) && !memcmp(data,in.data+offset(3,1),r.bytes));
    CHECK(kosdec_adx_view_read_at(&v,length(1),data,8).status==KOSDEC_ADX_INPUT_END);
    CHECK(kosdec_adx_view_read_at(&v,length(1)+1,data,8).status==KOSDEC_ADX_INPUT_ERROR);
    struct kosdec_adx_view before=v;CHECK(kosdec_adx_view_init(&v,read_at,&in,UINT32_MAX,UINT32_MAX,1)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&v,&before,sizeof(v)));free(in.data);
}
static void playback(void) {
    size_t sizes[2];uint8_t *sources[2];
    sources[0]=stream(50,18,1,32,61,&sizes[0]);sources[1]=stream(50,18,2,32,61,&sizes[1]);
    struct input in={.bytes=(uint32_t)(24+sizes[0]+sizes[1]),.fragment=7,.wait_every=3};
    in.data=calloc(in.bytes,1);CHECK(in.data);memcpy(in.data,"AFS",3);put(in.data+4,2);
    put(in.data+8,24);put(in.data+12,(uint32_t)sizes[0]);
    put(in.data+16,24+(uint32_t)sizes[0]);put(in.data+20,(uint32_t)sizes[1]);
    memcpy(in.data+24,sources[0],sizes[0]);memcpy(in.data+24+sizes[0],sources[1],sizes[1]);
    struct kosdec_adx_partition p;struct kosdec_adx_range entries[2];struct kosdec_adx_view views[2];
    CHECK(kosdec_adx_partition_init(&p,read_at,&in,in.bytes,entries,2)==KOSDEC_ADX_MORE);CHECK(load(&p)==KOSDEC_ADX_DONE);
    for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_partition_open_view(&p,i,read_at,&in,&views[i])==KOSDEC_ADX_PARTITION_OK);
    CHECK(kosdec_adx_partition_close(&p)==KOSDEC_ADX_PARTITION_OK);
    memset(entries,0,sizeof(entries)); /* Copied views cannot borrow this directory. */
    for(unsigned i=0;i<2;++i) {
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,kosdec_adx_view_read_at,&views[i],views[i].bytes,false);
        size_t at=0;bool done=false;
        for(unsigned calls=0;calls<10000;++calls) {
            int16_t pcm[22];struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&s,pcm,11);
            CHECK(r.status<=KOSDEC_ADX_DONE);
            for(size_t f=0;f<r.frames;++f)for(unsigned ch=0;ch<=i;++ch)CHECK(pcm[f*(i+1)+ch]==golden[ch][at+f]);
            at+=r.frames;if(r.status==KOSDEC_ADX_DONE){done=true;break;}
        }
        CHECK(done && at==65);free(sources[i]);
    }
    free(in.data);
}
int main(void){matrix();malformed();capacity_cancel();registry_views();playback();printf("partition: %u checks passed\n",checks);return 0;}
