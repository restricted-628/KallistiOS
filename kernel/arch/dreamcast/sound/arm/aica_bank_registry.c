/* Original bounded resident-bank registry. No proprietary code or asset format. */
#include "aica_bank_registry.h"
#define NONE AICA_BANK_SLOTS
static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8; }
static uint32_t u32(const uint8_t *p) { return u16(p)|u16(p+2)<<16; }
static int zero(const uint8_t *p, unsigned n) {
    while(n--) if(*p++) return 0;
    return 1;
}
static int begin(volatile uint32_t *sequence) {
    uint32_t s=*sequence;
    if(s & 1u) return 0;
    if(s>=UINT32_MAX-2u) { *sequence=UINT32_MAX; return 0; }
    *sequence=s+1;
    return 1;
}
static void end(volatile uint32_t *sequence) { *sequence=*sequence+1; }
static int extent(uint32_t base,uint32_t bytes) {
    return base>=0x30000u && base<0x200000u && !(base&31u) &&
           bytes>=128 && !(bytes&31u) && bytes<=0x200000u-base;
}
static void enqueue(aica_bank_registry_t *b,unsigned slot) {
    b->next[slot]=NONE;
    if(b->head==NONE) b->head=(uint16_t)slot;
    else b->next[b->tail]=(uint16_t)slot;
    b->tail=(uint16_t)slot;
}
void aica_bank_registry_init(aica_bank_registry_t *b) {
    unsigned i,j;
    volatile uint32_t *h=(volatile uint32_t *)b->header;
    for(i=0;i<16;++i) h[i]=0;
    for(i=0;i<AICA_BANK_SLOTS;++i) {
        volatile uint32_t *r=(volatile uint32_t *)&b->records[i];
        for(j=0;j<16;++j) r[j]=0;
        b->cursor[i]=b->end[i]=0; b->next[i]=NONE;
        b->address[i]=b->bytes[i]=b->samples[i]=b->zones[i]=0;
    }
    b->head=b->tail=NONE;
    b->header->layout_version=AICA_BANK_LAYOUT_VERSION;
    b->header->slot_count=AICA_BANK_SLOTS;
    b->header->ready=1;
}
static int header(const uint8_t *p,uint32_t size,uint32_t *ns,uint32_t *nz) {
    static const uint8_t magic[8]={'K','O','S','A','B','K',0,0};
    uint32_t i,po;
    for(i=0;i<8;++i) if(p[i]!=magic[i]) return 0;
    if(u16(p+8)!=1 || u16(p+10)!=64 || u32(p+12)!=size ||
       u32(p+44) || !zero(p+48,16)) return 0;
    *ns=u32(p+16);*nz=u32(p+20);
    if(!*ns || *ns>256 || !*nz || *nz>1024) return 0;
    po=64+(*ns+*nz)*32;
    return po<size && u32(p+24)==64 && u32(p+28)==64+*ns*32 &&
           u32(p+32)==po && u32(p+36)==size-po;
}
int aica_bank_registry_command(aica_bank_registry_t *b,const aica_cmd_t *p,
                               uint32_t active_session) {
    const uint32_t *v=(const uint32_t *)p->cmd_data;
    volatile aica_bank_record_t *r;
    uint32_t slot,result=AICA_BANK_OK;
    if((p->cmd!=AICA_CMD_BANK_REGISTER && p->cmd!=AICA_CMD_BANK_UNREGISTER) ||
       p->size!=(p->cmd==AICA_CMD_BANK_REGISTER?14u:11u) || p->timestamp ||
       p->misc[0] || p->misc[1] || p->misc[2] || p->misc[3] || !p->cmd_id)
        return 0;
    if(!active_session || v[0]!=active_session || v[1]>=AICA_BANK_SLOTS ||
       (b->header->session && b->header->session!=active_session)) return 0;
    slot=v[1];r=&b->records[slot];
    /* Replay is checked before ANY map/dereference of an image. */
    if(p->cmd_id==r->cookie) return r->operation==p->cmd;
    if(p->cmd_id<r->cookie || r->state==AICA_BANK_VALIDATING) return 0;
    if(!b->header->session) {
        if(!begin(&b->header->sequence)) return 0;
        b->header->session=active_session;end(&b->header->sequence);
    }
    if(!begin(&r->sequence)) return 0;
    r->session=active_session;r->cookie=p->cmd_id;r->operation=p->cmd;
    if(p->cmd==AICA_CMD_BANK_UNREGISTER) {
        if(r->state!=AICA_BANK_ACTIVE || v[2]!=r->generation) result=AICA_BANK_STALE;
        else if(r->queued_refs || r->voice_refs || r->producer_refs) result=AICA_BANK_BUSY;
        else {
            r->state=AICA_BANK_EMPTY;r->image_base=r->image_bytes=0;
            r->sample_count=r->zone_count=0;
        }
    }
    else if(r->state!=AICA_BANK_EMPTY) result=AICA_BANK_BUSY;
    else if(r->generation==UINT32_MAX) result=AICA_BANK_EXHAUSTED;
    else if(v[2]!=r->generation+1) result=AICA_BANK_STALE;
    else if(v[5]!=1 || !extent(v[3],v[4])) result=AICA_BANK_BAD_IMAGE;
    else {
        /* Header validation also consumes the shared service budget. */
        b->address[slot]=v[3];b->bytes[slot]=v[4];
        b->samples[slot]=b->zones[slot]=0;r->state=AICA_BANK_VALIDATING;
        b->cursor[slot]=b->end[slot]=0;
        enqueue(b,slot);result=AICA_BANK_PENDING;
    }
    r->result=result;end(&r->sequence);return 1;
}
static int sample(aica_bank_registry_t *b,unsigned slot,const uint8_t *image) {
    const uint8_t *p=image+64+(b->cursor[slot]-1)*32;
    uint32_t off=u32(p),bytes=u32(p+4),frames=u32(p+8),rate=u32(p+12);
    uint32_t ls=u32(p+16),le=u32(p+20),enc=u16(p+24),loop=u16(p+26),next;
    if((enc!=1 && enc!=2) || loop>1 || u32(p+28) || !frames || frames>65534 ||
       bytes!=frames*enc || !rate || rate>192000 || off!=b->end[slot] ||
       off>b->bytes[slot] || bytes>b->bytes[slot]-off ||
       (!loop && (ls || le)) || (loop && (ls>=le || le>frames))) return 0;
    next=(off+bytes+31u)&~31u;
    if(next>b->bytes[slot] || !zero(image+off+bytes,(unsigned)(next-off-bytes))) return 0;
    b->end[slot]=next;return 1;
}
static int zone(const uint8_t *p,uint32_t ns) {
    uint32_t i;int fine=(int)u16(p+12);
    if(fine>=32768) fine-=65536;
    if(u32(p)>=ns || !zero(p+25,7) || p[4]>127 || p[5]>127 || p[6]>1 ||
       p[7]>p[8] || p[8]>127 || !p[9] || p[9]>p[10] || p[10]>127 ||
       p[11]>127 || fine < -100 || fine > 100) return 0;
    for(i=16;i<=20;++i) if(p[i]>31) return 0;
    for(i=21;i<=24;++i) if(p[i]>15) return 0;
    return 1;
}
unsigned aica_bank_registry_service(aica_bank_registry_t *b) {
    unsigned budget=0;
    while(budget<AICA_BANK_BUDGET && b->head!=NONE) {
        unsigned slot=b->head;
        volatile aica_bank_record_t *r=&b->records[slot];
        const uint8_t *image;
        int valid;
        b->head=b->next[slot];if(b->head==NONE) b->tail=NONE;
        b->next[slot]=NONE;
        ++budget;
        /* An exhausted sequence is permanently faulted; never publish ACTIVE. */
        if(r->sequence==UINT32_MAX) continue;
        image=b->map(b->address[slot],b->bytes[slot]);
        if(!b->cursor[slot]) {
            valid=image && header(image,b->bytes[slot],&b->samples[slot],&b->zones[slot]);
            if(valid) {
                b->end[slot]=64+(b->samples[slot]+b->zones[slot])*32;
                b->cursor[slot]=1;enqueue(b,slot);continue;
            }
        }
        else valid=image && (b->cursor[slot]<=b->samples[slot] ? sample(b,slot,image) :
            zone(image+64+(b->cursor[slot]-1)*32,b->samples[slot]));
        ++b->cursor[slot];
        if(valid && b->cursor[slot]==b->samples[slot]+1)
            valid=b->end[slot]==b->bytes[slot];
        if(!valid || b->cursor[slot]==b->samples[slot]+b->zones[slot]+1) {
            if(!begin(&r->sequence)) continue;
            if(valid) {
                r->image_base=b->address[slot];r->image_bytes=b->bytes[slot];
                r->sample_count=b->samples[slot];r->zone_count=b->zones[slot];
                ++r->generation;r->state=AICA_BANK_ACTIVE;r->result=AICA_BANK_OK;
            }
            else {
                r->state=AICA_BANK_EMPTY;r->result=AICA_BANK_BAD_IMAGE;
                r->image_base=r->image_bytes=r->sample_count=r->zone_count=0;
            }
            end(&r->sequence);
        }
        else enqueue(b,slot);
    }
    return budget;
}
static volatile aica_bank_record_t *active(aica_bank_registry_t *b,uint32_t session,
                                          uint32_t slot,uint32_t generation) {
    volatile aica_bank_record_t *r;
    if(!session || slot>=AICA_BANK_SLOTS || b->header->session!=session) return 0;
    r=&b->records[slot];
    return r->state==AICA_BANK_ACTIVE && r->generation==generation &&
           r->sequence!=UINT32_MAX ? r : 0;
}
static int reference(aica_bank_registry_t *b,uint32_t session,uint32_t slot,
                     uint32_t generation,unsigned kind,int add) {
    volatile aica_bank_record_t *r=active(b,session,slot,generation);
    volatile uint32_t *count;
    if(!r || kind>2) return 0;
    count=kind==0?&r->queued_refs:kind==1?&r->voice_refs:&r->producer_refs;
    if((add && *count==UINT32_MAX) || (!add && !*count)) return 0;
    if(!begin(&r->sequence)) return 0;
    if(add) ++*count;else --*count;
    end(&r->sequence);return 1;
}
int aica_bank_registry_pin(aica_bank_registry_t *b,uint32_t session,uint32_t slot,
                           uint32_t generation,unsigned kind) {
    return reference(b,session,slot,generation,kind,1);
}
int aica_bank_registry_unpin(aica_bank_registry_t *b,uint32_t session,uint32_t slot,
                             uint32_t generation,unsigned kind) {
    return reference(b,session,slot,generation,kind,0);
}
int aica_bank_registry_promote(aica_bank_registry_t *b,uint32_t session,uint32_t slot,
                               uint32_t generation) {
    volatile aica_bank_record_t *r=active(b,session,slot,generation);
    if(!r || !r->queued_refs || r->voice_refs==UINT32_MAX || !begin(&r->sequence)) return 0;
    --r->queued_refs;++r->voice_refs;end(&r->sequence);return 1;
}
