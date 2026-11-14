#include "wire.h"
#include <stddef.h>
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static int equal(const uint32_t *a,const uint32_t *b) {
    for(unsigned i=0;i<8;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
static int zero(const uint32_t *a) { return !(a[0]|a[1]|a[2]|a[3]); }
static int span(uint32_t address,uint32_t bytes) {
    return address>=0x30000u && address<0x200000u && bytes &&
        !(address&31u) && bytes<=0x200000u-address;
}
static int publish(asq_wire_t *w,int result) {
    volatile asq_receipt_t *r=w->receipt;
    const asq_resource_t *s=&w->catalog->slots[w->request[2]];
    const uint32_t *a=w->spans[w->request[2]];
    uint32_t sequence=r->sequence;unsigned i;
    if((sequence&1u) || sequence>UINT32_MAX-3u) {
        r->sequence=UINT32_MAX;w->faulted=1;return 0;
    }
    r->sequence=sequence+1;
    for(i=1;i<64;i++) ((volatile uint32_t *)r)[i]=0;
    r->version=ASQ_WIRE_VERSION;r->ready=1;r->session=w->session;
    r->cookie=w->cookie;r->operation=w->request[1];r->result=result;w->result=result;
    r->flags=(w->admitted?ASQ_WIRE_ADMITTED:0u)|
        ((w->faulted || w->catalog->faulted)?ASQ_WIRE_FAULTED:0u)|
        (w->superseded_cookie?ASQ_WIRE_SUPERSEDED:0u);
    for(i=0;i<8;i++) { r->request[i]=w->request[i];r->superseded_request[i]=w->superseded_request[i]; }
    r->generation=s->generation;r->state=s->state;r->borrowed=s->borrowed;r->playing=s->playing;
    r->source=a[0];r->source_bytes=a[1];r->events=a[2];r->capacity=a[3];
    r->programs=s->bank.layout.programs;r->event_count=s->bank.layout.events;
    r->superseded_cookie=w->superseded_cookie;r->superseded_result=w->superseded_result;
    r->sequence=sequence+2;return 1;
}
int asq_wire_init(asq_wire_t *w,asq_catalog_t *catalog,volatile asq_receipt_t *r,
    void *(*map)(void *,uint32_t,uint32_t,int),void *context) {
    if(!w || !catalog || !r || !map) return 0;
    clear(w,sizeof(*w));w->catalog=catalog;w->receipt=r;w->map=map;w->context=context;
    for(unsigned i=0;i<64;i++) ((volatile uint32_t *)r)[i]=0;
    r->version=ASQ_WIRE_VERSION;r->ready=1;return 1;
}
static int apply(asq_wire_t *w) {
    const uint32_t *v=w->request,*a=v+4;unsigned slot=v[2];uint32_t generation=v[3];
    int result;
    if(w->faulted || w->catalog->faulted) return ASQ_CATALOG_FAULT;
    if(v[1]==ASQ_WIRE_QUERY) {
        if(!zero(a) || w->catalog->slots[slot].generation!=generation) return ASQ_CATALOG_INVALID;
        return 0;
    }
    if(v[1]==ASQ_WIRE_DROP) {
        if(!zero(a)) return ASQ_CATALOG_INVALID;
        result=asq_catalog_drop(w->catalog,slot,generation);
        if(!result) clear(w->spans[slot],sizeof(w->spans[slot]));
        return result;
    }
    if(!span(a[0],a[1]) || (a[1]&31u) || a[1]<64 || !a[3] ||
       a[3]>ASQ_MAX_EVENTS || !span(a[2],a[3]*16u)) return ASQ_CATALOG_INVALID;
    /* Checked capacity makes multiplication safe; catalog rejects whole-span
       aliasing and occupied slots before any decode or publication. */
    {
        const void *source=w->map(w->context,a[0],a[1],0);
        aica_seq_event_t *events=w->map(w->context,a[2],a[3]*16u,1);
        if(!source || !events) return ASQ_CATALOG_INVALID;
        result=asq_catalog_begin(w->catalog,slot,generation,source,a[1],events,a[3]);
    }
    if(result) return result; /* No NEW references acquired by this request. */
    for(unsigned i=0;i<4;i++) w->spans[slot][i]=a[i];
    w->admitted=1;w->pending=1;return ASQ_WIRE_PENDING;
}
int asq_wire_command(asq_wire_t *w,const aica_cmd_t *p,uint32_t active) {
    const uint32_t *v;unsigned op;int supersede;
    if(!w || !p || p->cmd!=ASQ_WIRE_COMMAND || p->size!=16 || !p->cmd_id ||
       p->timestamp || p->misc[0] || p->misc[1] || p->misc[2] || p->misc[3]) return 0;
    v=(const uint32_t *)p->cmd_data;op=v[1];
    if(!active || v[0]!=active || (w->session && w->session!=active) ||
       op<ASQ_WIRE_PREPARE || op>ASQ_WIRE_DROP || v[2]>=ASQ_CATALOG_SLOTS || !v[3]) return 0;
    if(p->cmd_id==w->cookie) return equal(v,w->request);
    if(w->superseded_cookie && p->cmd_id==w->superseded_cookie)
        return equal(v,w->superseded_request); /* Tombstone; never re-admit. */
    if(p->cmd_id<w->cookie) return 0;
    supersede=op==ASQ_WIRE_DROP && w->request[1]==ASQ_WIRE_PREPARE &&
        v[2]==w->request[2] && v[3]==w->request[3] && zero(v+4);
    if(w->pending && !supersede) return 0;
    if(w->faulted) return 0;
    /* Reserve PENDING plus terminal publication before acquiring any spans. */
    if((w->receipt->sequence&1u) || w->receipt->sequence>UINT32_MAX-(op==ASQ_WIRE_PREPARE?5u:3u)) {
        w->receipt->sequence=UINT32_MAX;w->faulted=1;return 0;
    }
    w->superseded_cookie=0;w->superseded_result=0;
    clear(w->superseded_request,sizeof(w->superseded_request));
    if(supersede) {
        w->superseded_cookie=w->cookie;
        w->superseded_result=w->pending?ASQ_WIRE_CANCELLED:w->result;
        for(unsigned i=0;i<8;i++) w->superseded_request[i]=w->request[i];
    }
    w->pending=0;w->admitted=0;w->session=active;w->cookie=p->cmd_id;
    for(unsigned i=0;i<8;i++) w->request[i]=v[i];
    return publish(w,apply(w));
}
void asq_wire_service(asq_wire_t *w) {
    int result;
    if(!w || !w->pending || w->faulted) return;
    result=asq_catalog_step(w->catalog,w->request[2],w->request[3]);
    if(!result) return;
    w->pending=0;publish(w,result>0?0:result);
}
