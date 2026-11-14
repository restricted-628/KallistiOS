#include "aica_music_wire.h"
#include <stddef.h>
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static unsigned bits(uint32_t x) { unsigned n=0;while(x) { x&=x-1;++n; }return n; }
static int blank(const uint32_t *v,unsigned count) { while(count--) if(*v++) return 0;return 1; }
static int publish(aica_music_wire_t *w,int result) {
    volatile aica_music_receipt_t *r=w->receipt;aica_music_t *m=w->music;
    volatile uint32_t *words=(volatile uint32_t *)r;
    uint32_t seq=r->sequence,p=w->request[2];unsigned i;
    uint32_t active=0,held=0,sustained=0,releasing=0,updates=0;
    if((seq&1u) || seq>=UINT32_MAX-2u) { r->sequence=UINT32_MAX;w->faulted=1;return 0; }
    r->sequence=seq+1;for(i=1;i<64;i++) words[i]=0;
    r->version=AICA_MUSIC_WIRE_VERSION;r->ready=1;r->session=w->session;
    r->cookie=w->cookie;r->operation=w->request[1];r->result=result;
    r->flags=(w->claimed?1u:0u)|(m->quiescing?2u:0u)|((w->faulted || m->faulted)?4u:0u)|((uint32_t)w->bank_pins<<8);
    for(i=0;i<8;i++) r->request[i]=w->request[i];
    if(p<8) {
        const aica_music_port_t *v=&m->ports[p];
        r->generation=v->generation;r->bank=v->bank;r->bank_generation=v->bank_generation;
        r->open=v->open;r->mute=v->mute;r->gm=v->gm;r->release_mode=v->release_mode;r->mailbox=v->mailbox;
        r->volume=v->fades[0].value;r->pan=v->fades[1].value;r->pitch=v->fades[2].value;
        r->direct=v->direct;r->effect=v->effect;
        for(i=0;i<3;i++) r->fade_remaining[i]=v->fades[i].duration-v->fades[i].elapsed;
        for(i=0;i<m->voice_count;i++) if(m->voices[i].port==p && m->voices[i].state) {
            ++active;if(m->voices[i].dirty) ++updates;
            if(m->voices[i].state==AICA_MUSIC_HELD) ++held;
            else if(m->voices[i].state==AICA_MUSIC_SUSTAINED) ++sustained;
            else if(m->voices[i].state==AICA_MUSIC_RELEASE) ++releasing;
        }
    }
    r->active=active;r->held=held;r->sustained=sustained;r->releasing=releasing;r->updates_pending=updates;
    r->queued=m->count;r->applied=m->applied;r->rejected=m->rejected;
    r->declined=m->declined;r->stolen=m->stolen;r->last_result=m->last_result;r->token=m->token;
    r->cancelled_cookie=w->cancelled_cookie;r->sequence=seq+2;return 1;
}
int aica_music_wire_init(aica_music_wire_t *w,aica_music_t *m,
    volatile aica_music_receipt_t *r,const aica_music_wire_ops_t *ops) {
    if(!w || !m || !r || !ops || !ops->claim || !ops->pin_bank || !ops->unpin_bank) return 0;
    clear(w,sizeof(*w));w->music=m;w->receipt=r;w->ops=*ops;
    for(unsigned i=0;i<64;i++) ((volatile uint32_t *)r)[i]=0;
    r->version=AICA_MUSIC_WIRE_VERSION;r->ready=1;return 1;
}
static int unpin(aica_music_wire_t *w,unsigned p) {
    aica_music_port_t *v=&w->music->ports[p];
    if(!(w->bank_pins&(1u<<p))) return 1;
    if(!w->ops.unpin_bank(w->ops.context,w->session,v->bank,v->bank_generation)) {
        w->faulted=1;return 0;
    }
    w->bank_pins=(uint8_t)(w->bank_pins&~(1u<<p));return 1;
}
static int apply(aica_music_wire_t *w) {
    const uint32_t *v=w->request,*a=v+4;unsigned op=v[1],p=v[2];uint32_t g=v[3],out;
    aica_music_t *m=w->music;int result;
    if(op==AICA_MUSIC_WIRE_CLAIM) {
        unsigned count=bits(a[0])+bits(a[1]);
        if(p || g || a[2] || a[3] || !count) return AICA_MUSIC_INVALID;
        if(w->claimed || w->faulted || m->quiescing) return AICA_MUSIC_BUSY;
        if(!w->ops.claim(w->ops.context,a[0],a[1])) return AICA_MUSIC_BUSY;
        m->voice_count=(uint8_t)count;w->claimed=1;return 0;
    }
    if(!w->claimed) return AICA_MUSIC_BUSY;
    if(op==AICA_MUSIC_WIRE_QUIESCE) {
        if(p || g || !blank(a,4)) return AICA_MUSIC_INVALID;
        aica_music_quiesce(m);
        for(unsigned i=0;i<8;i++) unpin(w,i);
        return w->faulted?AICA_MUSIC_FAULT:0;
    }
    if(op==AICA_MUSIC_WIRE_CLOSE_ALL || op==AICA_MUSIC_WIRE_RESET_ALL) {
        if(p || g || !blank(a,4)) return AICA_MUSIC_INVALID;
        if(op==AICA_MUSIC_WIRE_RESET_ALL && (w->faulted || m->faulted)) return AICA_MUSIC_FAULT;
        for(unsigned i=0;i<8;i++) {
            if(m->ports[i].open) {
                if(op==AICA_MUSIC_WIRE_CLOSE_ALL) aica_music_close(m,i,m->ports[i].generation);
                else aica_music_reset(m,i,m->ports[i].generation);
            }
            if(op==AICA_MUSIC_WIRE_CLOSE_ALL) unpin(w,i);
        }
        return w->faulted?AICA_MUSIC_FAULT:0;
    }
    if(p>=8) return AICA_MUSIC_INVALID;
    if(op==AICA_MUSIC_WIRE_QUERY) {
        if(!blank(a,4) || (g && m->ports[p].generation!=g)) return AICA_MUSIC_INVALID;
        return 0;
    }
    if(op==AICA_MUSIC_WIRE_CLOSE) {
        if(!blank(a,4)) return AICA_MUSIC_INVALID;
        result=aica_music_close(m,p,g);if(result) return result;
        return unpin(w,p)?0:AICA_MUSIC_FAULT;
    }
    if(w->faulted || m->faulted) return AICA_MUSIC_FAULT;
    if(m->quiescing) return AICA_MUSIC_BUSY;
    if(op==AICA_MUSIC_WIRE_OPEN) {
        if(g || a[0]>=128 || !a[1] || a[2] || a[3]) return AICA_MUSIC_INVALID;
        if(m->ports[p].open || (w->bank_pins&(1u<<p))) return AICA_MUSIC_BUSY;
        if(m->ports[p].generation==UINT32_MAX) return AICA_MUSIC_EXHAUSTED;
        if(!w->ops.pin_bank(w->ops.context,w->session,a[0],a[1])) return AICA_MUSIC_MISSING;
        result=aica_music_open(m,p,a[0],a[1],&out);
        /* All open preconditions are checked in this serialized owner. */
        if(result) {
            if(!w->ops.unpin_bank(w->ops.context,w->session,a[0],a[1])) w->faulted=1;
            return result;
        }
        w->bank_pins=(uint8_t)(w->bank_pins|(1u<<p));return 0;
    }
    if(!(w->bank_pins&(1u<<p))) return AICA_MUSIC_INVALID;
    switch(op) {
        case AICA_MUSIC_WIRE_MESSAGE:return aica_music_message(m,p,g,a[0],a[1],a[2],a[3]);
        case AICA_MUSIC_WIRE_MUTE:
            return blank(a+1,3)?aica_music_mute(m,p,g,a[0]):AICA_MUSIC_INVALID;
        case AICA_MUSIC_WIRE_GM:
            return blank(a+1,3)?aica_music_gm(m,p,g,a[0]):AICA_MUSIC_INVALID;
        case AICA_MUSIC_WIRE_RELEASE:
            return blank(a+1,3)?aica_music_release_mode(m,p,g,a[0]):AICA_MUSIC_INVALID;
        case AICA_MUSIC_WIRE_PARAMETER:
            return !a[3]?aica_music_parameter(m,p,g,a[0],(int32_t)a[1],a[2]):AICA_MUSIC_INVALID;
        case AICA_MUSIC_WIRE_RESET:
            return blank(a,4)?aica_music_reset(m,p,g):AICA_MUSIC_INVALID;
        default:return AICA_MUSIC_INVALID;
    }
}
int aica_music_wire_command(aica_music_wire_t *w,const aica_cmd_t *packet,uint32_t active_session) {
    const uint32_t *v;unsigned op;
    if(!w || !packet || packet->cmd!=AICA_MUSIC_WIRE_COMMAND || packet->size!=16 ||
       !packet->cmd_id || packet->timestamp || packet->misc[0] || packet->misc[1] || packet->misc[2] || packet->misc[3]) return 0;
    v=(const uint32_t *)packet->cmd_data;op=v[1];
    if(!active_session || v[0]!=active_session || (w->session && w->session!=active_session)) return 0;
    if(packet->cmd_id==w->cookie) {
        for(unsigned i=0;i<8;i++) if(v[i]!=w->request[i]) return 0;
        return 1;
    }
    if(packet->cmd_id<w->cookie || (w->pending && op!=AICA_MUSIC_WIRE_QUIESCE)) return 0;
    /* Reserve both publications; urgent quiesce uses only one final receipt. */
    if((w->receipt->sequence&1u) ||
       w->receipt->sequence>UINT32_MAX-(op==AICA_MUSIC_WIRE_QUIESCE?3u:5u)) {
        w->receipt->sequence=UINT32_MAX;w->faulted=1;return 0;
    }
    if(w->pending) {
        /* Validate the entire urgent request before cancelling accepted work. */
        if(v[2] || v[3] || !blank(v+4,4)) return 0;
        w->cancelled_cookie=w->cookie;w->pending=0;
    }
    w->session=active_session;w->cookie=packet->cmd_id;
    for(unsigned i=0;i<8;i++) w->request[i]=v[i];
    if(w->claimed && w->music->count && !w->music->faulted &&
       ((op>=AICA_MUSIC_WIRE_OPEN && op<=AICA_MUSIC_WIRE_RESET &&
         op!=AICA_MUSIC_WIRE_MESSAGE && op!=AICA_MUSIC_WIRE_CLOSE) || op==AICA_MUSIC_WIRE_RESET_ALL)) {
        w->pending=1;return publish(w,AICA_MUSIC_WIRE_PENDING);
    }
    return publish(w,apply(w));
}
void aica_music_wire_service(aica_music_wire_t *w) {
    if(!w || !w->pending || (w->music->count && !w->music->faulted)) return;
    w->pending=0;publish(w,apply(w));
}
