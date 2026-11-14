#include "catalog.h"
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static int span(const void *p,size_t n) {
    return p && n && n<=UINTPTR_MAX-(uintptr_t)p;
}
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+bn && y<x+an;
}
static asq_resource_t *resource(asq_catalog_t *c,unsigned slot,uint32_t g) {
    if(!c || slot>=ASQ_CATALOG_SLOTS || !g || !c->slots[slot].state ||
       c->slots[slot].generation!=g) return NULL;
    return &c->slots[slot];
}
/* Caller already checked the candidate span cannot wrap. */
static int available(const asq_catalog_t *c,const void *p,size_t n) {
    unsigned i;
    if(overlap(c,sizeof(*c),p,n)) return 0;
    for(i=0;i<ASQ_CATALOG_SLOTS;i++) {
        const asq_resource_t *r=&c->slots[i];
        if(r->state && (overlap(r->bank.source,r->bank.layout.size,p,n) ||
           overlap(r->bank.events,(size_t)r->bank.layout.events*sizeof(*r->bank.events),p,n))) return 0;
    }
    for(i=0;i<ASQ_CATALOG_BORROWS;i++)
        if(c->borrowers[i] && overlap(c->borrowers[i],sizeof(asq_lease_t),p,n)) return 0;
    return 1;
}
int asq_catalog_init(asq_catalog_t *c) {
    if(!c) return ASQ_CATALOG_INVALID;
    clear(c,sizeof(*c));return 0;
}
int asq_catalog_begin(asq_catalog_t *c,unsigned slot,uint32_t g,
    const void *source,size_t bytes,aica_seq_event_t *storage,size_t capacity) {
    asq_layout_t layout;asq_resource_t *r;size_t used;
    if(!c || slot>=ASQ_CATALOG_SLOTS || !g) return ASQ_CATALOG_INVALID;
    if(c->faulted) return ASQ_CATALOG_FAULT;
    r=&c->slots[slot];
    if(r->state) return ASQ_CATALOG_BUSY;
    if(r->generation==UINT32_MAX || g!=r->generation+1 ||
       asq_measure(source,bytes,&layout)<0 || capacity<layout.events) return ASQ_CATALOG_INVALID;
    used=(size_t)layout.events*sizeof(*storage);
    if(!span(storage,used) || !available(c,source,bytes) || !available(c,storage,used) ||
       asq_begin(&r->bank,source,bytes,storage,capacity)<0) return ASQ_CATALOG_INVALID;
    r->generation=g;r->state=ASQ_CATALOG_PREPARING;return 0;
}
int asq_catalog_step(asq_catalog_t *c,unsigned slot,uint32_t g) {
    asq_resource_t *r=resource(c,slot,g);int result;
    if(!r) return ASQ_CATALOG_INVALID;
    if(c->faulted) return ASQ_CATALOG_FAULT;
    if(r->state==ASQ_CATALOG_READY) return 1;
    if(r->state!=ASQ_CATALOG_PREPARING) return ASQ_CATALOG_INVALID;
    result=asq_step(&r->bank);
    if(result) r->state=result>0?ASQ_CATALOG_READY:ASQ_CATALOG_FAILED;
    return result;
}
int asq_catalog_drop(asq_catalog_t *c,unsigned slot,uint32_t g) {
    asq_resource_t *r=resource(c,slot,g);
    if(!r) return ASQ_CATALOG_INVALID;
    if(c->faulted) return ASQ_CATALOG_FAULT;
    if(r->borrowed || r->playing) return ASQ_CATALOG_BUSY;
    clear(r,sizeof(*r));r->generation=g;return 0;
}
int asq_catalog_acquire(asq_catalog_t *c,unsigned slot,uint32_t g,
    unsigned program,asq_lease_t *lease) {
    asq_resource_t *r=resource(c,slot,g);unsigned i;
    if(!r || r->state!=ASQ_CATALOG_READY || !span(lease,sizeof(*lease)) ||
       (uintptr_t)lease%_Alignof(asq_lease_t)) return ASQ_CATALOG_INVALID;
    if(c->faulted) return ASQ_CATALOG_FAULT;
    if(!available(c,lease,sizeof(*lease))) return ASQ_CATALOG_INVALID;
    for(i=0;i<ASQ_CATALOG_BORROWS && c->borrowers[i];i++) {}
    if(i==ASQ_CATALOG_BORROWS) return ASQ_CATALOG_BUSY;
    if(asq_get(&r->bank,program,&lease->validation)<0) return ASQ_CATALOG_INVALID;
    lease->slot=slot;lease->generation=g;c->borrowers[i]=lease;++r->borrowed;return 0;
}
int asq_catalog_return(asq_catalog_t *c,asq_lease_t *lease) {
    unsigned i;asq_resource_t *r;
    if(!c || !lease) return ASQ_CATALOG_INVALID;
    for(i=0;i<ASQ_CATALOG_BORROWS && c->borrowers[i]!=lease;i++) {}
    if(i==ASQ_CATALOG_BORROWS) return ASQ_CATALOG_INVALID;
    r=resource(c,lease->slot,lease->generation);
    if(!r || !r->borrowed) { c->faulted=1;return ASQ_CATALOG_FAULT; }
    --r->borrowed;c->borrowers[i]=NULL;clear(lease,sizeof(*lease));return 0;
}
static int same(const aica_seq_program_t *a,const aica_seq_program_t *b) {
    return a->events==b->events && a->count==b->count &&
        a->ticks_per_quarter==b->ticks_per_quarter && a->tempo_us==b->tempo_us;
}
int asq_catalog_retain(void *context,const aica_seq_program_t *p) {
    asq_catalog_t *c=context;unsigned i;
    if(!c || !p || c->faulted) return 0;
    for(i=0;i<ASQ_CATALOG_BORROWS;i++) {
        const asq_lease_t *b=c->borrowers[i];asq_resource_t *r;
        if(!b || !same(p,&b->validation.program)) continue;
        r=resource(c,b->slot,b->generation);
        if(!r || r->state!=ASQ_CATALOG_READY || r->playing==UINT32_MAX) return 0;
        ++r->playing;return 1;
    }
    return 0;
}
void asq_catalog_release(void *context,const aica_seq_program_t *p) {
    asq_catalog_t *c=context;unsigned i,j;
    if(!c) return;
    if(p) for(i=0;i<ASQ_CATALOG_SLOTS;i++) {
        asq_resource_t *r=&c->slots[i];aica_seq_validation_t v;
        uintptr_t address=(uintptr_t)p->events,start=(uintptr_t)r->bank.events;
        if(r->state!=ASQ_CATALOG_READY || !r->playing || address<start ||
           address-start>=(size_t)r->bank.layout.events*sizeof(*r->bank.events)) continue;
        /* At most128 small directory lookups in the one matching resource. */
        for(j=0;j<r->bank.layout.programs;j++) {
            if(asq_get(&r->bank,j,&v)==0 && same(p,&v.program)) { --r->playing;return; }
        }
        break;
    }
    c->faulted=1;
}
