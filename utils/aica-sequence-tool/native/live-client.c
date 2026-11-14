#include "live-client.h"
#include <string.h>
static int lost(asql_client_t *c) { c->state=ASQC_LOST;return -1; }
static int zero(const uint32_t *p,unsigned n) { while(n--) if(*p++) return 0;return 1; }
static int session(asql_client_t *c) {
    uint32_t active;if(c->ops.session(c->ops.context,&active)<0) return 0;
    return active==c->session?1:lost(c);
}
static int valid(unsigned op,unsigned p,uint32_t g,const uint32_t *a) {
    if(!a || op<ASQL_BIND || op>ASQL_STOP_ALL) return 0;
    if(op==ASQL_STOP_ALL) return !p && !g && zero(a,4);
    if(p>=8 || !g) return 0;
    if(op==ASQL_PLAY || op==ASQL_CHAIN) return a[0]<8 && a[1] && a[2]<128 && a[3]<(op==ASQL_PLAY?32u:1u);
    if(op==ASQL_SPEED) return (int32_t)a[0]>=-4032 && (int32_t)a[0]<=8191 && a[1]<=32767 && zero(a+2,2);
    if(op==ASQL_BEAT) return a[0]<=1 && a[1]<=(a[0]?3u:6u) && zero(a+2,2);
    if(op==ASQL_RELEASE) return a[0]<=1 && zero(a+1,3);
    return zero(a,4);
}
int asql_client_init(asql_client_t *c,uint32_t s,const asq_client_ops_t *ops) {
    asq_client_ops_t copy;
    if(!c || !s || !ops || !ops->read || !ops->submit || !ops->session) return -1;
    copy=*ops;memset(c,0,sizeof(*c));c->ops=copy;c->session=s;c->state=ASQC_SYNC;return 0;
}
int asql_client_retry(asql_client_t *c) {
    int rc;if(!c || c->state!=ASQC_WAIT) return -1;
    rc=session(c);if(rc<=0) return rc;
    return c->ops.submit(c->ops.context,c->packet)>0?1:0;
}
static int begin(asql_client_t *c,unsigned op,unsigned p,uint32_t g,const uint32_t *args) {
    uint32_t copy[4];memcpy(copy,args,16);
    if(c->cookie==UINT32_MAX) return lost(c);
    memset(c->superseded,0,64);
    if(c->state==ASQC_WAIT) memcpy(c->superseded,c->packet,64);
    memset(c->packet,0,64);c->packet[0]=16;c->packet[1]=ASQL_COMMAND;c->packet[3]=++c->cookie;
    c->packet[8]=c->session;c->packet[9]=op;c->packet[10]=p;c->packet[11]=g;
    memcpy(c->packet+12,copy,16);c->state=ASQC_WAIT;return asql_client_retry(c);
}
int asql_client_begin(asql_client_t *c,unsigned op,unsigned p,uint32_t g,const uint32_t a[4]) {
    if(!c || c->state!=ASQC_IDLE || !valid(op,p,g,a)) return -1;
    return begin(c,op,p,g,a);
}
int asql_client_cancel(asql_client_t *c,int all) {
    const uint32_t args[4]={0};
    if(!c || c->state!=ASQC_WAIT || (all!=0 && all!=1)) return -1;
    if(c->packet[9]==ASQL_STOP || c->packet[9]==ASQL_STOP_ALL) return asql_client_retry(c);
    if(c->packet[9]!=ASQL_CONTINUE || c->receipt.cookie!=c->cookie || c->receipt.result!=ASQL_PENDING) return -1;
    return begin(c,all?ASQL_STOP_ALL:ASQL_STOP,all?0:c->packet[10],all?0:c->packet[11],args);
}
static int shape(const asql_receipt_t *r) {
    if(r->version!=ASQL_VERSION || r->ready!=1 || (r->flags&~7u) || !zero(r->reserved,3) ||
       r->result<ASQL_FAULT || r->result>ASQL_DECLINED || r->bound>1 || r->state>AICA_SEQ_FAULT ||
       r->slot>=8 || r->program>=128 || r->chain_slot>=8 || r->chain_program>=128 ||
       r->cursor>r->event_count || r->event_count>65536 || r->priority>31 ||
       r->notes>64 || r->resuming>1 || r->remainder>=441 || r->beat_multiply>1 ||
       r->beat_shift>(r->beat_multiply?3u:6u) || r->speed_elapsed>r->speed_duration) return 0;
    if(r->bound && (!r->generation || !r->bank_generation || r->bank>=128)) return 0;
    if(r->cancelled_cookie) {
        const uint32_t *v=r->cancelled_request;
        if(!(r->flags&4u) || r->cancelled_cookie>=r->cookie || r->cancelled_result!=ASQL_CANCELLED ||
           v[0]!=r->session || v[1]!=ASQL_CONTINUE || !valid(v[1],v[2],v[3],v+4) ||
           (r->operation!=ASQL_STOP_ALL && r->operation!=ASQL_STOP && r->operation!=ASQL_UNBIND)) return 0;
    }
    else if((r->flags&4u) || r->cancelled_result || !zero(r->cancelled_request,8)) return 0;
    return 1;
}
int asql_client_poll(asql_client_t *c) {
    asql_receipt_t r;uint32_t before,after,words[64];int rc;
    if(!c || (c->state!=ASQC_SYNC && c->state!=ASQC_WAIT && c->state!=ASQC_DONE)) return -1;
    if(c->state==ASQC_DONE) return 1;
    rc=session(c);if(rc<=0) return rc;
    if(c->ops.read(c->ops.context,&before,1)<0) return 0;
    if(before==UINT32_MAX) return lost(c);
    if(before&1u) return 0;
    if(c->ops.read(c->ops.context,words,64)<0 || c->ops.read(c->ops.context,&after,1)<0) return 0;
    if(before!=after || words[0]!=before) return 0;
    memcpy(&r,words,256);rc=session(c);if(rc<=0) return rc;
    if(!shape(&r) || (r.flags&2u)) return lost(c);
    if(c->state==ASQC_SYNC) {
        asql_receipt_t empty={0};empty.version=ASQL_VERSION;empty.ready=1;
        if(memcmp(&r,&empty,256)) return lost(c);
        c->receipt=r;c->state=ASQC_IDLE;return 1;
    }
    if(r.sequence<c->receipt.sequence || r.cookie<c->receipt.cookie || r.cookie>c->cookie ||
       (r.sequence==c->receipt.sequence && memcmp(&r,&c->receipt,256)) ||
       (r.session!=c->session && (r.session || r.cookie || r.sequence))) return lost(c);
    if(r.cookie<c->cookie) { c->receipt=r;return 0; }
    if(r.session!=c->session || r.operation!=c->packet[9] || memcmp(r.request,c->packet+8,32)) return lost(c);
    if(r.cancelled_cookie && (r.cancelled_cookie!=c->superseded[3] || memcmp(r.cancelled_request,c->superseded+8,32))) return lost(c);
    if(r.result==0) {
        unsigned op=r.operation;
        if(op!=ASQL_STOP_ALL && r.generation!=c->packet[11]) return lost(c);
        if(op==ASQL_UNBIND ? r.bound!=0 : op!=ASQL_STOP_ALL && r.bound!=1) return lost(c);
        if((op==ASQL_PLAY || op==ASQL_CONTINUE) && r.state!=AICA_SEQ_RUNNING) return lost(c);
        if(op==ASQL_PAUSE && r.state!=AICA_SEQ_PAUSED) return lost(c);
        if((op==ASQL_STOP || op==ASQL_STOP_ALL || op==ASQL_UNBIND) && r.state!=AICA_SEQ_IDLE) return lost(c);
        if(op==ASQL_PLAY && (r.slot!=c->packet[12] || r.resource_generation!=c->packet[13] || r.program!=c->packet[14])) return lost(c);
        if(op==ASQL_CHAIN && (r.chain_slot!=c->packet[12] || r.chain_generation!=c->packet[13] || r.chain_program!=c->packet[14])) return lost(c);
    }
    if(r.result==ASQL_DECLINED && r.operation!=ASQL_PLAY) return lost(c);
    c->receipt=r;if(r.result==ASQL_PENDING) return 0;
    c->state=ASQC_DONE;return 1;
}
int asql_client_consume(asql_client_t *c) {
    if(!c || c->state!=ASQC_DONE) return -1;
    c->state=ASQC_IDLE;return 0;
}
