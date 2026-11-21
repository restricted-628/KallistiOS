#include "music_live.h"
#include "snd_iface_internal.h"
#include "snd_port_internal.h"
#include <dc/sound/aica_music_live.h>
#include <arch/timer.h>
#include <errno.h>
#include <string.h>
/* One serialized explicit owner. Never clear this after an admitted/uncertain
   CLAIM; a later session is not permission to retire old physical ownership. */
static aica_music_owner_t *owner;
static uint64_t deadline(aica_music_owner_t *o) {
    uint64_t now=timer_ms_gettime64();
    return UINT64_MAX-now<o->timeout_ms?UINT64_MAX:now+o->timeout_ms;
}
static int read_receipt(void *context, unsigned offset, uint32_t *words, unsigned count) {
    (void)context;
    if(offset || (count!=1 && count!=64)) return -1;
    return snd_iface_observation_read(AICA_MEM_MUSIC_RECEIPT,words,count*4u);
}
static int session(void *context, uint32_t *active) {
    aica_music_owner_t *o=context;aica_observation_header_t header;
    if(snd_observation_session(active)<0) return -1;
    if(*active!=o->session) return 0;
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_MUSIC)) {
        *active=0;return 0;
    }
    if(snd_iface_observation_read(AICA_MEM_OBSERVATION_HEADER,&header,sizeof(header))<0)
        return -1;
    *active=header.ready==1 && header.layout_version==AICA_OBSERVATION_LAYOUT_VERSION?
        header.active_session:0;
    return 0;
}
static int submit(void *context, const uint32_t words[16]) {
    aica_music_owner_t *o=context;uint32_t copy[16];
    memcpy(copy,words,sizeof(copy));
    /* Existing bridge returns0 admitted, negative on error. Even an error is
       conservatively uncertain here; no optimistic reference release. */
    return snd_iface_observation_submit(copy,16,deadline(o))<0?-1:1;
}
static void unused_unwind(aica_music_owner_t *o) {
    while(o->count) {
        snd_port_t port=o->ports[--o->count];
        snd_port_playback_unpin(port);snd_port_close(port);
    }
    o->initialized=0;
    if(owner==o) owner=0;
}
int aica_music_owner_init(aica_music_owner_t *o, unsigned voices, uint32_t timeout_ms) {
    aica_music_client_ops_t ops={read_receipt,session,submit,o};
    uint32_t args[4]={0}, active;unsigned channel;
    if(!o || owner || o->initialized || voices<1 || voices>64 || !timeout_ms) { errno=EINVAL;return -1; }
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_MUSIC)) { errno=ENOTSUP;return -1; }
    o->timeout_ms=timeout_ms;
    if(snd_observation_bind_until(deadline(o),&active)<0) return -1;
    /* Reserve exclusive owner before any pool acquisition. Caller serializes. */
    memset(o,0,sizeof(*o));o->timeout_ms=timeout_ms;o->session=active;o->initialized=1;owner=o;
    if(aica_music_client_init(&o->client,active,&ops)<0 || aica_music_client_poll(&o->client)!=1) {
        unused_unwind(o);errno=EBUSY;return -1;
    }
    while(o->count<voices) {
        snd_port_t port;
        if(snd_port_reserve_music(&port)<0) { unused_unwind(o);return -1; }
        if(snd_port_playback_pin(port,&channel)<0) {
            snd_port_close(port);unused_unwind(o);return -1;
        }
        o->ports[o->count++]=port;o->masks[channel/32]|=1u<<(channel%32);
    }
    args[0]=o->masks[0];args[1]=o->masks[1];
    o->claim_attempted=1;o->retained=1;
    /* From this point onward even immediate transport/session failure retains
       the pool; this intentionally conservative owner has no rollback API. */
    return aica_music_client_begin(&o->client,AICA_MUSIC_WIRE_CLAIM,0,0,args)<0?-1:0;
}
static int valid(aica_music_owner_t *o) { return o && owner==o && o->initialized; }
int aica_music_owner_poll(aica_music_owner_t *o) {
    int result;
    if(!valid(o)) { errno=EINVAL;return -1; }
    result=aica_music_client_poll(&o->client);
    if(result==1 && o->client.state==AICA_MC_DONE && o->pending_bank) {
        /* Failed quiesce may leave a superseded OPEN uncertain; retain until
           exact successful quiesce. Direct terminal OPEN (even rejection) is
           exact; successful OPEN already established the ARM producer pin. */
        if(o->client.receipt.operation==AICA_MUSIC_WIRE_OPEN ||
           (o->client.receipt.operation==AICA_MUSIC_WIRE_QUIESCE && o->client.receipt.result==0)) {
            if(abk_bank_pending_release(o->pending_bank)<0) return -1;
            o->pending_bank=0;
        }
    }
    return result;
}
int aica_music_owner_retry(aica_music_owner_t *o) {
    if(!valid(o)) return -1;
    return aica_music_client_retry(&o->client);
}
int aica_music_owner_open(aica_music_owner_t *o, unsigned port, abk_bank_t *bank) {
    uint32_t args[4];int result;
    if(!valid(o) || !bank || port>=8 || o->pending_bank || o->client.state!=AICA_MC_IDLE ||
       o->client.admission_closed) { errno=EINVAL;return -1; }
    if(abk_bank_pending_acquire(bank)<0) return -1;
    /* No bank reinitialization/mutation is permitted while this pending ref is
       retained. Close may latch closing but cannot unregister or free it. */
    if(bank->session!=o->session || bank->slot>=128 || !bank->generation) {
        abk_bank_pending_release(bank);errno=EINVAL;return -1;
    }
    args[0]=bank->slot;args[1]=bank->generation;args[2]=args[3]=0;
    o->pending_bank=bank;
    result=aica_music_client_begin(&o->client,AICA_MUSIC_WIRE_OPEN,port,0,args);
    /* Exhaustion/session loss is retained, just like uncertain submission. */
    return result;
}
int aica_music_owner_command(aica_music_owner_t *o, unsigned operation, unsigned port,
    uint32_t generation, const uint32_t arguments[4]) {
    if(!valid(o) || operation==AICA_MUSIC_WIRE_OPEN || operation==AICA_MUSIC_WIRE_CLAIM)
        return -1;
    return aica_music_client_begin(&o->client,operation,port,generation,arguments);
}
int aica_music_owner_set_input_mode(aica_music_owner_t *o, unsigned port,
    uint32_t generation, unsigned enabled) {
    if(!valid(o) || port >= 8 || !generation || enabled > 1) {
        errno = EINVAL;
        return -1;
    }
    if(!(snd_iface_observation_features() & AICA_DRIVER_FEATURE_MIDI_INPUT)) {
        errno = ENOTSUP;
        return -1;
    }
    const uint32_t args[4] = {enabled,0,0,0};
    return aica_music_client_begin(&o->client,AICA_MUSIC_WIRE_INPUT_MODE,
                                  port,generation,args);
}

int aica_music_owner_quiesce(aica_music_owner_t *o) {
    if(!valid(o)) return -1;
    return aica_music_client_quiesce(&o->client);
}
int aica_music_owner_consume(aica_music_owner_t *o) {
    if(!valid(o) || o->pending_bank) return -1;
    return aica_music_client_consume(&o->client);
}
