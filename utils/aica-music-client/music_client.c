#include "music_client.h"
#include <string.h>
#include <limits.h>
static int lost(aica_music_client_t *c,unsigned error) {
    c->state=AICA_MC_LOST;c->error=(uint8_t)error;return -1;
}
static int zeros(const uint32_t *a,unsigned n) { while(n--) if(*a++) return 0;return 1; }
static int session_ok(aica_music_client_t *c) {
    uint32_t active;
    if(c->ops.session(c->ops.context,&active)<0) { c->error=AICA_MC_IO;return 0; }
    if(active!=c->session) return lost(c,AICA_MC_PROTOCOL);
    return 1;
}
static int coherent(aica_music_client_t *c,aica_music_receipt_t *r) {
    uint32_t before,after,words[64];
    if(c->ops.read(c->ops.context,0,&before,1)<0) goto io;
    if(before==UINT32_MAX) return lost(c,AICA_MC_EXHAUSTED);
    if(before&1u) return 0;
    if(c->ops.read(c->ops.context,0,words,64)<0 ||
       c->ops.read(c->ops.context,0,&after,1)<0) goto io;
    if(before!=after || words[0]!=before) return 0;
    memcpy(r,words,sizeof(*r));return 1;
io:c->error=AICA_MC_IO;return 0;
}
static int valid_request(unsigned op,unsigned p,uint32_t g,const uint32_t *a) {
    int32_t value;
    if(!a || op<1 || op>13 || p>=8) return 0;
    if(op==AICA_MUSIC_WIRE_CLAIM) return !p && !g && (a[0] || a[1]) && !a[2] && !a[3];
    if(op>=AICA_MUSIC_WIRE_QUIESCE) return !p && !g && zeros(a,4);
    if(op==AICA_MUSIC_WIRE_OPEN) return !g && a[0]<128 && a[1] && !a[2] && !a[3];
    if(op==AICA_MUSIC_WIRE_QUERY) return zeros(a,4);
    if(!g) return 0;
    switch(op) {
        case AICA_MUSIC_WIRE_CLOSE:case AICA_MUSIC_WIRE_RESET:return zeros(a,4);
        case AICA_MUSIC_WIRE_MESSAGE:return a[0]>=0x80 && a[0]<0xf0 && a[1]<=127 && a[2]<=127 && a[3]<=31;
        case AICA_MUSIC_WIRE_MUTE:return a[0]<=65535 && zeros(a+1,3);
        case AICA_MUSIC_WIRE_GM:case AICA_MUSIC_WIRE_RELEASE:return a[0]<=1 && zeros(a+1,3);
        case AICA_MUSIC_WIRE_PARAMETER:
            memcpy(&value,a+1,sizeof(value));
            return a[0]<=4 && (a[0]==2?(value>=-32768 && value<=32767):(value>=-127 && value<=127)) &&
                a[2]<=32767 && (a[0]<3 || !a[2]) && !a[3];
        default:return 0;
    }
}
int aica_music_client_init(aica_music_client_t *c,uint32_t session,const aica_music_client_ops_t *ops) {
    aica_music_client_ops_t copy;
    if(!c || !session || !ops || !ops->read || !ops->session || !ops->submit) return -1;
    copy=*ops;memset(c,0,sizeof(*c));c->ops=copy;c->session=session;c->state=AICA_MC_SYNC;c->known_generation=255;return 0;
}
int aica_music_client_retry(aica_music_client_t *c) {
    int result;
    if(!c || c->state!=AICA_MC_WAIT) return -1;
    result=session_ok(c);if(result<=0) return result;
    result=c->ops.submit(c->ops.context,c->packet);
    if(result<0) { c->uncertain=1;c->error=AICA_MC_IO;return 0; }
    c->error=0;return result>0?1:0;
}
static int prepare(aica_music_client_t *c,unsigned op,unsigned p,uint32_t g,const uint32_t *a) {
    if(c->cookie==UINT32_MAX) return lost(c,AICA_MC_EXHAUSTED);
    memset(c->packet,0,sizeof(c->packet));c->packet[0]=16;c->packet[1]=AICA_MUSIC_WIRE_COMMAND;
    c->packet[3]=++c->cookie;c->packet[8]=c->session;c->packet[9]=op;
    c->packet[10]=p;c->packet[11]=g;memcpy(c->packet+12,a,16);
    c->state=AICA_MC_WAIT;c->uncertain=0;c->error=0;
    if(op==AICA_MUSIC_WIRE_QUIESCE) c->admission_closed=1;
    return aica_music_client_retry(c);
}
int aica_music_client_begin(aica_music_client_t *c,unsigned op,unsigned p,uint32_t g,const uint32_t a[4]) {
    uint32_t copy[4];
    if(!c || c->state!=AICA_MC_IDLE || !valid_request(op,p,g,a) ||
       (c->admission_closed && op!=AICA_MUSIC_WIRE_QUERY && op!=AICA_MUSIC_WIRE_QUIESCE)) return -1;
    memcpy(copy,a,sizeof(copy));return prepare(c,op,p,g,copy);
}
int aica_music_client_quiesce(aica_music_client_t *c) {
    const uint32_t zero[4]={0};
    if(!c || (c->state!=AICA_MC_IDLE && c->state!=AICA_MC_WAIT)) return -1;
    if(c->state==AICA_MC_WAIT) {
        if(c->packet[9]==AICA_MUSIC_WIRE_QUIESCE) return aica_music_client_retry(c);
        if(c->cookie==UINT32_MAX) return lost(c,AICA_MC_EXHAUSTED);
        memcpy(c->superseded,c->packet,sizeof(c->packet));
        /* An unobserved OPEN may already have executed before urgent quiesce.
           Its generation is unknown until an exact later query; retain packet. */
        if(c->packet[9]==AICA_MUSIC_WIRE_OPEN)
            c->known_generation=(uint8_t)(c->known_generation & ~(1u<<c->packet[10]));
    }
    return prepare(c,AICA_MUSIC_WIRE_QUIESCE,0,0,zero);
}
static int shape(const aica_music_receipt_t *r) {
    unsigned i;
    if(r->version!=1 || r->ready!=1 || (r->flags & ~0xff07u) ||
       !zeros(r->reserved,19) || r->result>1 || r->result < AICA_MUSIC_FAULT ||
       r->open>1 || r->mute>65535 || r->gm>1 || r->release_mode>1 || r->mailbox>127 ||
       r->bank>=128 || r->volume < -127 || r->volume>127 ||
       r->pan < -127 || r->pan>127 || r->pitch < -32768 || r->pitch>32767 ||
       r->direct < -127 || r->direct>127 || r->effect < -127 || r->effect>127 ||
       r->queued>64 || r->active>64 || r->held>64 || r->sustained>64 || r->releasing>64 ||
       r->held+r->sustained+r->releasing!=r->active || r->updates_pending>r->active ||
       r->last_result>0 || r->last_result < AICA_MUSIC_FAULT || (r->cancelled_cookie && r->cancelled_cookie>=r->cookie)) return 0;
    for(i=0;i<3;i++) if(r->fade_remaining[i]>32767) return 0;
    if(r->open && (!r->generation || !r->bank_generation)) return 0;
    return 1;
}
static int success(const aica_music_receipt_t *r,const uint32_t *q) {
    unsigned op=q[9],p=q[10];uint32_t pin=1u<<(p+8);
    if(!(r->flags&1)) return 0;
    switch(op) {
        case AICA_MUSIC_WIRE_CLAIM:return 1;
        case AICA_MUSIC_WIRE_OPEN:return r->open && (r->flags&pin) && r->bank==q[12] && r->bank_generation==q[13];
        case AICA_MUSIC_WIRE_CLOSE:return !r->open && !(r->flags&pin) && r->generation==q[11] && !r->active;
        case AICA_MUSIC_WIRE_QUIESCE:return (r->flags&2) && !(r->flags&0xff00) && !r->open && !r->active && !r->queued;
        case AICA_MUSIC_WIRE_CLOSE_ALL:return !(r->flags&0xff00) && !r->open && !r->active;
        case AICA_MUSIC_WIRE_RESET_ALL:return 1;
        case AICA_MUSIC_WIRE_QUERY:return !q[11] || r->generation==q[11];
        default:break;
    }
    if(!r->open || !(r->flags&pin) || r->generation!=q[11]) return 0;
    if(op==AICA_MUSIC_WIRE_MUTE && r->mute!=q[12]) return 0;
    if(op==AICA_MUSIC_WIRE_GM && r->gm!=q[12]) return 0;
    if(op==AICA_MUSIC_WIRE_RELEASE && r->release_mode!=q[12]) return 0;
    if(op==AICA_MUSIC_WIRE_RESET && (r->volume || r->pan || r->pitch || r->direct || r->effect || r->mute)) return 0;
    if(op==AICA_MUSIC_WIRE_PARAMETER) {
        int32_t value,actual[5]={r->volume,r->pan,r->pitch,r->direct,r->effect};
        memcpy(&value,q+13,sizeof(value));
        if(!q[14] && actual[q[12]]!=value) return 0;
        if(q[12]<3 && r->fade_remaining[q[12]]!=q[14]) return 0;
    }
    return 1;
}
int aica_music_client_poll(aica_music_client_t *c) {
    aica_music_receipt_t r;int result;
    if(!c || (c->state!=AICA_MC_SYNC && c->state!=AICA_MC_WAIT && c->state!=AICA_MC_DONE)) return -1;
    if(c->state==AICA_MC_DONE) return 1;
    result=session_ok(c);if(result<=0) return result;
    result=coherent(c,&r);if(result<=0) return result;
    result=session_ok(c);if(result<=0) return result;
    if(!shape(&r)) return lost(c,AICA_MC_PROTOCOL);
    if(c->state==AICA_MC_SYNC) {
        aica_music_receipt_t pristine={0};pristine.version=1;pristine.ready=1;
        if(memcmp(&r,&pristine,sizeof(r))) return lost(c,AICA_MC_PROTOCOL);
        c->receipt=r;c->state=AICA_MC_IDLE;c->error=0;return 1;
    }
    if(r.sequence<c->receipt.sequence || r.cookie<c->receipt.cookie ||
       (r.sequence==c->receipt.sequence && memcmp(&r,&c->receipt,sizeof(r))) ||
       (r.session!=c->session && (r.session || r.cookie || r.sequence)) || r.cookie>c->cookie) return lost(c,AICA_MC_PROTOCOL);
    if(r.cookie<c->cookie) { c->receipt=r;c->error=0;return 0; }
    if(r.operation!=c->packet[9] || r.session!=c->session ||
       memcmp(r.request,c->packet+8,sizeof(r.request))) return lost(c,AICA_MC_PROTOCOL);
    if(r.cancelled_cookie!=c->receipt.cancelled_cookie && r.cancelled_cookie!=c->superseded[3])
        return lost(c,AICA_MC_PROTOCOL);
    if(r.result==AICA_MUSIC_WIRE_PENDING) {
        unsigned op=c->packet[9];
        if(!(op==AICA_MUSIC_WIRE_OPEN || (op>=AICA_MUSIC_WIRE_MUTE && op<=AICA_MUSIC_WIRE_RESET) ||
             op==AICA_MUSIC_WIRE_RESET_ALL)) return lost(c,AICA_MC_PROTOCOL);
    }
    if(r.result==0 && !success(&r,c->packet)) return lost(c,AICA_MC_PROTOCOL);
    {
        unsigned p=c->packet[10];uint32_t expected=c->generation[p];
        if(c->packet[9]==AICA_MUSIC_WIRE_OPEN && r.result==0) {
            if(expected==UINT32_MAX) return lost(c,AICA_MC_PROTOCOL);
            ++expected;
        }
        if((c->known_generation&(1u<<p)) && r.generation!=expected) return lost(c,AICA_MC_PROTOCOL);
        if(r.result!=AICA_MUSIC_WIRE_PENDING) {
            c->generation[p]=r.generation;c->known_generation=(uint8_t)(c->known_generation|(1u<<p));
        }
    }
    c->receipt=r;c->error=0;
    if(r.result==AICA_MUSIC_WIRE_PENDING) return 0;
    c->state=AICA_MC_DONE;return 1;
}
int aica_music_client_consume(aica_music_client_t *c) {
    if(!c || c->state!=AICA_MC_DONE) return -1;
    memset(c->superseded,0,sizeof(c->superseded));c->state=AICA_MC_IDLE;return 0;
}
