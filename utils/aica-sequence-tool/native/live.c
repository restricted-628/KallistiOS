#include "live.h"
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static int zero(const uint32_t *p,unsigned n) { while(n--) if(*p++) return 0;return 1; }
static int equal(const uint32_t *a,const uint32_t *b) {
    for(unsigned i=0;i<8;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
static void split(volatile uint32_t *p,uint64_t n) { p[0]=(uint32_t)n;p[1]=(uint32_t)(n>>32); }
static int shape(const uint32_t *v) {
    unsigned op=v[1];const uint32_t *a=v+4;
    if(op<ASQL_BIND || op>ASQL_STOP_ALL) return 0;
    if(op==ASQL_STOP_ALL) return !v[2] && !v[3] && zero(a,4);
    if(v[2]>=8 || !v[3]) return 0;
    if(op==ASQL_PLAY || op==ASQL_CHAIN)
        return a[0]<8 && a[1] && a[2]<ASQ_MAX_PROGRAMS && a[3]<(op==ASQL_PLAY?32u:1u);
    if(op==ASQL_SPEED) return (int32_t)a[0]>=-4032 && (int32_t)a[0]<=8191 && a[1]<=32767 && zero(a+2,2);
    if(op==ASQL_BEAT) return a[0]<=1 && a[1]<=(a[0]?3u:6u) && zero(a+2,2);
    if(op==ASQL_RELEASE) return a[0]<=1 && zero(a+1,3);
    return zero(a,4);
}
static int faulted(const asql_t *s) {
    return s->faulted || s->catalog->faulted || s->music->faulted || s->music->music->faulted;
}
static uint32_t bound_generation(const asql_t *s,unsigned p,uint32_t g) {
    const aica_music_sequence_binding_t *b=&s->adapter->bindings[p];
    const aica_music_port_t *m=&s->music->music->ports[p];
    if(s->initialized && b->bound && b->generation==g && m->open && m->generation==g &&
       m->bank==b->bank && m->bank_generation==b->bank_generation)
        return g;
    return 0;
}
static int publish(asql_t *s,int result) {
    volatile asql_receipt_t *r=s->receipt;uint32_t sequence=r->sequence;unsigned p=s->request[2];
    if((sequence&1u) || sequence>UINT32_MAX-3u) { r->sequence=UINT32_MAX;s->faulted=1;return 0; }
    r->sequence=sequence+1;
    for(unsigned i=1;i<64;i++) ((volatile uint32_t *)r)[i]=0;
    r->version=ASQL_VERSION;r->ready=1;r->session=s->session;r->cookie=s->cookie;
    r->operation=s->request[1];r->result=result;s->result=result;
    r->flags=(s->initialized?1u:0u)|(faulted(s)?2u:0u)|(s->cancelled_cookie?4u:0u);
    r->cancelled_cookie=s->cancelled_cookie;r->cancelled_result=s->cancelled_result;
    for(unsigned i=0;i<8;i++) { r->request[i]=s->request[i];r->cancelled_request[i]=s->cancelled_request[i]; }
    if(s->initialized && p<8) {
        const aica_music_sequence_binding_t *b=&s->adapter->bindings[p];
        const aica_sequence_t *q=&s->adapter->sequences[p];
        const asql_program_t *c=&s->current[p],*n=&s->chained[p];
        r->bound=b->bound;r->state=q->state;r->generation=b->generation;
        r->bank=b->bank;r->bank_generation=b->bank_generation;
        r->slot=c->slot;r->resource_generation=c->generation;r->program=c->program;
        if(q->chain) { r->chain_slot=n->slot;r->chain_generation=n->generation;r->chain_program=n->program; }
        r->cursor=q->cursor;r->event_count=q->program.count;r->priority=q->priority;
        r->tempo=q->tempo;r->speed_target=q->speed_target;r->speed_from=q->speed_from;
        r->speed_elapsed=q->speed_elapsed;r->speed_duration=q->speed_duration;
        r->beat_multiply=q->beat_multiply;r->beat_shift=q->beat_shift;
        split(r->ticks,aica_sequence_ticks(q));split(r->beats,aica_sequence_beats(q));
        split(r->debt,q->pending);split(r->remaining,q->remaining);r->processed=q->processed;
        for(unsigned i=0;i<AICA_TIMED_NOTES;i++) if(s->adapter->notes[i].live && s->adapter->notes[i].port==p) ++r->notes;
        r->resuming=b->resuming;r->dropped=s->adapter->dropped;
        r->last_tick=s->clocks[p].tick;r->remainder=s->clocks[p].remainder;
    }
    r->sequence=sequence+2;return 1;
}
int asql_init(asql_t *s,aica_music_sequences_t *a,aica_music_wire_t *m,
              aica_bank_registry_t *r,asq_catalog_t *c,volatile asql_receipt_t *receipt) {
    if(!s || !a || !m || !r || !c || !receipt) return 0;
    clear(s,sizeof(*s));s->adapter=a;s->music=m;s->registry=r;s->catalog=c;s->receipt=receipt;
    for(unsigned i=0;i<64;i++) ((volatile uint32_t *)receipt)[i]=0;
    receipt->version=ASQL_VERSION;receipt->ready=1;return 1;
}
int asql_command(asql_t *s,const aica_cmd_t *p,uint32_t active) {
    const uint32_t *v;int supersede;
    if(!s || !p || p->cmd!=ASQL_COMMAND || p->size!=16 || !p->cmd_id || p->timestamp ||
       p->misc[0] || p->misc[1] || p->misc[2] || p->misc[3]) return 0;
    v=(const uint32_t *)p->cmd_data;
    if(!active || v[0]!=active || (s->session && s->session!=active) || !shape(v)) return 0;
    if(p->cmd_id==s->cookie) return equal(v,s->request);
    if(s->cancelled_cookie && p->cmd_id==s->cancelled_cookie) return equal(v,s->cancelled_request);
    if(p->cmd_id<s->cookie || s->faulted) return 0;
    supersede=s->pending && s->request[1]==ASQL_CONTINUE &&
        (v[1]==ASQL_STOP_ALL || ((v[1]==ASQL_STOP || v[1]==ASQL_UNBIND) &&
         v[2]==s->request[2] && v[3]==s->request[3]));
    if(s->pending && !supersede) return 0;
    /* Recheck urgent ownership before it is allowed to cancel the old request. */
    if(supersede && v[1]!=ASQL_STOP_ALL && !bound_generation(s,v[2],v[3])) return 0;
    if((s->receipt->sequence&1u) || s->receipt->sequence>UINT32_MAX-5u) {
        s->receipt->sequence=UINT32_MAX;s->faulted=1;return 0;
    }
    s->cancelled_cookie=0;s->cancelled_result=0;clear(s->cancelled_request,sizeof(s->cancelled_request));
    if(supersede) {
        s->cancelled_cookie=s->cookie;s->cancelled_result=ASQL_CANCELLED;
        for(unsigned i=0;i<8;i++) s->cancelled_request[i]=s->request[i];
    }
    s->session=active;s->cookie=p->cmd_id;
    for(unsigned i=0;i<8;i++) s->request[i]=v[i];
    s->pending=1;return publish(s,ASQL_PENDING);
}
static int quiet(asql_t *s,unsigned p) {
    const aica_music_t *m=s->music->music;
    if(s->music->pending) return 0;
    for(unsigned i=0;i<m->count;i++) if(m->queue[(m->head+i)%AICA_MUSIC_QUEUE].port==p) return 0;
    for(unsigned i=0;i<m->voice_count;i++) if(m->voices[i].state && m->voices[i].port==p) return 0;
    return 1;
}
int asql_stop_all(asql_t *s) {
    if(s->initialized) for(unsigned p=0;p<8;p++) if(s->adapter->bindings[p].bound) {
        if(!bound_generation(s,p,s->adapter->bindings[p].generation)) s->faulted=1;
        aica_sequence_stop(&s->adapter->sequences[p]);
    }
    return faulted(s)?ASQL_FAULT:0;
}
static int apply(asql_t *s,uint32_t tick) {
    const uint32_t *v=s->request,*a=v+4;unsigned op=v[1],p=v[2];uint32_t g=v[3];
    aica_sequence_t *q=&s->adapter->sequences[p];int result;
    if(op==ASQL_STOP_ALL) return asql_stop_all(s);
    if(op==ASQL_QUERY) return bound_generation(s,p,g)?(faulted(s)?ASQL_FAULT:0):ASQL_INVALID;
    if(op==ASQL_STOP) {
        if(!bound_generation(s,p,g)) return ASQL_INVALID;
        aica_sequence_stop(q);return faulted(s)?ASQL_FAULT:0;
    }
    if(faulted(s)) return ASQL_FAULT;
    if(op==ASQL_BIND) {
        aica_music_t *m=s->music->music;
        if(!s->music->claimed || m->quiescing || s->music->session!=s->session ||
           !m->ports[p].open || m->ports[p].generation!=g) return ASQL_INVALID;
        if((s->music->protected_ports&(1u<<p)) || !quiet(s,p)) return ASQL_BUSY;
        if(!s->initialized) {
            if(aica_music_sequences_init(s->adapter,m,s->registry,s->session,
               asq_catalog_retain,asq_catalog_release,s->catalog)) { s->faulted=1;return ASQL_FAULT; }
            s->initialized=1;
        }
        result=aica_music_sequence_bind(s->adapter,p,g);
        if(result) return result==-2?ASQL_BUSY:ASQL_INVALID;
        s->music->protected_ports|=1u<<p;s->clocks[p].tick=tick;
        s->clocks[p].remainder=s->clocks[p].paused_remainder=0;return 0;
    }
    if(!bound_generation(s,p,g)) return ASQL_INVALID;
    if(op==ASQL_UNBIND) {
        result=aica_music_sequence_unbind(s->adapter,p);
        if(result || s->catalog->faulted) { s->faulted=1;return ASQL_FAULT; }
        s->music->protected_ports&=~(1u<<p);return 0;
    }
    if(op==ASQL_PLAY || op==ASQL_CHAIN) {
        asq_lease_t lease;asql_program_t identity;
        result=asq_catalog_acquire(s->catalog,a[0],a[1],a[2],&lease);
        if(result) return result==ASQ_CATALOG_BUSY?ASQL_BUSY:ASQL_INVALID;
        identity.slot=a[0];identity.generation=a[1];identity.program=a[2];identity.events=lease.validation.program.events;
        result=op==ASQL_PLAY?aica_sequence_play(q,&lease.validation,a[3]):aica_sequence_chain(q,&lease.validation);
        if(asq_catalog_return(s->catalog,&lease)) { s->faulted=1;return ASQL_FAULT; }
        if(op==ASQL_PLAY && result>0) {
            s->current[p]=identity;clear(&s->chained[p],sizeof(s->chained[p]));
            s->clocks[p].tick=tick;s->clocks[p].remainder=s->clocks[p].paused_remainder=0;return 0;
        }
        if(op==ASQL_CHAIN && !result) { s->chained[p]=identity;return 0; }
        if(op==ASQL_PLAY && !result) return ASQL_DECLINED;
        return result==-2?ASQL_BUSY:ASQL_INVALID;
    }
    if(op==ASQL_PAUSE) {
        result=aica_sequence_pause(q);
        if(!result) s->clocks[p].paused_remainder=s->clocks[p].remainder;
        return result?ASQL_INVALID:0;
    }
    if(op==ASQL_CONTINUE) {
        result=aica_sequence_continue(q);
        if(result>0) { s->clocks[p].tick=tick;return 0; }
        if(q->state==AICA_SEQ_FAULT) { s->faulted=1;return ASQL_FAULT; }
        return result<0?ASQL_INVALID:ASQL_PENDING;
    }
    if(op==ASQL_LOOP_EXIT) return aica_sequence_chain(q,NULL)?ASQL_INVALID:0;
    if(op==ASQL_SPEED) return aica_sequence_speed(q,(int32_t)a[0],a[1])?ASQL_INVALID:0;
    if(op==ASQL_BEAT) return aica_sequence_beat(q,a[0],a[1])?ASQL_INVALID:0;
    if(op==ASQL_RELEASE) { q->release_mode=(uint8_t)a[0];return 0; }
    return ASQL_INVALID;
}
static void advance(asql_t *s,unsigned p,uint32_t tick) {
    asql_clock_t *c=&s->clocks[p];aica_sequence_t *q=&s->adapter->sequences[p];
    uint32_t delta=tick-c->tick,*fraction=q->state==AICA_SEQ_RUNNING?&c->remainder:&c->paused_remainder;
    uint64_t numerator=(uint64_t)delta*100000u+*fraction,us=numerator/441u;
    if(!bound_generation(s,p,s->adapter->bindings[p].generation)) { s->faulted=1;asql_stop_all(s);return; }
    c->tick=tick;
    if(us>UINT32_MAX) { s->faulted=1;asql_stop_all(s);return; }
    *fraction=(uint32_t)(numerator%441u);
    aica_sequence_service(q,(uint32_t)us);
    if(s->chained[p].events && q->program.events==s->chained[p].events && !q->chain) {
        s->current[p]=s->chained[p];clear(&s->chained[p],sizeof(s->chained[p]));
    }
    if(q->state==AICA_SEQ_FAULT || faulted(s)) { s->faulted=1;asql_stop_all(s); }
}
void asql_service(asql_t *s,uint32_t tick) {
    unsigned op;int result;
    if(!s || s->faulted) return;
    op=s->request[1];
    if(s->pending && (op==ASQL_STOP || op==ASQL_STOP_ALL || op==ASQL_UNBIND)) {
        result=apply(s,tick);s->pending=0;publish(s,result);
    }
    if(s->initialized && !faulted(s)) {
        unsigned first=s->next_port;
        /* Every port gets first admission once per eight passes. A saturated
           earlier producer cannot consume every newly available MIDI slot. */
        s->next_port=(uint8_t)((first+1u)&7u);
        for(unsigned i=0;i<8 && !s->faulted;i++) {
            unsigned p=(first+i)&7u;
            if(s->adapter->bindings[p].bound) advance(s,p,tick);
        }
    }
    if(s->pending) {
        result=apply(s,tick);
        if(result!=ASQL_PENDING) { s->pending=0;publish(s,result); }
        /* CONTINUE progress is visible only through its eventual coherent result;
           don't spend the reserved terminal receipt sequence on repeated polls. */
    }
    else if(s->faulted && !(s->receipt->flags&2u)) publish(s,ASQL_FAULT);
}
