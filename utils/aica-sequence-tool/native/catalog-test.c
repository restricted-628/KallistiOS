#include "catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, emitted, silenced;
#define C(x) do { ++checks; if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); abort(); } } while(0)
static asq_catalog_t catalog;
static unsigned char blobs[8][160];
static aica_seq_event_t storage[8][4];
static void put(unsigned char *p,uint32_t n) { for(unsigned i=0;i<4;i++) { p[i]=(unsigned char)n;n>>=8; } }
static void seal(unsigned char *b) {
    uint32_t crc=UINT32_MAX;
    for(unsigned i=64;i<160;i++) { crc^=b[i];for(unsigned j=0;j<8;j++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1))); }
    put(b+32,crc^UINT32_MAX);
}
static void fixture(unsigned char *b) {
    memset(b,0,160);memcpy(b,"KOSASQ\0\0",8);b[8]=1;b[10]=64;
    put(b+12,160);put(b+16,1);put(b+20,4);put(b+24,64);put(b+28,96);
    put(b+64,96);put(b+68,4);put(b+72,96);put(b+76,500000);
    b[108]=AICA_SEQ_MIDI;b[109]=0x90;b[110]=60;b[111]=100;
    put(b+112,96);b[124]=AICA_SEQ_MIDI;b[125]=0x80;b[126]=60;
    put(b+128,96);b[140]=AICA_SEQ_MIDI;b[141]=0xb0;b[142]=48;b[143]=17;
    b[156]=AICA_SEQ_END;seal(b);
}
static void prepare(unsigned slot,uint32_t generation) {
    int rc;unsigned passes=0;
    C(asq_catalog_begin(&catalog,slot,generation,blobs[slot],160,storage[slot],4)==0);
    do { rc=asq_catalog_step(&catalog,slot,generation);C(++passes<20); } while(!rc);
    C(rc==1);
}
static int emit(void *ctx,unsigned s,unsigned a,unsigned b,unsigned p) {
    (void)ctx;(void)s;(void)a;(void)b;(void)p;++emitted;return 1;
}
static void silence(void *ctx,int release,int pause) { (void)ctx;(void)release;(void)pause;++silenced; }
static int resume(void *ctx) { (void)ctx;return 1; }
static void edges(void) {
    asq_catalog_t c,old;
    unsigned char data[8][192];aica_seq_event_t events[8][4];
    asq_lease_t lease;unsigned i,j,passes;int rc;
    C(asq_catalog_init(&c)==0);
    for(i=0;i<8;i++) {
        uint32_t crc=UINT32_MAX;unsigned char *b=data[i];
        memset(b,0,192);memcpy(b,"KOSASQ\0\0",8);b[8]=1;b[10]=64;
        put(b+12,192);put(b+16,2);put(b+20,4);put(b+24,64);put(b+28,128);
        for(j=0;j<2;j++) {
            unsigned d=64+j*32,e=128+j*32;
            put(b+d,e);put(b+d+4,2);put(b+d+8,96);put(b+d+12,500000);
            b[e+12]=AICA_SEQ_MIDI;b[e+13]=0x90;b[e+14]=60;b[e+15]=100;
            put(b+e+16,96);b[e+28]=AICA_SEQ_END;
        }
        /* CRC-correct error in the last program must not publish the first. */
        if(i==7) b[188]=99;
        for(j=64;j<192;j++) { crc^=b[j];for(unsigned k=0;k<8;k++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
        put(b+32,crc^UINT32_MAX);
        C(asq_catalog_begin(&c,i,1,b,192,events[i],4)==0);
    }
    old=c;
    C(asq_catalog_acquire(&c,0,1,0,(asq_lease_t *)(void *)&c)<0);
    C(memcmp(&old,&c,sizeof(c))==0);
    for(i=0;i<8;i++) {
        passes=0;
        do {
            asq_bank_t before=c.slots[i].bank;
            C(asq_catalog_acquire(&c,i,1,0,&lease)<0);
            rc=asq_catalog_step(&c,i,1);C(++passes<30);
            if(before.phase==1) C(c.slots[i].bank.crc_offset-before.crc_offset<=ASQ_CRC_BUDGET);
            if(before.phase==3) C(c.slots[i].bank.decoded-before.decoded<=AICA_SEQ_BUDGET);
            if(before.phase==4) C(c.slots[i].bank.validation.index-before.validation.index<=AICA_SEQ_BUDGET);
        } while(!rc);
        C(i==7?rc<0:rc==1);
        if(i<7) {
            C(asq_catalog_acquire(&c,i,1,1,&lease)==0);
            old=c;C(asq_catalog_drop(&c,i,1)==ASQ_CATALOG_BUSY);
            C(memcmp(&old,&c,sizeof(c))==0);
            C(asq_catalog_acquire(&c,i,1,0,(asq_lease_t *)(void *)data[i])<0);
            C(asq_catalog_acquire(&c,i,1,0,(asq_lease_t *)(void *)events[i])<0);
            C(asq_catalog_return(&c,&lease)==0);
        }
        C(asq_catalog_drop(&c,i,1)==0);
    }
    old=c;
    C(asq_catalog_begin(&c,0,2,&c,sizeof(c),events[0],4)<0);
    C(asq_catalog_begin(&c,0,2,data[0],192,(aica_seq_event_t *)(void *)&c,4)<0);
    C(asq_catalog_begin(&c,0,2,data[0],192,(aica_seq_event_t *)(UINTPTR_MAX-3),4)<0);
    C(memcmp(&old,&c,sizeof(c))==0);
}
int main(void) {
    asq_lease_t a,b,copy,leases[ASQ_CATALOG_BORROWS],extra;
    aica_sequence_t seq,other;
    aica_seq_ops_t ops={asq_catalog_retain,asq_catalog_release,emit,silence,resume,&catalog};
    asq_catalog_t previous;
    edges();C(asq_catalog_init(&catalog)==0);
    for(unsigned i=0;i<8;i++) fixture(blobs[i]);
    C(asq_catalog_begin(&catalog,0,0,blobs[0],160,storage[0],4)<0);
    C(asq_catalog_begin(&catalog,8,1,blobs[0],160,storage[0],4)<0);
    previous=catalog;
    C(asq_catalog_begin(&catalog,0,1,blobs[0],160,storage[0],3)<0);
    C(memcmp(&catalog,&previous,sizeof(catalog))==0);
    C(asq_catalog_begin(&catalog,0,1,blobs[0],160,storage[0],4)==0);
    C(asq_catalog_acquire(&catalog,0,1,0,&a)<0);
    previous=catalog;
    C(asq_catalog_begin(&catalog,1,1,blobs[0],160,storage[1],4)<0);
    C(asq_catalog_begin(&catalog,1,1,blobs[1],160,storage[0],4)<0);
    C(memcmp(&catalog,&previous,sizeof(catalog))==0);
    C(asq_catalog_drop(&catalog,0,2)<0);
    C(asq_catalog_drop(&catalog,0,1)==0);
    C(asq_catalog_begin(&catalog,0,1,blobs[0],160,storage[0],4)<0);
    prepare(0,2);prepare(1,1);
    C(asq_catalog_acquire(&catalog,0,1,0,&a)<0);
    C(asq_catalog_step(&catalog,0,1)<0);
    C(asq_catalog_acquire(&catalog,0,2,1,&a)<0);
    C(asq_catalog_acquire(&catalog,0,2,0,&a)==0);
    C(asq_catalog_acquire(&catalog,1,1,0,&a)<0);
    copy=a;C(asq_catalog_return(&catalog,&copy)<0);
    C(asq_catalog_drop(&catalog,0,2)==ASQ_CATALOG_BUSY);
    C(aica_sequence_init(&seq,&ops)==0);
    C(aica_sequence_init(&other,&ops)==0);
    C(aica_sequence_play(&seq,&a.validation,20)==1);
    C(aica_sequence_play(&other,&a.validation,20)==1);
    C(asq_catalog_return(&catalog,&a)==0);
    C(asq_catalog_return(&catalog,&a)<0);
    C(catalog.slots[0].playing==2);
    C(asq_catalog_retain(&catalog,&copy.validation.program)==0);
    C(aica_sequence_pause(&seq)==0);
    C(asq_catalog_drop(&catalog,0,2)==ASQ_CATALOG_BUSY);
    C(aica_sequence_continue(&seq)==1);
    C(asq_catalog_acquire(&catalog,1,1,0,&b)==0);
    C(aica_sequence_play(&seq,&b.validation,1)==0);
    C(catalog.slots[1].playing==0);
    catalog.slots[1].playing=UINT32_MAX;
    C(aica_sequence_play(&seq,&b.validation,31)==-2);
    C(aica_sequence_chain(&seq,&b.validation)==-2);
    C(seq.program.events==storage[0] && !seq.chain && catalog.slots[0].playing==2);
    catalog.slots[1].playing=0;
    C(aica_sequence_chain(&seq,&b.validation)==0);
    C(aica_sequence_chain(&seq,&b.validation)==0);
    C(catalog.slots[1].playing==1);
    C(asq_catalog_return(&catalog,&b)==0);
    C(asq_catalog_drop(&catalog,1,1)==ASQ_CATALOG_BUSY);
    C(aica_sequence_service(&seq,1000000)>0);
    C(catalog.slots[0].playing==1 && catalog.slots[1].playing==1);
    C(aica_sequence_service(&seq,1000000)>0);
    C(seq.state==AICA_SEQ_IDLE && catalog.slots[1].playing==0);
    aica_sequence_stop(&other);C(catalog.slots[0].playing==0);
    for(unsigned i=0;i<ASQ_CATALOG_BORROWS;i++) C(asq_catalog_acquire(&catalog,0,2,0,leases+i)==0);
    C(asq_catalog_acquire(&catalog,0,2,0,&extra)==ASQ_CATALOG_BUSY);
    C(asq_catalog_begin(&catalog,2,1,leases,sizeof(leases),storage[2],4)<0);
    for(unsigned i=0;i<ASQ_CATALOG_BORROWS;i++) C(asq_catalog_return(&catalog,leases+i)==0);
    C(asq_catalog_drop(&catalog,0,2)==0 && catalog.slots[0].bank.source==NULL);
    C(asq_catalog_drop(&catalog,1,1)==0);
    blobs[2][32]^=1;
    C(asq_catalog_begin(&catalog,2,1,blobs[2],160,storage[2],4)==0);
    C(asq_catalog_step(&catalog,2,1)<0);
    C(asq_catalog_acquire(&catalog,2,1,0,&a)<0);
    C(asq_catalog_drop(&catalog,2,1)==0);fixture(blobs[2]);
    prepare(2,2);
    C(asq_catalog_acquire(&catalog,2,2,0,&a)==0);
    catalog.slots[2].playing=UINT32_MAX;
    C(asq_catalog_retain(&catalog,&a.validation.program)==0);
    catalog.slots[2].playing=0;
    C(asq_catalog_return(&catalog,&a)==0);
    C(asq_catalog_drop(&catalog,2,2)==0);
    catalog.slots[2].generation=UINT32_MAX;
    C(asq_catalog_begin(&catalog,2,0,blobs[2],160,storage[2],4)<0);
    prepare(3,1);copy.validation.program.events=storage[3];copy.validation.program.count=3;
    asq_catalog_release(&catalog,&copy.validation.program);
    C(catalog.faulted && asq_catalog_drop(&catalog,3,1)==ASQ_CATALOG_FAULT);
    C(emitted>=4 && silenced>=3);
    printf("ASQ catalog: %u checks PASS\n",checks);return 0;
}
