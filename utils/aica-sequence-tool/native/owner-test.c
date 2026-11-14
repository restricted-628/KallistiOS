#include "owner.h"
#include <dc/sound/aica_observation.h>
#include <dc/sound/aica_sequence_resource.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire-fixture.h"
static _Alignas(32) unsigned char ram[0x200000];
static asq_catalog_t catalog;static asq_wire_t wire;static asq_receipt_t receipt;static asq_owner_t host;
static unsigned checks,allocations,frees,allocation_calls,fail_allocation,submissions,transfer_calls;
static unsigned destroy_busy,transfer_hold,transport,bad_receipt,free_busy,partial;
static uint32_t active=9,features=AICA_DRIVER_FEATURE_SEQUENCE_RESOURCE,next_address=0x30000;
static uint32_t allocated[64];
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort(); } } while(0)
struct spu_transfer_request { spu_transfer_status_t status;const void *input;uint32_t address;int live; };
static struct spu_transfer_request request;
int irq_inside_int(void) { return 0; }
uint64_t timer_ms_gettime64(void) { return 1; }
uint32_t snd_iface_observation_features(void) { return features; }
int snd_observation_session(uint32_t *s) { *s=active;return 0; }
int snd_observation_bind_until(uint64_t deadline,uint32_t *s) { C(deadline>1);*s=active;return 0; }
int snd_iface_observation_read(uint32_t offset,void *out,uint32_t bytes) {
    if(offset==AICA_MEM_OBSERVATION_HEADER) {
        aica_observation_header_t h={0};C(bytes==sizeof(h));h.ready=1;h.layout_version=AICA_OBSERVATION_LAYOUT_VERSION;h.active_session=active;memcpy(out,&h,bytes);return 0;
    }
    C(offset==AICA_MEM_SEQUENCE_RECEIPT && (bytes==4 || bytes==256));memcpy(out,&receipt,bytes);
    if(bad_receipt && bytes==256) ((uint32_t *)out)[63]=1;
    return 0;
}
int snd_iface_observation_submit(void *packet,uint32_t words,uint64_t deadline) {
    ++submissions;C(words==16 && deadline>1);
    if(transport==1) { errno=EIO;return -1; }
    C(asq_wire_command(&wire,packet,active));
    if(transport==2) { errno=EIO;return -1; }return 0;
}
uint32_t snd_mem_malloc(size_t bytes) {
    ++allocation_calls;if(fail_allocation==allocation_calls) { errno=ENOMEM;return 0; }
    C(bytes && bytes<0x100000 && allocations<64);
    uint32_t result=next_address;next_address+=(uint32_t)((bytes+31)&~(size_t)31);
    C(next_address<0x200000);allocated[allocations++]=result;return result;
}
void snd_mem_free(uint32_t address) {
    if(free_busy) { --free_busy;errno=EBUSY;return; }
    unsigned i;for(i=0;i<allocations && allocated[i]!=address;i++) {}
    C(i<allocations);allocated[i]=allocated[--allocations];++frees;
}
int spu_memload_async(uintptr_t to,const void *from,size_t bytes,spu_transfer_method_t method,uint32_t timeout,
    spu_transfer_callback_t callback,void *data,spu_transfer_request_t **out) {
    C(!request.live && bytes && method==SPU_TRANSFER_AUTO && timeout && !callback && !data);
    C(to>=0x30000 && bytes<=0x200000-to);++transfer_calls;memset(&request,0,sizeof(request));
    request.live=1;request.input=from;request.address=(uint32_t)to;
    request.status.state=SPU_TRANSFER_RUNNING;request.status.direction=SPU_TRANSFER_TO_SOUND_RAM;
    request.status.requested_bytes=bytes;*out=&request;return 0;
}
int spu_transfer_get_status(const spu_transfer_request_t *p,spu_transfer_status_t *s) {
    C(p==&request && request.live);
    if(request.status.state==SPU_TRANSFER_RUNNING && !transfer_hold) {
        memcpy(ram+request.address,request.input,request.status.requested_bytes);
        request.status.state=SPU_TRANSFER_COMPLETE;request.status.completed_bytes=request.status.requested_bytes-(partial?1u:0u);
    }
    *s=request.status;return 0;
}
int spu_transfer_cancel(spu_transfer_request_t *p) {
    C(p==&request && request.live);
    if(request.status.state==SPU_TRANSFER_COMPLETE) { errno=EALREADY;return -1; }
    request.status.state=SPU_TRANSFER_CANCELLED;request.status.result=ECANCELED;return 0;
}
int spu_transfer_destroy(spu_transfer_request_t *p) {
    C(p==&request && request.live);
    if(destroy_busy) { --destroy_busy;errno=EBUSY;return -1; }
    C(request.status.state!=SPU_TRANSFER_RUNNING);request.live=0;return 0;
}
static void *map(void *ctx,uint32_t a,uint32_t n,int write) { (void)ctx;(void)n;(void)write;return ram+a; }
static void pump_to(unsigned state) {
    for(unsigned i=0;i<1000 && host.slots[0].state!=state;i++) {
        asq_wire_service(&wire);int rc=asq_owner_pump(&host);C(rc==0 || host.slots[0].state==state);
    }
    C(host.slots[0].state==state);
}
int main(int argc,char **argv) {
    unsigned mode=argc>1?(unsigned)strtoul(argv[1],NULL,10):0;
    C(asq_catalog_init(&catalog)==0);C(asq_wire_init(&wire,&catalog,&receipt,map,NULL));
    if(mode==1) { features=0;C(asq_owner_init(&host,32,100)<0 && errno==ENOTSUP && !allocation_calls);goto done; }
    C(asq_owner_init(&host,32,100)==0);
    if(mode==2) fail_allocation=2;
    if(mode==6 || mode==14) transfer_hold=1;
    if(mode==13) partial=1;
    if(mode==10) {
        memcpy(ram+0x100000,fixture,sizeof(fixture));
        C(asq_catalog_begin(&catalog,0,1,ram+0x100000,sizeof(fixture),(aica_seq_event_t *)(void *)(ram+0x110000),7)==0);
        while(asq_catalog_step(&catalog,0,1)==0) {}
        wire.spans[0][0]=0x100000;wire.spans[0][1]=sizeof(fixture);wire.spans[0][2]=0x110000;wire.spans[0][3]=7;
    }
    if(mode==2) {
        C(asq_owner_begin(&host,0,fixture,sizeof(fixture))<0 && allocations==1);
        C(asq_owner_close(&host,0)==0 && !allocations);goto done;
    }
    C(asq_owner_begin(&host,0,fixture,sizeof(fixture))==0 && allocations==2);
    if(mode==3) {
        destroy_busy=2;C(asq_owner_pump(&host)==0 && request.live && submissions==0 && frees==0);
        C(asq_owner_pump(&host)==0 && request.live && submissions==0 && frees==0);
    }
    if(mode==4 || mode==5) transport=mode-3;
    if(mode==6) { C(asq_owner_close(&host,0)==0);pump_to(ASQH_CLOSED);C(!allocations && !submissions);goto done; }
    if(mode==14) {
        active=10;C(asq_owner_pump(&host)==0 && request.live);C(asq_owner_pump(&host)<0);
        C(!request.live && allocations==2 && !frees && !submissions);goto done;
    }
    if(mode==13) { pump_to(ASQH_FAILED);C(!submissions && allocations==2);C(asq_owner_close(&host,0)==0 && !allocations);goto done; }
    pump_to(ASQH_REGISTER);
    if(mode==16) {
        ram[host.slots[0].source+32]^=1;
        while(wire.pending) asq_wire_service(&wire);
        C(receipt.state==ASQ_CATALOG_FAILED && (receipt.flags&ASQ_WIRE_ADMITTED));
        receipt.flags&=~ASQ_WIRE_ADMITTED;
        C(asq_owner_pump(&host)<0 && host.lost && allocations==2 && !frees);
        C(asq_owner_close(&host,0)<0 && allocations==2);goto done;
    }
    if(mode==4 || mode==5) { C(asq_owner_pump(&host)==0);transport=0;C(asq_owner_retry(&host)>=0); }
    if(mode==7) { C(asq_owner_close(&host,0)==0);pump_to(ASQH_CLOSED);C(!allocations);goto done; }
    if(mode==8) { active=10;C(asq_owner_pump(&host)<0);C(asq_owner_close(&host,0)<0 && allocations==2 && !frees);goto done; }
    if(mode==9) { bad_receipt=1;C(asq_owner_pump(&host)<0);C(host.lost && allocations==2 && !frees);goto done; }
    if(mode==10) {
        pump_to(ASQH_FAILED);C(!host.slots[0].admitted && catalog.slots[0].state==ASQ_CATALOG_READY);
        C(asq_owner_close(&host,0)==0 && !allocations && frees==2);
        C(catalog.slots[0].bank.source==ram+0x100000 && catalog.slots[0].state==ASQ_CATALOG_READY);goto done;
    }
    pump_to(ASQH_READY);C(!host.slots[0].input && !request.live && allocations==2);
    C(asq_owner_query(&host,0)==0);pump_to(ASQH_READY);
    C(host.client.receipt.programs==1 && host.client.receipt.event_count==7);
    C(asq_owner_begin(&host,0,fixture,sizeof(fixture))<0 && allocations==2);
    if(mode==11) {
        asq_lease_t lease;C(asq_catalog_acquire(&catalog,0,1,0,&lease)==0);
        C(asq_owner_close(&host,0)==0);C(asq_owner_pump(&host)<0 && errno==EBUSY && !frees);
        C(asq_catalog_return(&catalog,&lease)==0);
    }
    if(mode==12) free_busy=1;
    C(asq_owner_close(&host,0)==0);
    if(mode==12) { C(asq_owner_pump(&host)<0 && allocations==2);C(asq_owner_close(&host,0)==0); }
    else pump_to(ASQH_CLOSED);
    C(!allocations && frees==2 && !request.live && catalog.slots[0].state==ASQ_CATALOG_EMPTY);
    if(mode==15) for(unsigned generation=2;generation<=4;generation++) {
        C(asq_owner_begin(&host,0,fixture,sizeof(fixture))==0);pump_to(ASQH_READY);
        C(host.slots[0].generation==generation);C(asq_owner_close(&host,0)==0);pump_to(ASQH_CLOSED);C(!allocations);
    }
done:printf("ASQ owner case%u: %u checks PASS\n",mode,checks);return 0;
}
