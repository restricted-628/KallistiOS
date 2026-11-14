#include "wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire-fixture.h"
static _Alignas(32) unsigned char ram[0x200000];
static asq_catalog_t catalog;
static asq_wire_t wire;
static asq_receipt_t receipt;
static unsigned checks,maps;
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort(); } } while(0)
static void *map(void *ctx,uint32_t address,uint32_t bytes,int write) {
    (void)ctx;(void)write;++maps;C(address>=0x30000 && bytes<=0x200000-address);return ram+address;
}
static void init(void) {
    memset(ram,0,sizeof(ram));memcpy(ram+0x30000,fixture,sizeof(fixture));
    C(asq_catalog_init(&catalog)==0);C(asq_wire_init(&wire,&catalog,&receipt,map,NULL));maps=0;
}
static void packet(uint32_t p[16],uint32_t id,unsigned op,unsigned slot,uint32_t generation) {
    memset(p,0,64);p[0]=16;p[1]=ASQ_WIRE_COMMAND;p[3]=id;
    p[8]=9;p[9]=op;p[10]=slot;p[11]=generation;
    if(op==ASQ_WIRE_PREPARE) { p[12]=0x30000;p[13]=sizeof(fixture);p[14]=0x40000;p[15]=7; }
}
static int send(uint32_t p[16]) { return asq_wire_command(&wire,(const aica_cmd_t *)(void *)p,9); }
static void finish(void) {
    unsigned count=0;
    while(wire.pending) { asq_wire_service(&wire);C(++count<40); }
}
int main(void) {
    uint32_t p[16],original[16],drop[16];asq_receipt_t before;asq_lease_t lease;
    init();packet(p,1,ASQ_WIRE_PREPARE,0,1);memcpy(original,p,64);
    C(send(p) && receipt.result==ASQ_WIRE_PENDING && receipt.flags==ASQ_WIRE_ADMITTED);
    before=receipt;C(send(p) && memcmp(&before,&receipt,256)==0);
    packet(p,2,ASQ_WIRE_QUERY,0,1);C(!send(p));
    packet(p,2,ASQ_WIRE_DROP,1,1);C(!send(p));
    packet(p,2,ASQ_WIRE_DROP,0,1);p[12]=1;C(!send(p));
    C(memcmp(&before,&receipt,256)==0);
    p[12]=0;memcpy(drop,p,64);C(send(p) && receipt.result==0);
    C(receipt.superseded_cookie==1 && receipt.superseded_result==ASQ_WIRE_CANCELLED);
    C(!memcmp(receipt.superseded_request,original+8,32));
    C(catalog.slots[0].state==0 && catalog.slots[0].bank.source==NULL);
    before=receipt;C(send(original) && !memcmp(&before,&receipt,256));
    C(send(drop) && !memcmp(&before,&receipt,256));
    packet(p,3,ASQ_WIRE_PREPARE,0,2);C(send(p));
    before=receipt;C(!send(original) && !memcmp(&before,&receipt,256));finish();
    C(receipt.result==0 && receipt.state==ASQ_CATALOG_READY && receipt.flags==ASQ_WIRE_ADMITTED);
    C(asq_catalog_acquire(&catalog,0,2,0,&lease)==0);
    packet(p,4,ASQ_WIRE_DROP,0,2);C(send(p) && receipt.result==ASQ_CATALOG_BUSY);
    C(receipt.superseded_cookie==3 && receipt.superseded_result==0);
    C(catalog.slots[0].borrowed==1 && catalog.slots[0].bank.source==ram+0x30000);
    C(asq_catalog_return(&catalog,&lease)==0);
    packet(p,5,ASQ_WIRE_PREPARE,0,2);p[12]=0x50000;p[14]=0x60000;
    memcpy(ram+0x50000,fixture,sizeof(fixture));C(send(p));
    C(receipt.result==ASQ_CATALOG_BUSY && !(receipt.flags&ASQ_WIRE_ADMITTED));
    C(catalog.slots[0].state==ASQ_CATALOG_READY && receipt.source==0x30000 && receipt.events==0x40000);
    packet(p,6,ASQ_WIRE_DROP,0,2);C(send(p) && receipt.result==0);
    C(receipt.state==0 && receipt.source==0 && receipt.events==0);
    /* Exact terminal replay never repeats admission or reads source again. */
    before=receipt;C(send(p) && !memcmp(&before,&receipt,256));
    for(unsigned field=0;field<16;field++) {
        init();packet(p,1,ASQ_WIRE_PREPARE,0,1);
        if(field==0) p[field]=15;
        else if(field==1) p[field]=99;
        else if(field==3 || field==8 || field==11 || field>=12) p[field]=0;
        else if(field==9 || field==10) p[field]=99;
        else p[field]=1;
        C(!send(p) || receipt.result<0);C(!wire.pending && !catalog.slots[0].state && !maps);
    }
    for(unsigned field=12;field<16;field++) {
        init();packet(p,1,ASQ_WIRE_PREPARE,0,1);p[field]=UINT32_MAX;
        C(send(p) && receipt.result<0 && !maps);
    }
    init();packet(p,1,ASQ_WIRE_PREPARE,0,1);p[14]=p[12];
    C(send(p) && receipt.result<0 && !catalog.slots[0].state);
    init();ram[0x30000+32]^=1;packet(p,1,ASQ_WIRE_PREPARE,0,1);C(send(p));finish();
    C(receipt.result<0 && receipt.state==ASQ_CATALOG_FAILED && (receipt.flags&ASQ_WIRE_ADMITTED));
    packet(p,2,ASQ_WIRE_DROP,0,1);C(send(p) && receipt.result==0);
    init();receipt.sequence=UINT32_MAX-3u;packet(p,1,ASQ_WIRE_PREPARE,0,1);
    C(!send(p) && wire.faulted && receipt.sequence==UINT32_MAX && !catalog.slots[0].state);
    init();packet(p,1,ASQ_WIRE_PREPARE,0,1);C(send(p));
    before=receipt;C(!asq_wire_command(&wire,(const aica_cmd_t *)(void *)p,10));
    C(!memcmp(&before,&receipt,256));finish();C(receipt.result==0);
    printf("ASQ wire: %u checks PASS\n",checks);return 0;
}
