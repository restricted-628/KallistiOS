#include "abk_registry.h"
#include "snd_iface_internal.h"
#include <kos/mutex.h>
#include <kos/irq.h>
#include <kos/timer.h>
#include <dc/sound/sound.h>
#include <errno.h>
#include <stddef.h>

static mutex_t lock=MUTEX_INITIALIZER;
static abk_bank_t *owners[AICA_BANK_SLOTS];
static int quiescing;
static int fail(abk_bank_t *b,int e) {
    if(e<=0) e=EIO;
    b->last_error=e;if(!b->first_error) b->first_error=e;
    errno=e;return -1;
}
static int enter(abk_bank_t *b) {
    if(!b) { errno=EINVAL;return -1; }
    if(irq_inside_int()) { errno=EPERM;return -1; }
    return mutex_lock(&lock);
}
static int leave(int result) {
    int e=errno;mutex_unlock(&lock);errno=e;return result;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return an>UINTPTR_MAX-ap || bn>UINTPTR_MAX-bp ||
           (an && bn && ap<bp+bn && bp<ap+an);
}
static int available(abk_bank_t *b) {
    uint32_t session;
    aica_observation_header_t h;
    if(!(snd_iface_observation_features() & AICA_DRIVER_FEATURE_BANK_REGISTRY))
        return fail(b,ENOTSUP);
    if(snd_observation_session(&session)<0)
        return fail(b,errno);
    if(b->session && b->session!=session) {
        b->state=ABKB_LOST;return fail(b,ENODEV);
    }
    if(b->session) {
        if(snd_iface_observation_read(AICA_MEM_OBSERVATION_HEADER,&h,sizeof(h))<0)
            return fail(b,errno);
        if(h.active_session!=b->session || !h.ready) {
            b->state=ABKB_LOST;return fail(b,ENODEV);
        }
    }
    return 0;
}
static int record(abk_bank_t *b,aica_bank_record_t *r) {
    if(snd_iface_observation_read(AICA_MEM_BANK_RECORDS+b->slot*64,r,sizeof(*r))<0) {
        if(errno==EOVERFLOW) b->state=ABKB_LOST;
        return fail(b,errno);
    }
    if(r->flags || r->reserved || (r->session && r->session!=b->session)) {
        b->state=ABKB_LOST;return fail(b,EPROTO);
    }
    return 0;
}
static int lost(abk_bank_t *b) { b->state=ABKB_LOST;return fail(b,EPROTO); }
static int finish_close(abk_bank_t *b) {
    if(b->address) {
        errno=0;snd_mem_free(b->address);
        if(errno) return fail(b,errno);
        b->address=0;
    }
    else if(abk_upload_close(&b->staging)<0) return fail(b,errno);
    if(owners[b->slot]==b) owners[b->slot]=NULL;
    b->state=ABKB_CLOSED;b->last_error=0;return 0;
}
static int submit(abk_bank_t *b,int removing,uint32_t timeout_ms) {
    uint32_t words[14]={0};aica_cmd_t *p=(aica_cmd_t *)words;
    aica_bank_record_t r;
    aica_bank_header_t h;
    uint64_t deadline;
    if(!timeout_ms) return fail(b,EINVAL);
    deadline=timer_ms_gettime64()+timeout_ms;
    if(available(b)<0 || snd_observation_bind_until(deadline,&b->session)<0)
        return fail(b,errno);
    if(snd_iface_observation_read(AICA_MEM_BANK_HEADER,&h,sizeof(h))<0)
        return fail(b,errno);
    if(!h.ready || h.layout_version!=AICA_BANK_LAYOUT_VERSION ||
       h.slot_count!=AICA_BANK_SLOTS || (h.session && h.session!=b->session))
        return lost(b);
    if(record(b,&r)<0) return -1;
    if(r.cookie==UINT32_MAX || (r.generation==UINT32_MAX && !removing))
        return fail(b,EOVERFLOW);
    if(removing) {
        if(b->pending) return fail(b,EBUSY);
        if(r.state!=AICA_BANK_ACTIVE || r.generation!=b->generation ||
           r.image_base!=b->address || r.image_bytes!=b->bytes) return lost(b);
    }
    else if(r.state!=AICA_BANK_EMPTY) return fail(b,EBUSY);
    p->size=removing?11:14;p->cmd=removing?AICA_CMD_BANK_UNREGISTER:AICA_CMD_BANK_REGISTER;
    p->cmd_id=r.cookie+1;words[8]=b->session;words[9]=b->slot;
    words[10]=removing?b->generation:r.generation+1;
    if(!removing) {
        words[11]=b->staging.address;words[12]=b->staging.plan.file_bytes;words[13]=1;
    }
    if(snd_iface_observation_submit(words,p->size,deadline)<0) return fail(b,errno);
    /* Bookkeeping/owner slot already exists. No allocation after admission. */
    b->cookie=p->cmd_id;b->generation=words[10];
    b->state=removing?ABKB_UNREGISTERING:ABKB_PUBLISHING;
    b->last_error=0;return 0;
}
static int poll_locked(abk_bank_t *b,uint32_t timeout_ms) {
    aica_bank_record_t r;
    int result;
    switch(b->state) {
        case ABKB_EMPTY:case ABKB_CLOSED:return 0;
        case ABKB_LOST:return fail(b,ENODEV);
        case ABKB_RETIRED:return finish_close(b);
        case ABKB_FAILED:return b->closing?finish_close(b):fail(b,b->first_error);
        case ABKB_STAGING:
            if(b->closing && abk_upload_cancel(&b->staging)<0) return fail(b,errno);
            result=abk_upload_pump(&b->staging);
            if(b->staging.state==ABKU_IMAGE_READY) b->state=ABKB_STAGED;
            else if(b->staging.state==ABKU_FAILED || b->staging.state==ABKU_CANCELLED)
                b->state=ABKB_FAILED;
            if(b->closing && !b->staging.request) return finish_close(b);
            return result<0?fail(b,errno):0;
        case ABKB_STAGED:return b->closing?finish_close(b):0;
        case ABKB_ACTIVE:
            if(available(b)<0) return -1;
            return b->closing?submit(b,1,timeout_ms):0;
        default:break;
    }
    if(available(b)<0 || record(b,&r)<0) return -1;
    if(r.cookie<b->cookie) return fail(b,EAGAIN);
    if(r.cookie!=b->cookie || r.session!=b->session ||
       r.operation!=(b->state==ABKB_PUBLISHING?AICA_CMD_BANK_REGISTER:AICA_CMD_BANK_UNREGISTER))
        return lost(b);
    if(r.result==AICA_BANK_PENDING) {
        if(b->state!=ABKB_PUBLISHING || r.state!=AICA_BANK_VALIDATING) return lost(b);
        return 0;
    }
    if(r.result>AICA_BANK_EXHAUSTED) return lost(b);
    if(b->state==ABKB_PUBLISHING) {
        if(r.result!=AICA_BANK_OK) {
            if(r.state!=AICA_BANK_EMPTY || r.generation!=b->generation-1 ||
               r.image_base || r.image_bytes || r.queued_refs || r.voice_refs || r.producer_refs)
                return lost(b);
            b->state=ABKB_FAILED;
            return fail(b,r.result==AICA_BANK_BUSY?EBUSY:
                          r.result==AICA_BANK_EXHAUSTED?EOVERFLOW:EINVAL);
        }
        if(r.state!=AICA_BANK_ACTIVE || r.generation!=b->generation ||
           r.image_base!=b->staging.address || r.image_bytes!=b->staging.plan.file_bytes ||
           r.sample_count!=b->staging.plan.sample_count || r.zone_count!=b->staging.plan.zone_count ||
           r.queued_refs || r.voice_refs || r.producer_refs) return lost(b);
        /* Nonallocating ownership move, under the same lock as close/cancel. */
        b->address=b->staging.address;b->bytes=b->staging.plan.file_bytes;
        b->staging.address=0;b->staging.data=NULL;b->staging.state=ABKU_MOVED;
        b->state=ABKB_ACTIVE;
        return b->closing?submit(b,1,timeout_ms):0;
    }
    if(r.result==AICA_BANK_BUSY) {
        if(r.state!=AICA_BANK_ACTIVE || r.generation!=b->generation ||
           r.image_base!=b->address || r.image_bytes!=b->bytes) return lost(b);
        b->state=ABKB_ACTIVE;return fail(b,EBUSY);
    }
    if(r.result!=AICA_BANK_OK || r.state!=AICA_BANK_EMPTY ||
       r.generation!=b->generation || r.image_base || r.image_bytes ||
       r.queued_refs || r.voice_refs || r.producer_refs) return lost(b);
    b->state=ABKB_RETIRED;
    return finish_close(b);
}
int abk_bank_stage(abk_bank_t *b,unsigned slot,const void *data,uint32_t size,
                   abk_sample_t *samples,uint32_t sample_capacity,
                   abk_zone_t *zones,uint32_t zone_capacity,
                   uint32_t chunk_limit,uint32_t lifetime_ms) {
    int rc;
    /* Whole-owner alias rejection precedes every write, including error state. */
    if(!b || !data || !samples || !zones || slot>=AICA_BANK_SLOTS ||
       sample_capacity>ABK_MAX_SAMPLES || zone_capacity>ABK_MAX_ZONES ||
       overlaps(b,sizeof(*b),data,size) ||
       overlaps(b,sizeof(*b),samples,sample_capacity*sizeof(*samples)) ||
       overlaps(b,sizeof(*b),zones,zone_capacity*sizeof(*zones))) { errno=EINVAL;return -1; }
    if(enter(b)<0) return -1;
    if(b->state!=ABKB_EMPTY) return leave(fail(b,EALREADY));
    if(quiescing || owners[slot]) return leave(fail(b,EBUSY));
    if(available(b)<0) return leave(-1);
    owners[slot]=b;b->slot=slot;b->state=ABKB_STAGING;
    rc=abk_upload_begin_image(&b->staging,data,size,samples,sample_capacity,
                              zones,zone_capacity,chunk_limit,lifetime_ms);
    if(rc<0) { b->state=ABKB_FAILED;fail(b,errno); }
    return leave(rc);
}
int abk_bank_poll(abk_bank_t *b,uint32_t timeout_ms) {
    int rc;if(enter(b)<0) return -1;
    rc=poll_locked(b,timeout_ms);return leave(rc);
}
int abk_bank_publish(abk_bank_t *b,uint32_t timeout_ms) {
    int rc;if(enter(b)<0) return -1;
    if(b->state!=ABKB_STAGED || b->closing || quiescing) return leave(fail(b,EBUSY));
    rc=submit(b,0,timeout_ms);return leave(rc);
}
int abk_bank_close(abk_bank_t *b,uint32_t timeout_ms) {
    int rc;if(enter(b)<0) return -1;
    if(b->state==ABKB_EMPTY) { b->state=ABKB_CLOSED;return leave(0); }
    b->closing=1;rc=poll_locked(b,timeout_ms);return leave(rc);
}
int abk_bank_pending_acquire(abk_bank_t *b) {
    if(enter(b)<0) return -1;
    if(b->state!=ABKB_ACTIVE || b->closing || quiescing) return leave(fail(b,EBUSY));
    if(available(b)<0) return leave(-1);
    if(b->pending==UINT32_MAX) return leave(fail(b,EOVERFLOW));
    ++b->pending;return leave(0);
}
int abk_bank_pending_release(abk_bank_t *b) {
    if(enter(b)<0) return -1;
    if(!b->pending) return leave(fail(b,EINVAL));
    --b->pending;return leave(0);
}
int abk_bank_quiesce(void) {
    unsigned i;
    if(irq_inside_int()) { errno=EPERM;return -1; }
    if(mutex_lock(&lock)<0) return -1;
    quiescing=1;
    for(i=0;i<AICA_BANK_SLOTS;++i)
        if(owners[i]) { errno=EBUSY;return leave(-1); }
    return leave(0);
}
