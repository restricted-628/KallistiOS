#include "owner.h"
#include "snd_iface_internal.h"
#include <dc/sound/aica_sequence_resource.h>
#include <dc/sound/aica_observation.h>
#include <dc/sound/sound.h>
#include <arch/timer.h>
#include <kos/irq.h>
#include <errno.h>
#include <string.h>
static asq_owner_t *owner;
static uint64_t deadline(const asq_owner_t *o) {
    uint64_t now=timer_ms_gettime64();return UINT64_MAX-now<o->timeout_ms?UINT64_MAX:now+o->timeout_ms;
}
static int read_receipt(void *context,uint32_t *words,unsigned count) {
    (void)context;if(count!=1 && count!=64) return -1;
    return snd_iface_observation_read(AICA_MEM_SEQUENCE_RECEIPT,words,count*4u);
}
static int session(void *context,uint32_t *active) {
    asq_owner_t *o=context;aica_observation_header_t header;
    if(snd_observation_session(active)<0) return -1;
    if(*active!=o->session) return 0;
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_SEQUENCE_RESOURCE)) { *active=0;return 0; }
    if(snd_iface_observation_read(AICA_MEM_OBSERVATION_HEADER,&header,sizeof(header))<0) return -1;
    *active=header.ready==1 && header.layout_version==AICA_OBSERVATION_LAYOUT_VERSION?header.active_session:0;
    return 0;
}
static int submit(void *context,const uint32_t packet[16]) {
    asq_owner_t *o=context;uint32_t copy[16];memcpy(copy,packet,64);
    return snd_iface_observation_submit(copy,16,deadline(o))<0?-1:1;
}
static int context(asq_owner_t *o) {
    if(!o || owner!=o || !o->initialized) { errno=EINVAL;return -1; }
    if(irq_inside_int()) { errno=EPERM;return -1; }return 0;
}
static int live(asq_owner_t *o) {
    uint32_t active;
    if(o->lost) { errno=ENODEV;return -1; }
    if(session(o,&active)<0) { errno=EIO;return -1; }
    if(active!=o->session) { o->lost=1;errno=ENODEV;return -1; }return 0;
}
static int error(asq_host_slot_t *s,int e) {
    if(e<=0) e=EIO;
    if(!s->error) s->error=e;
    if(!s->transfer && s->state!=ASQH_REGISTER && s->state!=ASQH_DROP) s->state=ASQH_FAILED;
    errno=e;return -1;
}
static int overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return !a || !b || n>UINTPTR_MAX-x || m>UINTPTR_MAX-y || (x<y+m && y<x+n);
}
static int span(uint32_t address,uint32_t bytes) {
    return address>=0x30000 && address<0x200000 && !(address&31u) && bytes<=0x200000-address;
}
int asq_owner_init(asq_owner_t *o,uint32_t chunk,uint32_t timeout) {
    asq_client_ops_t ops={read_receipt,session,submit,o};uint32_t active;
    if(!o || owner || o->initialized || chunk<32 || chunk>32768 || (chunk&31u) || !timeout) { errno=EINVAL;return -1; }
    if(irq_inside_int()) { errno=EPERM;return -1; }
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_SEQUENCE_RESOURCE)) { errno=ENOTSUP;return -1; }
    o->timeout_ms=timeout;
    if(snd_observation_bind_until(deadline(o),&active)<0) return -1;
    memset(o,0,sizeof(*o));o->session=active;o->timeout_ms=timeout;o->chunk_limit=chunk;o->active=8;
    if(asq_client_init(&o->client,active,&ops)<0 || asq_client_poll(&o->client)!=1) { errno=EBUSY;return -1; }
    o->initialized=1;owner=o;return 0;
}
static int free_slot(asq_owner_t *o,asq_host_slot_t *s) {
    if(!s->safe_to_free || s->transfer) { errno=EBUSY;return -1; }
    if(live(o)<0) return -1;
    if(s->source) {
        errno=0;snd_mem_free(s->source);if(errno) return error(s,errno);
        s->source=0;
    }
    if(s->events) {
        errno=0;snd_mem_free(s->events);if(errno) return error(s,errno);
        s->events=0;
    }
    s->input=NULL;s->state=ASQH_CLOSED;return 0;
}
static int upload(asq_owner_t *o,asq_host_slot_t *s) {
    uint32_t left=s->bytes-s->committed;s->chunk=left<o->chunk_limit?left:o->chunk_limit;
    errno=0;
    if(spu_memload_async(s->source+s->committed,s->input+s->committed,s->chunk,
        SPU_TRANSFER_AUTO,o->timeout_ms,NULL,NULL,&s->transfer)<0) return error(s,errno);
    return 0;
}
int asq_owner_begin(asq_owner_t *o,unsigned slot,const void *source,size_t bytes) {
    asq_layout_t plan;asq_host_slot_t *s;uint32_t generation;
    if(context(o)<0) return -1;
    if(slot>=8 || !bytes || overlap(o,sizeof(*o),source,bytes) || asq_measure(source,bytes,&plan)<0) { errno=EINVAL;return -1; }
    if(live(o)<0) return -1;
    s=&o->slots[slot];
    if(o->active!=8 || o->client.state!=ASQC_IDLE ||
       (s->state!=ASQH_EMPTY && s->state!=ASQH_CLOSED)) { errno=EBUSY;return -1; }
    if(s->generation==UINT32_MAX || o->client.cookie>=UINT32_MAX-1) { errno=EOVERFLOW;return -1; }
    generation=s->generation;memset(s,0,sizeof(*s));s->generation=generation;s->requested_generation=generation+1;
    s->safe_to_free=1;s->input=source;s->bytes=plan.size;s->capacity=plan.events;s->state=ASQH_UPLOAD;
    errno=0;s->source=snd_mem_malloc(s->bytes);
    if(!s->source) return error(s,errno?errno:ENOMEM);
    errno=0;s->events=snd_mem_malloc(s->capacity*16u);
    if(!s->events) return error(s,errno?errno:ENOMEM);
    if(!span(s->source,s->bytes) || !span(s->events,s->capacity*16u) ||
       (s->source<s->events+s->capacity*16u && s->events<s->source+s->bytes)) return error(s,ERANGE);
    o->active=slot;return upload(o,s);
}
static int drop(asq_owner_t *o,unsigned slot) {
    asq_host_slot_t *s=&o->slots[slot];const uint32_t args[4]={0};int result;
    s->state=ASQH_DROP;o->active=slot;
    result=asq_client_begin(&o->client,ASQ_WIRE_DROP,slot,s->generation,args);
    if(result<0 && o->client.state==ASQC_LOST) { o->lost=1;s->state=ASQH_LOST; }
    return result<0?-1:0;
}
static int transfer_step(asq_owner_t *o,asq_host_slot_t *s) {
    spu_transfer_status_t status;int exact;
    errno=0;
    if(spu_transfer_get_status(s->transfer,&status)<0) return error(s,errno);
    if(status.state==SPU_TRANSFER_QUEUED || status.state==SPU_TRANSFER_RUNNING) {
        if(s->close_requested || s->error || o->lost) {
            errno=0;if(spu_transfer_cancel(s->transfer)<0 && errno!=EALREADY) return error(s,errno);
        }
        return 0;
    }
    if(status.state!=SPU_TRANSFER_COMPLETE && status.state!=SPU_TRANSFER_CANCELLED &&
       status.state!=SPU_TRANSFER_TIMED_OUT && status.state!=SPU_TRANSFER_ERROR) return error(s,EIO);
    exact=status.state==SPU_TRANSFER_COMPLETE && status.result==0 &&
        status.direction==SPU_TRANSFER_TO_SOUND_RAM && status.requested_bytes==s->chunk && status.completed_bytes==s->chunk;
    if(!exact) error(s,status.result?status.result:(status.state==SPU_TRANSFER_CANCELLED?ECANCELED:EIO));
    errno=0;
    if(spu_transfer_destroy(s->transfer)<0) {
        if(errno==EBUSY) return 0;
        return error(s,errno);
    }
    s->transfer=NULL;
    if(exact && !s->error && !s->close_requested && !o->lost) s->committed+=s->chunk;
    s->chunk=0;
    if(o->lost) { s->input=NULL;s->state=ASQH_LOST;return -1; }
    if(s->error || s->close_requested) {
        s->input=NULL;s->state=ASQH_FAILED;o->active=8;
        if(s->close_requested) return free_slot(o,s);
        errno=s->error;return -1;
    }
    if(s->committed<s->bytes) return upload(o,s);
    s->input=NULL;return 0;
}
int asq_owner_pump(asq_owner_t *o) {
    asq_host_slot_t *s;unsigned slot;int result;
    if(context(o)<0) return -1;
    result=live(o);
    if(o->active==8) return result;
    slot=o->active;s=&o->slots[slot];
    if(s->transfer) {
        if(result<0 && !o->lost) return -1;
        result=transfer_step(o,s);
        if(result<0 || s->transfer || o->active==8 || o->lost) return result;
    }
    else if(result<0) return result;
    if(s->state==ASQH_UPLOAD) {
        uint32_t args[4]={s->source,s->bytes,s->events,s->capacity};
        if(s->committed!=s->bytes) { o->active=8;return error(s,EIO); }
        /* Any attempted PREPARE is uncertain until its exact receipt resolves. */
        s->safe_to_free=0;s->state=ASQH_REGISTER;
        result=asq_client_begin(&o->client,ASQ_WIRE_PREPARE,slot,s->requested_generation,args);
        if(result<0 && o->client.state==ASQC_LOST) { o->lost=1;s->state=ASQH_LOST; }
        return result<0?-1:0;
    }
    if(s->state!=ASQH_REGISTER && s->state!=ASQH_DROP && s->state!=ASQH_QUERY) return 0;
    result=asq_client_poll(&o->client);
    if(result<0) { o->lost=1;s->state=ASQH_LOST;return -1; }
    if(o->client.receipt.cookie==o->client.cookie && (o->client.receipt.flags&ASQ_WIRE_ADMITTED)) {
        if(o->client.receipt.event_count!=s->capacity) { o->lost=1;s->state=ASQH_LOST;errno=EPROTO;return -1; }
        s->admitted=1;s->generation=s->requested_generation;
    }
    if(!result) {
        if(s->close_requested && s->state==ASQH_REGISTER && s->admitted) {
            result=asq_client_cancel(&o->client);
            if(result>=0) s->state=ASQH_DROP;
        }
        return result<0?-1:0;
    }
    {
        const asq_receipt_t *r=&o->client.receipt;int rc=r->result;
        int may_drop=s->state==ASQH_REGISTER || s->state==ASQH_QUERY;
        if(s->state==ASQH_REGISTER) {
            if(!s->admitted && r->state &&
               (r->source==s->source || r->events==s->events)) {
                o->lost=1;s->state=ASQH_LOST;errno=EPROTO;return -1;
            }
            if(!s->admitted) s->safe_to_free=1;
            s->state=rc?ASQH_FAILED:ASQH_READY;
            if(rc) s->error=EINVAL;
        }
        else if(s->state==ASQH_QUERY) {
            if(!rc && (r->source!=s->source || r->events!=s->events ||
               r->source_bytes!=s->bytes || r->capacity!=s->capacity || r->event_count!=s->capacity)) {
                o->lost=1;s->state=ASQH_LOST;errno=EPROTO;return -1;
            }
            s->state=!rc && r->state==ASQ_CATALOG_READY?ASQH_READY:ASQH_FAILED;
            if(rc) s->error=EIO;
        }
        else if(!rc) { s->safe_to_free=1;s->admitted=0;s->state=ASQH_FAILED; }
        else { s->state=ASQH_FAILED;s->error=rc==ASQ_CATALOG_BUSY?EBUSY:EIO; }
        asq_client_consume(&o->client);o->active=8;
        if(s->close_requested) {
            if(s->safe_to_free) return free_slot(o,s);
            if(may_drop && s->admitted) return drop(o,slot);
        }
        if(rc) { errno=s->error;return -1; }
    }
    return 0;
}
int asq_owner_retry(asq_owner_t *o) {
    if(context(o)<0 || live(o)<0) return -1;
    return asq_client_retry(&o->client);
}
int asq_owner_query(asq_owner_t *o,unsigned slot) {
    const uint32_t args[4]={0};asq_host_slot_t *s;int result;
    if(context(o)<0 || live(o)<0) return -1;
    if(slot>=8) { errno=EINVAL;return -1; }
    s=&o->slots[slot];
    if(o->active!=8 || o->client.state!=ASQC_IDLE || !s->admitted ||
       (s->state!=ASQH_READY && s->state!=ASQH_FAILED)) { errno=EBUSY;return -1; }
    s->state=ASQH_QUERY;o->active=slot;
    result=asq_client_begin(&o->client,ASQ_WIRE_QUERY,slot,s->generation,args);
    if(result<0 && o->client.state==ASQC_LOST) { o->lost=1;s->state=ASQH_LOST; }
    return result<0?-1:0;
}
int asq_owner_close(asq_owner_t *o,unsigned slot) {
    asq_host_slot_t *s;
    if(context(o)<0) return -1;
    if(slot>=8) { errno=EINVAL;return -1; }
    if(live(o)<0) return -1;
    s=&o->slots[slot];
    if(s->state==ASQH_EMPTY || s->state==ASQH_CLOSED) return 0;
    if(o->active!=8 && o->active!=slot) { errno=EBUSY;return -1; }
    s->close_requested=1;
    if(s->transfer) {
        errno=0;if(spu_transfer_cancel(s->transfer)<0 && errno!=EALREADY) return error(s,errno);
        return 0;
    }
    if(s->state==ASQH_REGISTER || s->state==ASQH_DROP || s->state==ASQH_QUERY) return asq_owner_pump(o);
    if(s->safe_to_free) { if(o->active==slot) o->active=8;return free_slot(o,s); }
    if(s->admitted) return drop(o,slot);
    errno=EBUSY;return -1;
}
