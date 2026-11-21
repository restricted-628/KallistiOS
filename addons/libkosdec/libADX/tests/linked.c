#include <kosdec/adx_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"linked:%u %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
struct input { uint8_t *data;uint32_t bytes;unsigned fragment,calls;bool wait; };
static struct kosdec_adx_input_result read_at(void *context,uint32_t offset,uint8_t *out,size_t cap) {
    struct input *p=context;++p->calls;
    CHECK(offset<=p->bytes && cap<=p->bytes-offset && cap<=512);
    if(p->wait && p->calls%3==0) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment) cap=p->fragment;
    memcpy(out,p->data+offset,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static size_t decode(struct input *p,bool linked,size_t capacity,int16_t *pcm,
                     enum kosdec_adx_status expected,uint64_t entries) {
    struct kosdec_adx_stream stream;
    kosdec_adx_stream_init(&stream,read_at,p,p->bytes,false);
    CHECK(kosdec_adx_stream_set_linked(&stream,linked)==KOSDEC_ADX_DONE);
    if(linked) CHECK(kosdec_adx_stream_seek_begin(&stream,0)==KOSDEC_ADX_UNSUPPORTED);
    size_t frames=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned n=0;n<10000 && status<KOSDEC_ADX_DONE;++n) {
        unsigned calls=p->calls;
        struct kosdec_adx_stream_result r=kosdec_adx_stream_read(&stream,pcm+2*frames,capacity);
        CHECK(p->calls-calls<=1 && r.frames<=32);
        frames+=r.frames;status=r.status;
    }
    CHECK(status==expected && stream.linked_entries==entries);
    return frames;
}
static void link_control(struct input *p, const int16_t *reference, bool restore) {
    int16_t actual[196+64];
    struct kosdec_adx_stream reader;
    kosdec_adx_stream_init(&reader,read_at,p,p->bytes,false);
    CHECK(kosdec_adx_stream_set_linked(&reader,true)==KOSDEC_ADX_DONE);
    struct kosdec_adx_service service;int16_t ring[64];
    CHECK(kosdec_adx_service_init(&service,&reader,NULL,ring,64,32)==KOSDEC_ADX_MORE);
    bool enabled=false;
    CHECK(kosdec_adx_service_get_linked(&service,&enabled) && enabled);
    struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
    kosdec_adx_service_step(&service);enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_take(&service,&receipt) && receipt==KOSDEC_ADX_MORE);
    size_t frames=0;bool released=false,done=false;
    for(unsigned n=0;n<10000;++n) {
        struct kosdec_adx_stream_result made=kosdec_adx_service_step(&service);
        struct kosdec_adx_pipe_result got=kosdec_adx_pipe_read(&service.pipe,actual+2*frames,32);
        frames+=got.frames;
        if(frames && !released) {
            command.kind=restore ? KOSDEC_ADX_SET_LINK_FOLLOW : KOSDEC_ADX_RELEASE_LINK;
            command.enabled=false;
            CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
            kosdec_adx_service_step(&service);
            CHECK(kosdec_adx_service_take(&service,&receipt) && receipt==KOSDEC_ADX_MORE);
            CHECK(kosdec_adx_service_get_linked(&service,&enabled) && !enabled);
            command.kind=KOSDEC_ADX_SET_LINK_FOLLOW;
            command.enabled=true;
            CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
            kosdec_adx_service_step(&service);
            CHECK(kosdec_adx_service_take(&service,&receipt) &&
                  receipt==(restore ? KOSDEC_ADX_MORE : KOSDEC_ADX_BAD_ARGUMENT));
            CHECK(kosdec_adx_service_get_linked(&service,&enabled) && enabled==restore);
            released=true;
        }
        if(made.status>=KOSDEC_ADX_DONE) { CHECK(made.status==KOSDEC_ADX_DONE);done=true;break; }
    }
    CHECK(done && released && frames==(restore ? 98u : 65u) &&
          reader.linked_entries==(restore ? 2u : 1u));
    CHECK(!memcmp(actual,reference,2*frames*sizeof(int16_t)));
    CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_DONE);
}
int main(void) {
    size_t na,nb;
    uint8_t *a=stream(50,18,2,32,65,&na),*b=stream(50,18,2,0,33,&nb);
    memset(a+20,0,24);memset(b+20,0,24);
    put32(b+12,33);memmove(b+50+2*36,b+50+3*36,18);nb-=36;
    uint8_t *data=malloc(na+nb+2);CHECK(data);
    memcpy(data+1,a,na);memcpy(data+1+na,b,nb);
    int16_t reference[512],actual[512];
    struct input one={a,(uint32_t)na,512,0,false},two={b,(uint32_t)nb,512,0,false};
    CHECK(decode(&one,false,32,reference,KOSDEC_ADX_DONE,0)==65);
    CHECK(decode(&two,false,32,reference+130,KOSDEC_ADX_DONE,0)==33);
    unsigned fragments[]={1,2,3,4,7,17,18,19,35,36,37,64,127,511,512};
    unsigned capacities[]={1,2,7,17,31,32,33};
    for(unsigned i=0;i<sizeof(fragments)/sizeof(fragments[0]);++i)
        for(unsigned j=0;j<sizeof(capacities)/sizeof(capacities[0]);++j) {
            struct input p={data+1,(uint32_t)(na+nb),fragments[i],0,true};
            CHECK(decode(&p,true,capacities[j],actual,KOSDEC_ADX_DONE,2)==98);
            CHECK(!memcmp(actual,reference,196*sizeof(int16_t)));
        }
    struct input p={data+1,(uint32_t)(na+nb),512,0,false};
    CHECK(decode(&p,false,32,actual,KOSDEC_ADX_INVALID,0)==65);
    p.bytes=(uint32_t)(na+nb-1);
    CHECK(decode(&p,true,32,actual,KOSDEC_ADX_TRUNCATED,1)<=98);
    p.bytes=(uint32_t)(na+nb);data[1+na]=0;
    CHECK(decode(&p,true,32,actual,KOSDEC_ADX_INVALID,1)==65);
    data[1+na]=b[0];data[1+na+7]=1;
    CHECK(decode(&p,true,32,actual,KOSDEC_ADX_UNSUPPORTED,1)==65);
    data[1+na+7]=2;put32(data+1+na+8,32000);
    CHECK(decode(&p,true,32,actual,KOSDEC_ADX_UNSUPPORTED,1)==65);
    memcpy(data+1+na,b,nb);
    /* A valid zero-frame record still owns its header/footer and format. */
    uint8_t empty_then_audio[256];
    memcpy(empty_then_audio,a,50);put32(empty_then_audio+12,0);
    memcpy(empty_then_audio+50,a+na-18,18);memcpy(empty_then_audio+68,b,nb);
    for(unsigned i=0;i<sizeof(fragments)/sizeof(fragments[0]);++i) {
        struct input zero={empty_then_audio,(uint32_t)(68+nb),fragments[i],0,true};
        CHECK(decode(&zero,true,32,actual,KOSDEC_ADX_DONE,2)==33);
        CHECK(!memcmp(actual,reference+130,66*sizeof(int16_t)));
    }
    link_control(&p,reference,false);
    link_control(&p,reference,true);
    free(a);free(b);free(data);
    printf("ADX_LINKED checks=%u failures=0\n",checks);
    return 0;
}
