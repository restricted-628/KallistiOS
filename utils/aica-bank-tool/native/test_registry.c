#include "abk_registry.h"
#include "aica_bank_registry.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

static atomic_uint checks;
static unsigned maps, frees, allocs, submits;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static unsigned char ram[0x200000];
static aica_bank_header_t bh;
static aica_bank_record_t br[128];
static const uint8_t *mapping(uint32_t a,uint32_t n) {
    ++maps;CHECK(a>=0x30000 && n<=sizeof(ram)-a);return ram+a;
}
static aica_bank_registry_t arm={.header=&bh,.records=br,.map=mapping};
static uint32_t session=7,feature=AICA_DRIVER_FEATURE_BANK_REGISTRY;
static uint32_t queued[14],queued_words,allocation;
static int free_error,submit_error,read_error,irq;
struct spu_transfer_request { int live; };
static struct spu_transfer_request request;
static spu_transfer_status_t transfer;
static abk_sample_t samples[256];
static abk_zone_t zones[1024];
int irq_inside_int(void) { return irq; }
uint64_t timer_ms_gettime64(void) { return 1; }
uint32_t snd_iface_observation_features(void) { return feature; }
int snd_observation_session(uint32_t *s) { *s=session;return 0; }
int snd_observation_bind_until(uint64_t d,uint32_t *s) { CHECK(d>1);*s=session;return 0; }
int snd_iface_observation_read(uint32_t offset,void *out,uint32_t n) {
    const void *p;
    if(read_error) { errno=read_error;return -1; }
    CHECK(n==64);
    if(offset==AICA_MEM_OBSERVATION_HEADER) {
        aica_observation_header_t h={0};h.active_session=session;h.ready=1;
        memcpy(out,&h,n);return 0;
    }
    if(offset==AICA_MEM_BANK_HEADER) p=&bh;
    else { CHECK(offset>=AICA_MEM_BANK_RECORDS && offset<AICA_MEM_BANK_END);
        p=&br[(offset-AICA_MEM_BANK_RECORDS)/64]; }
    if(*(const uint32_t *)p==UINT32_MAX) { errno=EOVERFLOW;return -1; }
    memcpy(out,p,n);return 0;
}
int snd_iface_observation_submit(void *p,uint32_t n,uint64_t deadline) {
    ++submits;CHECK(deadline>1 && (n==11 || n==14));
    if(submit_error) { errno=submit_error;return -1; }
    CHECK(!queued_words);memcpy(queued,p,n*4);queued_words=n;return 0;
}
uint32_t snd_mem_malloc(size_t n) {
    ++allocs;CHECK(!allocation && n<=sizeof(ram)-0x30000);allocation=(uint32_t)n;return 0x30000;
}
void snd_mem_free(uint32_t a) {
    ++frees;CHECK(a==0x30000 && allocation && !request.live);
    CHECK(br[0].state!=AICA_BANK_ACTIVE && br[0].state!=AICA_BANK_VALIDATING);
    if(free_error) { errno=free_error;return; }
    memset(ram+a,0xee,allocation);allocation=0;
}
int spu_memload_async(uintptr_t to,const void *from,size_t n,
                     spu_transfer_method_t method,uint32_t lifetime,
                     spu_transfer_callback_t callback,void *arg,spu_transfer_request_t **out) {
    CHECK(!request.live && allocation && to>=0x30000 && to+n<=0x30000+allocation);
    CHECK(method==SPU_TRANSFER_AUTO && lifetime && !callback && !arg);
    memcpy(ram+to,from,n);request.live=1;*out=&request;
    transfer=(spu_transfer_status_t){.state=SPU_TRANSFER_COMPLETE,
      .direction=SPU_TRANSFER_TO_SOUND_RAM,.requested_bytes=n,.completed_bytes=n,.result=0};return 0;
}
int spu_transfer_get_status(const spu_transfer_request_t *r,spu_transfer_status_t *s) {
    CHECK(r==&request && request.live);*s=transfer;return 0;
}
int spu_transfer_cancel(spu_transfer_request_t *r) { CHECK(r==&request && request.live);errno=EALREADY;return -1; }
int spu_transfer_destroy(spu_transfer_request_t *r) { CHECK(r==&request && request.live);request.live=0;return 0; }
static void dispatch(void) {
    CHECK(queued_words);CHECK(aica_bank_registry_command(&arm,(aica_cmd_t *)queued,session));queued_words=0;
}
static int command(unsigned slot,uint32_t cookie,int remove,uint32_t generation,uint32_t size) {
    uint32_t w[14]={0};aica_cmd_t *p=(aica_cmd_t *)w;
    p->cmd=remove?AICA_CMD_BANK_UNREGISTER:AICA_CMD_BANK_REGISTER;p->size=remove?11:14;p->cmd_id=cookie;
    w[8]=session;w[9]=slot;w[10]=generation;w[11]=0x30000;w[12]=size;w[13]=1;
    return aica_bank_registry_command(&arm,p,session);
}
static void stage(abk_bank_t *b,const unsigned char *data,uint32_t n) {
    CHECK(abk_bank_stage(b,0,data,n,samples,256,zones,1024,32,17)==0);
    while(b->state==ABKB_STAGING) CHECK(abk_bank_poll(b,10)==0);
    CHECK(b->state==ABKB_STAGED && !memcmp(data,ram+0x30000,n));
}
static void publish(abk_bank_t *b) {
    CHECK(abk_bank_publish(b,10)==0 && b->state==ABKB_PUBLISHING);
    CHECK(abk_bank_poll(b,10)==-1 && errno==EAGAIN && allocation);
    dispatch();CHECK(br[0].state==AICA_BANK_VALIDATING && !br[0].image_base);
    CHECK(abk_bank_poll(b,10)==0 && b->state==ABKB_PUBLISHING);
    while(arm.head!=128) CHECK(aica_bank_registry_service(&arm)<=32);
    CHECK(abk_bank_poll(b,10)==0 && b->state==ABKB_ACTIVE && !b->staging.address);
    CHECK(b->staging.state==ABKU_MOVED && b->address==0x30000);
}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static unsigned waiting;
static int release_gate;
static abk_bank_t *racing;
static void *racer(void *closing) {
    CHECK(!pthread_mutex_lock(&gate));++waiting;pthread_cond_broadcast(&wake);
    while(!release_gate) pthread_cond_wait(&wake,&gate);
    CHECK(!pthread_mutex_unlock(&gate));
    if(closing) (void)abk_bank_close(racing,10);
    else (void)abk_bank_poll(racing,10);
    return NULL;
}
static void adoption_races(const unsigned char *data,uint32_t n) {
    unsigned i;
    for(i=0;i<64;i++) {
        abk_bank_t b={0};pthread_t a,c;
        stage(&b,data,n);CHECK(abk_bank_publish(&b,10)==0);dispatch();
        while(arm.head!=128) aica_bank_registry_service(&arm);
        CHECK(br[0].state==AICA_BANK_ACTIVE && b.state==ABKB_PUBLISHING);
        waiting=0;release_gate=0;racing=&b;
        CHECK(!pthread_create(&a,NULL,racer,NULL));CHECK(!pthread_create(&c,NULL,racer,&b));
        CHECK(!pthread_mutex_lock(&gate));
        while(waiting<2) pthread_cond_wait(&wake,&gate);
        release_gate=1;pthread_cond_broadcast(&wake);CHECK(!pthread_mutex_unlock(&gate));
        CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(c,NULL));
        CHECK(b.state==ABKB_UNREGISTERING && allocation && b.address==0x30000 && !b.staging.address);
        dispatch();CHECK(abk_bank_poll(&b,10)==0 && b.state==ABKB_CLOSED && !allocation);
    }
}
static void host_cases(const unsigned char *data,uint32_t n) {
    abk_bank_t b={0},other={0};unsigned before;
    stage(&b,data,n);
    CHECK(abk_bank_stage(&other,0,data,n,samples,256,zones,1024,32,17)==-1 && errno==EBUSY);
    submit_error=EAGAIN;CHECK(abk_bank_publish(&b,10)==-1 && b.state==ABKB_STAGED);submit_error=0;
    publish(&b);
    CHECK(abk_bank_pending_acquire(&b)==0);
    CHECK(abk_bank_close(&b,10)==-1 && errno==EBUSY && b.state==ABKB_ACTIVE);
    CHECK(abk_bank_pending_acquire(&b)==-1 && errno==EBUSY);
    CHECK(abk_bank_pending_release(&b)==0);
    CHECK(aica_bank_registry_pin(&arm,session,0,b.generation,0));
    CHECK(aica_bank_registry_promote(&arm,session,0,b.generation));
    CHECK(br[0].voice_refs==1 && !br[0].queued_refs);
    CHECK(abk_bank_poll(&b,10)==0);dispatch();
    CHECK(abk_bank_poll(&b,10)==-1 && errno==EBUSY && allocation);
    CHECK(aica_bank_registry_unpin(&arm,session,0,b.generation,1));
    CHECK(!aica_bank_registry_unpin(&arm,session,0,b.generation,1));
    CHECK(abk_bank_poll(&b,10)==0);dispatch();
    before=maps;CHECK(command(0,b.cookie,1,b.generation,n));CHECK(maps==before);
    free_error=EBUSY;CHECK(abk_bank_poll(&b,10)==-1 && b.state==ABKB_RETIRED && allocation);
    free_error=0;CHECK(abk_bank_poll(&b,10)==0 && b.state==ABKB_CLOSED && !allocation);
    CHECK(!command(0,b.cookie-1,0,b.generation,n) && maps==before);
    CHECK(command(0,b.cookie,1,b.generation,n) && maps==before);
    /* Close before ARM receipt: adoption must precede unregistration/free. */
    b=(abk_bank_t){0};stage(&b,data,n);CHECK(abk_bank_publish(&b,10)==0);
    CHECK(abk_bank_close(&b,10)==-1 && errno==EAGAIN && allocation);
    dispatch();while(arm.head!=128) aica_bank_registry_service(&arm);
    CHECK(abk_bank_poll(&b,10)==0 && b.state==ABKB_UNREGISTERING && allocation);
    dispatch();CHECK(abk_bank_poll(&b,10)==0 && !allocation);
    /* Cancellation before publish retires transport, never registers. */
    b=(abk_bank_t){0};CHECK(abk_bank_stage(&b,0,data,n,samples,256,zones,1024,32,17)==0);
    before=submits;(void)abk_bank_close(&b,10);
    CHECK(b.state==ABKB_CLOSED && !allocation && submits==before);
    /* Failed metadata publication can close only after exact rejection. */
    b=(abk_bank_t){0};stage(&b,data,n);ram[0x30000]=0;
    CHECK(abk_bank_publish(&b,10)==0);dispatch();aica_bank_registry_service(&arm);
    CHECK(abk_bank_poll(&b,10)==-1 && b.state==ABKB_FAILED && allocation);
    CHECK(abk_bank_close(&b,10)==0 && !allocation);
    /* All terminal owner paths above release the reserved slot. */
    feature=0;b=(abk_bank_t){0};before=allocs;
    CHECK(abk_bank_stage(&b,0,data,n,samples,256,zones,1024,32,17)==-1 && errno==ENOTSUP && allocs==before);
    feature=AICA_DRIVER_FEATURE_BANK_REGISTRY;
}
static void arm_cases(const unsigned char *data,uint32_t n) {
    unsigned i,turns=0,total=0,before;
    aica_bank_registry_init(&arm);memcpy(ram+0x30000,data,n);maps=0;
    for(i=0;i<128;i++) CHECK(command(i,1,0,1,n));
    CHECK(maps==0); /* Commands do not consume metadata outside the budget. */
    while(arm.head!=128) {
        unsigned done=aica_bank_registry_service(&arm);CHECK(done<=32);total+=done;++turns;
        if(turns<=4) for(i=0;i<128;i++) CHECK(arm.cursor[i]==(i<turns*32?1u:0u));
    }
    CHECK(total==128u*1281u && turns==5124 && maps==total);
    for(i=0;i<128;i++) CHECK(br[i].state==AICA_BANK_ACTIVE && br[i].generation==1);
    before=maps;CHECK(command(0,1,0,1,n) && maps==before);
    CHECK(command(0,2,1,1,n));CHECK(br[0].state==AICA_BANK_EMPTY);
    CHECK(command(0,2,1,1,n) && maps==before);
    CHECK(!command(0,1,0,1,n) && maps==before);
    br[0].generation=UINT32_MAX;CHECK(command(0,3,0,0,n));CHECK(br[0].result==AICA_BANK_EXHAUSTED);
    br[0].sequence=UINT32_MAX-1;before=br[0].cookie;
    CHECK(!command(0,4,0,0,n) && br[0].sequence==UINT32_MAX && br[0].cookie==before);
    br[1].queued_refs=UINT32_MAX;CHECK(!aica_bank_registry_pin(&arm,session,1,1,0));
    br[1].voice_refs=UINT32_MAX;CHECK(!aica_bank_registry_promote(&arm,session,1,1));
    CHECK(!aica_bank_registry_pin(&arm,session+1,1,1,0));
    CHECK(!aica_bank_registry_pin(&arm,session,128,1,0));
    CHECK(!aica_bank_registry_pin(&arm,session,1,2,0));
    CHECK(!aica_bank_registry_pin(&arm,session,1,1,3));
    aica_bank_registry_init(&arm);
}
static void malformed_cases(const unsigned char *data,uint32_t n) {
    unsigned i,bit,turns;abk_plan_t plan;
    /* Exhaustively mutate header/metadata bits; valid host inputs must remain
       accepted. ARM intentionally trusts host CRC/zone ambiguity validation. */
    for(i=0;i<256;i++) for(bit=0;bit<8;bit++) {
        abk_result_t native;
        aica_bank_registry_init(&arm);memcpy(ram+0x30000,data,n);
        ram[0x30000+i]^=(unsigned char)(1u<<bit);
        native=abk_plan_build(ram+0x30000,n,&plan,samples,256,zones,1024);
        CHECK(command(0,1,0,1,n));turns=0;
        while(arm.head!=128) { CHECK(aica_bank_registry_service(&arm)<=32);CHECK(++turns<=41); }
        CHECK(br[0].state!=AICA_BANK_VALIDATING);
        if(native==ABK_OK) CHECK(br[0].state==AICA_BANK_ACTIVE);
        if(br[0].state==AICA_BANK_ACTIVE) CHECK(br[0].image_bytes==n && br[0].sample_count==3 && br[0].zone_count==3);
    }
    aica_bank_registry_init(&arm);memcpy(ram+0x30000,data,n);
    CHECK(command(0,1,0,1,n));br[0].sequence=UINT32_MAX-1;
    while(arm.head!=128) aica_bank_registry_service(&arm);
    CHECK(br[0].sequence==UINT32_MAX && br[0].state==AICA_BANK_VALIDATING && !br[0].image_base);
    aica_bank_registry_init(&arm);
    CHECK(command(0,1,0,1,UINT32_MAX));CHECK(br[0].result==AICA_BANK_BAD_IMAGE);
    CHECK(command(0,2,0,1,32));CHECK(br[0].result==AICA_BANK_BAD_IMAGE);
    CHECK(command(0,3,0,2,n));CHECK(br[0].result==AICA_BANK_STALE);
    CHECK(command(0,4,0,1,n));
    CHECK(!command(0,5,1,1,n));CHECK(br[0].cookie==4 && br[0].result==AICA_BANK_PENDING);
    while(arm.head!=128) aica_bank_registry_service(&arm);
    CHECK(command(0,UINT32_MAX,1,1,n));CHECK(br[0].state==AICA_BANK_EMPTY);
    CHECK(command(0,UINT32_MAX,1,1,n));CHECK(!command(0,1,0,2,n));
    aica_bank_registry_init(&arm);
}
static void lost_case(const unsigned char *data,uint32_t n) {
    /* LOST must retain an ARM-owned allocation. Model reset below is test-only
       cleanup, not a supported production recovery API. */
    static abk_bank_t b;unsigned before;
    stage(&b,data,n);publish(&b);before=frees;
    ++session;CHECK(abk_bank_close(&b,10)==-1 && b.state==ABKB_LOST && allocation);
    CHECK(abk_bank_poll(&b,10)==-1 && frees==before && allocation);
    CHECK(abk_bank_quiesce()==-1 && errno==EBUSY);
    aica_bank_registry_init(&arm);allocation=0;
}
static unsigned char *load(const char *p,uint32_t *n) {
    FILE *f=fopen(p,"rb");long size;unsigned char *data;
    CHECK(f);CHECK(!fseek(f,0,SEEK_END));size=ftell(f);CHECK(size>0 && size<=0x1d0000);
    *n=(uint32_t)size;rewind(f);data=malloc(*n);CHECK(data);CHECK(fread(data,1,*n,f)==*n);fclose(f);return data;
}
int main(int argc,char **argv) {
    uint32_t n,m;unsigned char *data,*maximum;
    CHECK(argc==3 || argc==4 || argc==5);data=load(argv[1],&n);maximum=load(argv[2],&m);
    if(argc==5) {
        abk_bank_t b={0};unsigned before;int fault=atoi(argv[4]);
        aica_bank_registry_init(&arm);stage(&b,data,n);
        CHECK(abk_bank_publish(&b,10)==0);dispatch();
        while(arm.head!=128) aica_bank_registry_service(&arm);
        CHECK(br[0].state==AICA_BANK_ACTIVE);before=frees;
        if(fault>=13) {
            CHECK(abk_bank_poll(&b,10)==0 && b.state==ABKB_ACTIVE);
            CHECK(abk_bank_close(&b,10)==0);dispatch();
        }
        switch(fault) {
            case 0:br[0].flags=1;break;
            case 1:br[0].reserved=1;break;
            case 2:++br[0].cookie;break;
            case 3:++br[0].session;break;
            case 4:br[0].operation=99;break;
            case 5:br[0].state=AICA_BANK_EMPTY;break;
            case 6:br[0].image_base+=32;break;
            case 7:br[0].image_bytes+=32;break;
            case 8:++br[0].sample_count;break;
            case 9:br[0].queued_refs=1;break;
            case 10:br[0].result=99;break;
            case 11:br[0].sequence=UINT32_MAX;break;
            case 12:br[0].result=AICA_BANK_BAD_IMAGE;break;
            case 13:++br[0].generation;break;
            case 14:br[0].image_base=0x30000;break;
            case 15:br[0].voice_refs=1;break;
            case 16:br[0].result=AICA_BANK_STALE;break;
            default:CHECK(0);
        }
        CHECK(abk_bank_poll(&b,10)==-1 && b.state==ABKB_LOST && allocation && frees==before);
        CHECK(abk_bank_close(&b,10)==-1 && frees==before && allocation);
        free(data);free(maximum);printf("registry: malformed receipt %d retained ownership PASS\n",fault);return 0;
    }
    if(argc==4) {
        abk_bank_t b={0},other={0};unsigned before;
        aica_bank_registry_init(&arm);stage(&b,data,n);before=submits;
        CHECK(abk_bank_quiesce()==-1 && errno==EBUSY);
        CHECK(abk_bank_publish(&b,10)==-1 && errno==EBUSY && b.state==ABKB_STAGED && submits==before);
        CHECK(abk_bank_close(&b,10)==0 && !allocation);
        CHECK(abk_bank_quiesce()==0);before=allocs;
        CHECK(abk_bank_stage(&other,1,data,n,samples,256,zones,1024,32,17)==-1 && errno==EBUSY && allocs==before);
        free(data);free(maximum);puts("registry: quiesce stops both stage and publish PASS");return 0;
    }
    aica_bank_registry_init(&arm);host_cases(data,n);adoption_races(data,n);arm_cases(maximum,m);malformed_cases(data,n);lost_case(data,n);
    free(data);free(maximum);
    printf("registry: %u checks PASS; 128 banks, 32 aggregate records/pass, 5124 maximum-image passes\n",atomic_load(&checks));
    return 0;
}
