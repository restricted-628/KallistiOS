/* Native format only. Byte reads preserve both ARM and SH4 alignment rules. */
#include "aica_music_bank.h"
#include <stddef.h>
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8; }
static uint32_t le32(const uint8_t *p) { return le16(p)|le16(p+2)<<16; }
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
void aica_music_bank_init(aica_music_bank_t *b,aica_bank_registry_t *r,uint32_t session) {
    clear(b,sizeof(*b));b->registry=r;b->session=session;
}
void aica_music_bank_cancel(void *context) {
    aica_music_bank_t *b=context;
    if(!b || !b->pinned) return;
    /* If sequence exhaustion prevents unpin, retain the pin conservatively.
       The registry is then faulted and cannot unload or accept another pin. */
    aica_bank_registry_unpin(b->registry,b->session,b->pending.bank,
                             b->pending.bank_generation,2);
    b->pinned=0;b->cursor=0;
}
static int same(const aica_music_event_t *a,const aica_music_event_t *b) {
    return a->generation==b->generation && a->bank==b->bank &&
        a->bank_generation==b->bank_generation && a->port==b->port &&
        a->channel==b->channel && a->status==b->status && a->a==b->a &&
        a->b==b->b && a->priority==b->priority;
}
int aica_music_bank_resolve(void *context,const aica_music_event_t *e,
                            unsigned bank,unsigned program,unsigned drum,
                            aica_music_tone_t *out) {
    aica_music_bank_t *b=context;volatile aica_bank_record_t *r;
    const uint8_t *image;unsigned budget;
    if(!b || !b->registry || !b->registry->map || !e || !out ||
       e->bank>=AICA_BANK_SLOTS || !e->bank_generation || bank>127 ||
       program>127 || drum>1 || e->a>127 || !e->b || e->b>127)
        return AICA_MUSIC_INVALID;
    if(b->pinned && (!same(&b->pending,e) || b->bank!=bank ||
                      b->program!=program || b->drum!=drum)) aica_music_bank_cancel(b);
    if(!b->pinned) {
        if(!aica_bank_registry_pin(b->registry,b->session,e->bank,e->bank_generation,2))
            return AICA_MUSIC_MISSING;
        b->pinned=1;b->pending=*e;b->cursor=0;
        b->bank=(uint8_t)bank;b->program=(uint8_t)program;b->drum=(uint8_t)drum;
    }
    r=&b->registry->records[e->bank];
    if(r->sequence==UINT32_MAX || r->state!=AICA_BANK_ACTIVE ||
       r->generation!=e->bank_generation || r->session!=b->session)
        return AICA_MUSIC_FAULT;
    image=b->registry->map(r->image_base,r->image_bytes);
    if(!image) return AICA_MUSIC_MISSING;
    for(budget=0;budget<AICA_MUSIC_ZONE_BUDGET && b->cursor<r->zone_count;budget++) {
        const uint8_t *z=image+64+(r->sample_count+b->cursor++)*32,*s;
        aica_channel_config_t *c;int fine;
        if(z[4]!=bank || z[5]!=program || z[6]!=drum || e->a<z[7] ||
           e->a>z[8] || e->b<z[9] || e->b>z[10]) continue;
        /* Registry validated all offsets/counts. Its producer pin keeps this
           generation immutable until a voice lease exists or lookup cancels. */
        s=image+64+le32(z)*32;
        clear(out,sizeof(*out));out->bank=e->bank;out->generation=e->bank_generation;
        out->root_key=z[11];fine=(int)le16(z+12);if(fine>=32768) fine-=65536;
        out->fine_cents=(int16_t)fine;c=&out->config;
        c->base=r->image_base+le32(s);c->type=le16(s+24)==1?AICA_SM_8BIT:AICA_SM_16BIT;
        c->length=le32(s+8);c->freq=le32(s+12);c->loop=le16(s+26);
        c->loopstart=c->loop?le32(s+16):0;c->loopend=c->loop?le32(s+20):c->length;
        c->vol=z[14];c->pan=z[15];c->attack_rate=z[16];c->decay1_rate=z[17];
        c->decay2_rate=z[18];c->release_rate=z[19];c->decay_level=z[20];
        c->key_rate_scaling=z[21];c->direct_level=z[22];
        c->effect_channel=z[23];c->effect_send=z[24];return 0;
    }
    return b->cursor<r->zone_count?AICA_MUSIC_RETRY:AICA_MUSIC_MISSING;
}
int aica_music_bank_retain(void *context,unsigned voice,const aica_music_tone_t *tone) {
    aica_music_bank_t *b=context;uint32_t mask;
    if(!b || !tone || voice>=AICA_MUSIC_VOICES || tone->bank>=AICA_BANK_SLOTS ||
       !b->pinned || tone->bank!=b->pending.bank ||
       tone->generation!=b->pending.bank_generation) return AICA_MUSIC_INVALID;
    mask=1u<<(voice%32);
    if(b->leases[tone->bank][voice/32]&mask)
        return b->generation[tone->bank]==tone->generation?0:AICA_MUSIC_FAULT;
    if((b->leases[tone->bank][0] || b->leases[tone->bank][1]) &&
       b->generation[tone->bank]!=tone->generation) return AICA_MUSIC_FAULT;
    if(!aica_bank_registry_pin(b->registry,b->session,tone->bank,tone->generation,1))
        return AICA_MUSIC_BUSY;
    b->generation[tone->bank]=tone->generation;
    b->leases[tone->bank][voice/32]|=mask;return 0;
}
