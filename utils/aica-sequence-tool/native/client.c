#include "client.h"
#include <string.h>
static int lost(asq_client_t *c) { c->state=ASQC_LOST;return -1; }
static int zero(const uint32_t *p,unsigned n) { while(n--) if(*p++) return 0;return 1; }
static int session(asq_client_t *c) {
    uint32_t active;
    if(c->ops.session(c->ops.context,&active)<0) return 0;
    return active==c->session?1:lost(c);
}
static int valid(unsigned op,unsigned slot,uint32_t g,const uint32_t *a) {
    if(op<1 || op>3 || slot>=8 || !g || !a) return 0;
    if(op!=ASQ_WIRE_PREPARE) return zero(a,4);
    return a[0]>=0x30000 && a[0]<0x200000 && !(a[0]&31u) && a[1]>=64 &&
        !(a[1]&31u) && a[1]<=0x200000-a[0] && a[2]>=0x30000 && a[2]<0x200000 &&
        !(a[2]&31u) && a[3] && a[3]<=ASQ_MAX_EVENTS && a[3]<=((0x200000-a[2])/16u);
}
int asq_client_init(asq_client_t *c,uint32_t s,const asq_client_ops_t *ops) {
    asq_client_ops_t copy;
    if(!c || !s || !ops || !ops->read || !ops->submit || !ops->session) return -1;
    copy=*ops;memset(c,0,sizeof(*c));c->ops=copy;c->session=s;c->state=ASQC_SYNC;return 0;
}
int asq_client_retry(asq_client_t *c) {
    int result;
    if(!c || c->state!=ASQC_WAIT) return -1;
    result=session(c);if(result<=0) return result;
    result=c->ops.submit(c->ops.context,c->packet);return result>0?1:0;
}
static int prepare(asq_client_t *c,unsigned op,unsigned slot,uint32_t g,const uint32_t *args) {
    if(c->cookie==UINT32_MAX) return lost(c);
    memset(c->superseded,0,sizeof(c->superseded));
    if(op==ASQ_WIRE_DROP && c->packet[9]==ASQ_WIRE_PREPARE &&
       c->packet[10]==slot && c->packet[11]==g) memcpy(c->superseded,c->packet,64);
    memset(c->packet,0,64);c->packet[0]=16;c->packet[1]=ASQ_WIRE_COMMAND;
    c->packet[3]=++c->cookie;c->packet[8]=c->session;c->packet[9]=op;
    c->packet[10]=slot;c->packet[11]=g;memcpy(c->packet+12,args,16);c->state=ASQC_WAIT;
    return asq_client_retry(c);
}
int asq_client_begin(asq_client_t *c,unsigned op,unsigned slot,uint32_t g,const uint32_t args[4]) {
    uint32_t copy[4];
    if(!c || c->state!=ASQC_IDLE || !valid(op,slot,g,args)) return -1;
    memcpy(copy,args,16);return prepare(c,op,slot,g,copy);
}
int asq_client_cancel(asq_client_t *c) {
    uint32_t args[4]={0};
    if(!c || c->state!=ASQC_WAIT) return -1;
    if(c->packet[9]==ASQ_WIRE_DROP) return asq_client_retry(c);
    if(c->packet[9]!=ASQ_WIRE_PREPARE || c->receipt.cookie!=c->cookie ||
       !(c->receipt.flags&ASQ_WIRE_ADMITTED)) return -1;
    return prepare(c,ASQ_WIRE_DROP,c->packet[10],c->packet[11],args);
}
static int shape(const asq_receipt_t *r) {
    if(r->version!=ASQ_WIRE_VERSION || r->ready!=1 || (r->flags&~7u) ||
       !zero(r->reserved,28) || r->result>1 || r->result<ASQ_CATALOG_FAULT ||
       r->state>ASQ_CATALOG_FAILED || r->borrowed>ASQ_CATALOG_BORROWS ||
       r->programs>ASQ_MAX_PROGRAMS || r->event_count>ASQ_MAX_EVENTS || r->capacity>ASQ_MAX_EVENTS) return 0;
    if(r->state) {
        uint32_t args[4]={r->source,r->source_bytes,r->events,r->capacity};
        if(!r->generation || !r->programs || !r->event_count || r->event_count>r->capacity ||
           !valid(ASQ_WIRE_PREPARE,0,r->generation,args) ||
           r->source_bytes!=((64u+r->programs*32u+r->event_count*16u+31u)&~31u) ||
           (r->source<r->events+r->capacity*16u && r->events<r->source+r->source_bytes)) return 0;
    }
    else if(r->borrowed || r->playing || r->source || r->source_bytes || r->events ||
            r->capacity || r->programs || r->event_count) return 0;
    if(r->superseded_cookie) {
        if(!(r->flags&ASQ_WIRE_SUPERSEDED) || r->operation!=ASQ_WIRE_DROP ||
           r->superseded_cookie>=r->cookie || r->superseded_result>0 ||
           r->superseded_result<ASQ_WIRE_CANCELLED || r->superseded_request[0]!=r->session ||
           r->superseded_request[1]!=ASQ_WIRE_PREPARE) return 0;
    }
    else if((r->flags&ASQ_WIRE_SUPERSEDED) || r->superseded_result || !zero(r->superseded_request,8)) return 0;
    return 1;
}
int asq_client_poll(asq_client_t *c) {
    asq_receipt_t r;uint32_t before,after,words[64];int result;
    if(!c || (c->state!=ASQC_SYNC && c->state!=ASQC_WAIT && c->state!=ASQC_DONE)) return -1;
    if(c->state==ASQC_DONE) return 1;
    result=session(c);if(result<=0) return result;
    if(c->ops.read(c->ops.context,&before,1)<0) return 0;
    if(before==UINT32_MAX) return lost(c);
    if(before&1u) return 0;
    if(c->ops.read(c->ops.context,words,64)<0 || c->ops.read(c->ops.context,&after,1)<0) return 0;
    if(before!=after || words[0]!=before) return 0;
    memcpy(&r,words,256);result=session(c);if(result<=0) return result;
    if(!shape(&r) || (r.flags&ASQ_WIRE_FAULTED)) return lost(c);
    if(c->state==ASQC_SYNC) {
        asq_receipt_t empty={0};empty.version=ASQ_WIRE_VERSION;empty.ready=1;
        if(memcmp(&r,&empty,256)) return lost(c);
        c->receipt=r;c->state=ASQC_IDLE;return 1;
    }
    if(r.sequence<c->receipt.sequence || r.cookie<c->receipt.cookie || r.cookie>c->cookie ||
       (r.sequence==c->receipt.sequence && memcmp(&r,&c->receipt,256)) ||
       (r.session!=c->session && (r.session || r.cookie || r.sequence))) return lost(c);
    if(r.cookie<c->cookie) { c->receipt=r;return 0; }
    if(r.session!=c->session || r.operation!=c->packet[9] || memcmp(r.request,c->packet+8,32)) return lost(c);
    if(r.superseded_cookie && (r.superseded_cookie!=c->superseded[3] ||
       memcmp(r.superseded_request,c->superseded+8,32))) return lost(c);
    if(r.flags&ASQ_WIRE_ADMITTED) {
        if(r.operation!=ASQ_WIRE_PREPARE || r.generation!=c->packet[11] ||
           r.source!=c->packet[12] || r.source_bytes!=c->packet[13] ||
           r.events!=c->packet[14] || r.capacity!=c->packet[15] ||
           (r.result<0 && r.state!=ASQ_CATALOG_FAILED)) return lost(c);
    }
    if(r.result==ASQ_WIRE_PENDING && (!(r.flags&ASQ_WIRE_ADMITTED) || r.state!=ASQ_CATALOG_PREPARING)) return lost(c);
    if(r.result==0) {
        if(r.generation!=c->packet[11]) return lost(c);
        if(r.operation==ASQ_WIRE_PREPARE && (!(r.flags&ASQ_WIRE_ADMITTED) || r.state!=ASQ_CATALOG_READY)) return lost(c);
        if(r.operation==ASQ_WIRE_DROP && r.state!=ASQ_CATALOG_EMPTY) return lost(c);
    }
    c->receipt=r;
    if(r.result==ASQ_WIRE_PENDING) return 0;
    c->state=ASQC_DONE;return 1;
}
int asq_client_consume(asq_client_t *c) {
    if(!c || c->state!=ASQC_DONE) return -1;
    c->state=ASQC_IDLE;return 0;
}
