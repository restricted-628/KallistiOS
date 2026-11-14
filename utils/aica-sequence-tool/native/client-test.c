#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire-fixture.h"
static _Alignas(32) unsigned char ram[0x200000];
static asq_catalog_t catalog;static asq_wire_t wire;static asq_receipt_t receipt;
static asq_client_t client;
static unsigned checks,submits,reads,transport,read_error,torn,corrupt;
static uint32_t active=9;
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort(); } } while(0)
static void *map(void *ctx,uint32_t a,uint32_t n,int write) { (void)ctx;(void)n;(void)write;return ram+a; }
static int session(void *ctx,uint32_t *s) { (void)ctx;*s=active;return 0; }
static int read_receipt(void *ctx,uint32_t *words,unsigned count) {
    (void)ctx;++reads;if(read_error) return -1;
    memcpy(words,&receipt,count*4u);
    if(torn && count==64) words[0]+=2;
    if(corrupt && count==64) words[63]=1;
    return 0;
}
static int submit(void *ctx,const uint32_t p[16]) {
    (void)ctx;++submits;if(transport==1) return 0;
    int accepted=asq_wire_command(&wire,(const aica_cmd_t *)(const void *)p,active);
    return transport==2?-1:accepted;
}
static void init(void) {
    const asq_client_ops_t ops={read_receipt,session,submit,NULL};
    C(asq_catalog_init(&catalog)==0);C(asq_wire_init(&wire,&catalog,&receipt,map,NULL));
    memcpy(ram+0x30000,fixture,sizeof(fixture));active=9;
    submits=reads=transport=read_error=torn=corrupt=0;
    C(asq_client_init(&client,9,&ops)==0);C(asq_client_poll(&client)==1 && client.state==ASQC_IDLE);
}
static void finish(void) { unsigned i=0;while(wire.pending) { asq_wire_service(&wire);C(++i<40); } }
int main(void) {
    const uint32_t args[4]={0x30000,sizeof(fixture),0x40000,7},zero[4]={0};
    uint32_t old[16];unsigned n;
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);
    C(asq_client_begin(&client,ASQ_WIRE_QUERY,0,1,zero)<0);
    C(asq_client_cancel(&client)<0); /* Not observed admitted yet. */
    C(asq_client_poll(&client)==0 && client.receipt.result==ASQ_WIRE_PENDING);
    memcpy(old,client.packet,64);C(asq_client_cancel(&client)==1);
    C(asq_client_poll(&client)==1 && client.receipt.result==0);
    C(client.receipt.superseded_cookie==old[3] && client.receipt.superseded_result==ASQ_WIRE_CANCELLED);
    C(asq_client_consume(&client)==0);C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,2,args)==1);
    finish();C(asq_client_poll(&client)==1 && client.receipt.state==ASQ_CATALOG_READY);
    C(asq_client_consume(&client)==0);C(asq_client_begin(&client,ASQ_WIRE_DROP,0,2,zero)==1);
    C(asq_client_poll(&client)==1 && client.receipt.superseded_result==0);
    for(unsigned mode=1;mode<=2;mode++) {
        init();transport=mode;
        C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==0 && client.state==ASQC_WAIT);
        C(asq_client_begin(&client,ASQ_WIRE_QUERY,0,1,zero)<0);
        memcpy(old,client.packet,64);C(asq_client_poll(&client)==0);
        transport=0;C(asq_client_retry(&client)==1 && !memcmp(old,client.packet,64));
        finish();C(asq_client_poll(&client)==1 && client.receipt.state==ASQ_CATALOG_READY);
    }
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);
    read_error=1;C(asq_client_poll(&client)==0 && client.state==ASQC_WAIT);read_error=0;
    torn=1;C(asq_client_poll(&client)==0 && client.state==ASQC_WAIT);torn=0;
    C(asq_client_poll(&client)==0);active=10;C(asq_client_poll(&client)<0 && client.state==ASQC_LOST);
    C(catalog.slots[0].state==ASQ_CATALOG_PREPARING);
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);
    corrupt=1;C(asq_client_poll(&client)<0 && client.state==ASQC_LOST);
    init();client.cookie=UINT32_MAX;n=submits;
    C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)<0 && client.state==ASQC_LOST && submits==n);
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);finish();
    receipt.events+=32;C(asq_client_poll(&client)<0 && client.state==ASQC_LOST);
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);
    receipt.sequence=UINT32_MAX;C(asq_client_poll(&client)<0 && client.state==ASQC_LOST);
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);
    receipt.result=ASQ_WIRE_CANCELLED;C(asq_client_poll(&client)<0 && client.state==ASQC_LOST);
    /* Occupied rejection exposes existing storage but ADMITTED is false for the
       new request; no client operation frees either allocation. */
    init();C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,1,args)==1);finish();
    C(asq_client_poll(&client)==1);C(asq_client_consume(&client)==0);
    C(asq_client_begin(&client,ASQ_WIRE_PREPARE,0,2,args)==1);
    C(asq_client_poll(&client)==1 && client.receipt.result==ASQ_CATALOG_BUSY);
    C(!(client.receipt.flags&ASQ_WIRE_ADMITTED) && client.receipt.generation==1);
    C(catalog.slots[0].state==ASQ_CATALOG_READY && catalog.slots[0].bank.source==ram+0x30000);
    printf("ASQ client: %u checks PASS\n",checks);return 0;
}
