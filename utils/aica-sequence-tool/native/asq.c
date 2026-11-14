#include "asq.h"
#include <string.h>
#include <limits.h>
_Static_assert(sizeof(aica_seq_event_t)==16,"native event size");
_Static_assert(offsetof(aica_seq_event_t,kind)==12,"native event fields");
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int span(const void *p,size_t n) {
    return p && n && n<=UINTPTR_MAX-(uintptr_t)p;
}
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+bn && y<x+an;
}
static int zeros(const uint8_t *p,unsigned n) {
    while(n--) if(*p++) return 0;
    return 1;
}
int asq_measure(const void *source,size_t bytes,asq_layout_t *result) {
    const uint8_t *p=source;
    asq_layout_t out;
    uint32_t end;
    if(!span(source,bytes) || bytes<64 || bytes>UINT32_MAX ||
       !span(result,sizeof(*result)) || (uintptr_t)result%_Alignof(asq_layout_t) ||
       overlap(source,bytes,result,sizeof(*result))) return -1;
    if(memcmp(p,"KOSASQ\0\0",8) || p[8]!=1 || p[9] || p[10]!=64 || p[11]) return -1;
    out.size=u32(p+12);out.programs=u32(p+16);out.events=u32(p+20);
    out.event_offset=u32(p+28);
    if(out.size!=bytes || !out.programs || out.programs>ASQ_MAX_PROGRAMS ||
       !out.events || out.events>ASQ_MAX_EVENTS || out.events<out.programs ||
       u32(p+24)!=64 || out.event_offset!=64+out.programs*32 ||
       !zeros(p+36,28)) return -1;
    /* All arithmetic is bounded by the checked counts, before any body read. */
    end=out.event_offset+out.events*16;
    if(out.size!=((end+31)&~31u) || !zeros(p+end,out.size-end)) return -1;
    *result=out;return 0;
}
int asq_begin(asq_bank_t *b,const void *source,size_t bytes,
              aica_seq_event_t *storage,size_t capacity) {
    asq_layout_t layout;
    size_t used;
    if(asq_measure(source,bytes,&layout)<0 || capacity<layout.events) return -1;
    used=(size_t)layout.events*sizeof(*storage);
    if(!span(b,sizeof(*b)) || (uintptr_t)b%_Alignof(asq_bank_t) ||
       !span(storage,used) || (uintptr_t)storage%_Alignof(aica_seq_event_t) ||
       overlap(b,sizeof(*b),source,bytes) || overlap(b,sizeof(*b),storage,used) ||
       overlap(source,bytes,storage,used)) return -1;
    memset(b,0,sizeof(*b));b->source=source;b->events=storage;b->layout=layout;
    b->crc=UINT32_MAX;b->crc_offset=64;b->phase=1;return 0;
}
int asq_step(asq_bank_t *b) {
    unsigned budget;
    if(!b || !b->phase || b->phase>=6) return -1;
    if(b->phase==5) return 1;
    if(b->phase==1) {
        for(budget=0;budget<ASQ_CRC_BUDGET && b->crc_offset<b->layout.size;budget++) {
            unsigned bit;b->crc^=b->source[b->crc_offset++];
            for(bit=0;bit<8;bit++) b->crc=(b->crc>>1)^(0xedb88320u & (0u-(b->crc&1)));
        }
        if(b->crc_offset==b->layout.size) {
            if((b->crc^UINT32_MAX)!=u32(b->source+32)) goto invalid;
            b->phase=2;
        }
        return 0;
    }
    if(b->phase==2) {
        const uint8_t *d=b->source+64+b->program*32;
        aica_seq_program_t program;
        b->count=u32(d+4);
        if(u32(d)!=b->layout.event_offset+b->first*16 || !b->count ||
           b->count>b->layout.events-b->first || !zeros(d+16,16)) goto invalid;
        program.events=b->events+b->first;program.count=b->count;
        program.ticks_per_quarter=u32(d+8);program.tempo_us=u32(d+12);
        if(aica_seq_validate_begin(&b->validation,&program)<0) goto invalid;
        b->decoded=0;b->phase=3;return 0;
    }
    if(b->phase==3) {
        for(budget=0;budget<AICA_SEQ_BUDGET && b->decoded<b->count;budget++) {
            uint32_t index=b->first+b->decoded++;
            const uint8_t *p=b->source+b->layout.event_offset+index*16;
            aica_seq_event_t *e=b->events+index;
            e->delta=u32(p);e->value=u32(p+4);e->auxiliary=u32(p+8);
            e->kind=p[12];e->status=p[13];e->a=p[14];e->b=p[15];
        }
        if(b->decoded==b->count) b->phase=4;
        return 0;
    }
    if(b->phase==4) {
        int result=aica_seq_validate_step(&b->validation);
        if(result<0) goto invalid;
        if(!result) return 0;
        b->first+=b->count;
        if(++b->program==b->layout.programs) {
            if(b->first!=b->layout.events) goto invalid;
            b->phase=5;return 1;
        }
        b->phase=2;return 0;
    }
invalid:b->phase=6;return -1;
}
int asq_get(const asq_bank_t *b,unsigned index,aica_seq_validation_t *result) {
    aica_seq_program_t p;
    const uint8_t *d;
    if(!b || b->phase!=5 || index>=b->layout.programs ||
       !span(result,sizeof(*result)) || (uintptr_t)result%_Alignof(aica_seq_validation_t) ||
       overlap(b,sizeof(*b),result,sizeof(*result)) ||
       overlap(b->source,b->layout.size,result,sizeof(*result)) ||
       overlap(b->events,(size_t)b->layout.events*sizeof(*b->events),result,sizeof(*result))) return -1;
    d=b->source+64+index*32;
    p.events=b->events+(u32(d)-b->layout.event_offset)/16;
    p.count=u32(d+4);p.ticks_per_quarter=u32(d+8);p.tempo_us=u32(d+12);
    if(aica_seq_validate_begin(result,&p)<0) return -1;
    result->index=p.count;result->ready=1;return 0;
}
