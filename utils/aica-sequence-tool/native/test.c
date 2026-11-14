#include "asq.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static unsigned pins,emissions;
static int retain(void *context,const aica_seq_program_t *p) {
    (void)context;CHECK(p->count>0);++pins;return 1;
}
static void release(void *context,const aica_seq_program_t *p) {
    (void)context;(void)p;CHECK(pins>0);--pins;
}
static int emit(void *context,unsigned s,unsigned a,unsigned b,unsigned priority) {
    (void)context;CHECK(s>=0x80 && s<0xf0 && a<=127 && b<=127 && priority<=31);
    ++emissions;return 1;
}
static void silence(void *context,int release_mode,int pause) {
    (void)context;CHECK(release_mode<=1 && pause<=1);
}
static int resume(void *context) { (void)context;return 1; }
static uint32_t hashword(uint32_t h,uint32_t v) {
    unsigned i;for(i=0;i<4;i++) { h=(h^(v&255))*16777619u;v>>=8; }return h;
}
static void check_blob(uint8_t *source,size_t size) {
    asq_bank_t bank,old;
    asq_layout_t layout;
    aica_seq_validation_t view,previous;
    aica_seq_event_t *events;
    uint32_t checksum=2166136261u;
    unsigned i,passes=0;
    int result;
    memset(&bank,0x5a,sizeof(bank));old=bank;
    memset(&view,0xa5,sizeof(view));previous=view;
    if(asq_measure(source,size,&layout)<0) { puts("0");return; }
    events=malloc((size_t)layout.events*sizeof(*events));CHECK(events!=NULL);
    memset(events,0x72,(size_t)layout.events*sizeof(*events));
    CHECK(asq_begin(&bank,source,size,events,layout.events-1)<0);
    CHECK(memcmp(&bank,&old,sizeof(bank))==0);
    CHECK(asq_begin((asq_bank_t *)(void *)source,source,size,events,layout.events)<0);
    CHECK(asq_begin(&bank,source,size,(aica_seq_event_t *)(void *)source,layout.events)<0);
    CHECK(asq_begin(&bank,source,size,(aica_seq_event_t *)(void *)&bank,layout.events)<0);
    CHECK(asq_begin(&bank,source,size,(aica_seq_event_t *)((uint8_t *)events+1),layout.events)<0);
    CHECK(asq_measure(source,size,(asq_layout_t *)(void *)source)<0);
    CHECK(asq_measure((void *)(UINTPTR_MAX-15),64,&layout)<0);
    CHECK(asq_begin(&bank,source,size,(aica_seq_event_t *)(UINTPTR_MAX-3),layout.events)<0);
    CHECK(asq_begin((asq_bank_t *)(UINTPTR_MAX-3),source,size,events,layout.events)<0);
    CHECK(asq_begin(&bank,source,size,events,layout.events)==0);
    CHECK(asq_get(&bank,0,&view)<0 && memcmp(&view,&previous,sizeof(view))==0);
    do {
        uint32_t crc=bank.crc_offset,decoded=bank.decoded,index=bank.validation.index;
        uint8_t phase=bank.phase;
        result=asq_step(&bank);CHECK(++passes<20000);
        if(phase==1) CHECK(bank.crc_offset-crc<=ASQ_CRC_BUDGET);
        if(phase==3) CHECK(bank.decoded-decoded<=AICA_SEQ_BUDGET);
        if(phase==4) CHECK(bank.validation.index-index<=AICA_SEQ_BUDGET);
        if(!result) CHECK(asq_get(&bank,0,&view)<0);
    } while(!result);
    if(result<0) {
        CHECK(asq_step(&bank)<0 && asq_get(&bank,0,&view)<0);
        free(events);puts("0");return;
    }
    CHECK(asq_step(&bank)==1);
    CHECK(asq_get(&bank,layout.programs,&view)<0);
    CHECK(asq_get(&bank,0,(aica_seq_validation_t *)(void *)&bank)<0);
    CHECK(asq_get(&bank,0,(aica_seq_validation_t *)(void *)source)<0);
    CHECK(asq_get(&bank,0,(aica_seq_validation_t *)(void *)events)<0);
    for(i=0;i<layout.programs;i++) {
        aica_sequence_t sequence;
        aica_seq_ops_t ops={retain,release,emit,silence,resume,NULL};
        unsigned k;
        CHECK(asq_get(&bank,i,&view)==0 && view.ready && !view.failed);
        checksum=hashword(checksum,view.program.count);
        checksum=hashword(checksum,view.program.ticks_per_quarter);
        checksum=hashword(checksum,view.program.tempo_us);
        for(k=0;k<view.program.count;k++) {
            const aica_seq_event_t *e=view.program.events+k;
            checksum=hashword(checksum,e->delta);checksum=hashword(checksum,e->value);
            checksum=hashword(checksum,e->auxiliary);
            checksum=hashword(checksum,(uint32_t)e->kind|((uint32_t)e->status<<8)|
                              ((uint32_t)e->a<<16)|((uint32_t)e->b<<24));
        }
        CHECK(aica_sequence_init(&sequence,&ops)==0);
        CHECK(aica_sequence_play(&sequence,&view,17)==1 && pins==1);
        CHECK(aica_sequence_pause(&sequence)==0);
        CHECK(aica_sequence_continue(&sequence)==1);
        for(k=0;k<4;k++) CHECK(aica_sequence_service(&sequence,10000)<=AICA_SEQ_BUDGET);
        aica_sequence_stop(&sequence);CHECK(pins==0);
    }
    printf("1 %u %u %08x\n",layout.programs,layout.events,checksum);
    free(events);
}
int main(void) {
    uint8_t length[4];unsigned serial=0;
    CHECK(asq_step(NULL)<0);CHECK(asq_get(NULL,0,NULL)<0);
    for(;;) {
        uint32_t size;uint8_t *allocation,*source;size_t n=fread(length,1,4,stdin);
        if(!n) break;
        CHECK(n==4);size=(uint32_t)length[0]|((uint32_t)length[1]<<8)|
            ((uint32_t)length[2]<<16)|((uint32_t)length[3]<<24);
        CHECK(size<=2*1024*1024);
        allocation=malloc((size_t)size+4);CHECK(allocation!=NULL);
        source=allocation+(serial++%4);
        CHECK(fread(source,1,size,stdin)==size);check_blob(source,size);free(allocation);
    }
    CHECK(!ferror(stdin));fprintf(stderr,"native checks: %lu\n",checks);return 0;
}
