#include "live-owner.h"
#include "snd_iface_internal.h"
#include <dc/sound/aica_sequence_resource.h>
#include <dc/sound/aica_observation.h>
#include <arch/timer.h>
#include <kos/irq.h>
#include <errno.h>
#include <string.h>
static asql_owner_t *exclusive;
static int session(void *context,uint32_t *active) {
    asql_owner_t *o=context;aica_observation_header_t header;
    if(snd_observation_session(active)<0) return -1;
    if(*active!=o->session) return 0;
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_SEQUENCE_CONTROL)) { *active=0;return 0; }
    if(snd_iface_observation_read(AICA_MEM_OBSERVATION_HEADER,&header,sizeof(header))<0) return -1;
    *active=header.ready==1 && header.layout_version==AICA_OBSERVATION_LAYOUT_VERSION?header.active_session:0;
    return 0;
}
static int read_receipt(void *context,uint32_t *words,unsigned count) {
    (void)context;if(count!=1 && count!=64) return -1;
    return snd_iface_observation_read(AICA_MEM_SEQUENCE_CONTROL_RECEIPT,words,count*4u);
}
static int submit(void *context,const uint32_t words[16]) {
    asql_owner_t *o=context;uint32_t copy[16];uint64_t now=timer_ms_gettime64();
    uint64_t end=UINT64_MAX-now<o->timeout_ms?UINT64_MAX:now+o->timeout_ms;
    memcpy(copy,words,64);return snd_iface_observation_submit(copy,16,end)<0?-1:1;
}
static int context(asql_owner_t *o) {
    if(!o || exclusive!=o || !o->initialized) { errno=EINVAL;return -1; }
    if(irq_inside_int()) { errno=EPERM;return -1; }return 0;
}
int asql_owner_init(asql_owner_t *o,aica_music_owner_t *m,asq_owner_t *r,uint32_t timeout) {
    asq_client_ops_t ops={read_receipt,session,submit,o};uint32_t active;
    if(!o || !m || !r || exclusive || o->initialized || !timeout ||
       !m->initialized || !r->initialized || !m->session || m->session!=r->session ||
       m->client.state!=AICA_MC_IDLE || r->client.state!=ASQC_IDLE || r->lost) { errno=EINVAL;return -1; }
    if(irq_inside_int()) { errno=EPERM;return -1; }
    if(!(snd_iface_observation_features()&AICA_DRIVER_FEATURE_SEQUENCE_CONTROL)) { errno=ENOTSUP;return -1; }
    memset(o,0,sizeof(*o));o->session=m->session;o->music=m;o->resources=r;o->timeout_ms=timeout;
    if(session(o,&active)<0 || active!=o->session) { errno=ENODEV;return -1; }
    if(asql_client_init(&o->client,o->session,&ops)<0 || asql_client_poll(&o->client)!=1) { errno=EBUSY;return -1; }
    o->initialized=1;exclusive=o;return 0;
}
int asql_owner_command(asql_owner_t *o,unsigned op,unsigned p,uint32_t g,const uint32_t a[4]) {
    if(context(o)<0) return -1;
    if(!a) { errno=EINVAL;return -1; }
    if(o->music->client.state!=AICA_MC_IDLE || o->resources->client.state!=ASQC_IDLE ||
       o->resources->active!=8 || o->resources->lost) { errno=EBUSY;return -1; }
    if(op==ASQL_PLAY || op==ASQL_CHAIN) {
        if(a[0]>=8 || o->resources->slots[a[0]].state!=ASQH_READY ||
           !o->resources->slots[a[0]].admitted || o->resources->slots[a[0]].generation!=a[1]) { errno=EINVAL;return -1; }
    }
    return asql_client_begin(&o->client,op,p,g,a);
}
int asql_owner_poll(asql_owner_t *o) { return context(o)<0?-1:asql_client_poll(&o->client); }
int asql_owner_retry(asql_owner_t *o) { return context(o)<0?-1:asql_client_retry(&o->client); }
int asql_owner_cancel(asql_owner_t *o,int all) { return context(o)<0?-1:asql_client_cancel(&o->client,all); }
int asql_owner_consume(asql_owner_t *o) { return context(o)<0?-1:asql_client_consume(&o->client); }
