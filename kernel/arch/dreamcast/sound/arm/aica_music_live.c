#include "aica_music_live.h"
#include "aica.h"
static void clear(void *p, unsigned n) {
    uint8_t *b=p; while(n--) *b++=0;
}
static void fault(aica_music_live_t *s) {
    s->music.faulted=1;
    if(s->backend.failures!=UINT32_MAX) ++s->backend.failures;
}
int aica_music_live_owned(const aica_music_live_t *s, unsigned channel) {
    return channel<64 && s->backend.claimed &&
        (s->backend.masks[channel/32] & (1u<<(channel%32)))!=0;
}
int aica_music_live_intersects(const aica_music_live_t *s, uint32_t low, uint32_t high) {
    return s->backend.claimed && ((s->backend.masks[0]&low) || (s->backend.masks[1]&high));
}
uint32_t aica_music_clock_step(aica_music_backend_t *b, uint32_t tick) {
    uint32_t delta, n, result;
    if(!b->clock_ready) { b->last_tick=tick;b->remainder=0;b->clock_ready=1;return 0; }
    delta=tick-b->last_tick;b->last_tick=tick;
    n=(delta%441u)*100u+b->remainder;
    result=(delta/441u)*100u+n/441u;b->remainder=n%441u;
    return result;
}
void aica_music_clock_rebase(aica_music_backend_t *b) {
    b->clock_ready=0;b->remainder=0;
}
static int claim(void *context, uint32_t low, uint32_t high) {
    aica_music_live_t *s=context;unsigned count=0;
    if(s->music.faulted || s->wire.faulted || s->backend.claimed || (!low && !high)) return 0;
    if(!aica_music_hw_invalidate(low, high)) { fault(s);return 0; }
    for(unsigned i=0;i<64;i++)
        if((i<32?low:high)&(1u<<(i%32))) s->backend.physical[count++]=(uint8_t)i;
    s->backend.masks[0]=low;s->backend.masks[1]=high;
    s->backend.voices=(uint8_t)count;s->backend.claimed=1;
    return 1;
}
static int pin(void *context, uint32_t session, uint32_t bank, uint32_t generation) {
    aica_music_live_t *s=context;
    return aica_bank_registry_pin(s->bank.registry,session,bank,generation,AICA_BANK_PIN_PRODUCER);
}
static int unpin(void *context, uint32_t session, uint32_t bank, uint32_t generation) {
    aica_music_live_t *s=context;
    return aica_bank_registry_unpin(s->bank.registry,session,bank,generation,AICA_BANK_PIN_PRODUCER);
}
static int resolve(void *context, const aica_music_event_t *e, unsigned bank,
    unsigned program, unsigned drum, aica_music_tone_t *tone,unsigned *zone_budget) {
    aica_music_live_t *s=context;
    return aica_music_bank_resolve(&s->bank,e,bank,program,drum,tone,zone_budget);
}
static void cancel(void *context) {
    aica_music_live_t *s=context;aica_music_bank_cancel(&s->bank);
}
static int retain(void *context, unsigned voice, const aica_music_tone_t *tone) {
    aica_music_live_t *s=context;
    return aica_music_bank_retain(&s->bank,voice,tone);
}
static int prepare(aica_music_live_t *s, unsigned voice, unsigned op,
    const aica_channel_config_t *c, uint32_t fields) {
    unsigned physical;
    if(s->wire.faulted) fault(s);
    if(s->music.faulted) return 0;
    if(!s->backend.claimed || voice>=s->backend.voices) { fault(s);return 0; }
    physical=s->backend.physical[voice];
    /* STOP has no config fields. Check it here before invalidation so it does
       not go through the shared ARM boolean-result validator. */
    if((op==AICA_CHANNEL_OP_STOP ? fields!=0 :
        !aica_music_hw_validate(physical,op,c,fields)) ||
       !aica_music_hw_invalidate(physical<32?1u<<physical:0,
                                 physical>=32?1u<<(physical-32):0)) {
        fault(s);return 0;
    }
    return 1;
}
static int start(void *context, unsigned voice, uint32_t token,
    const aica_channel_config_t *c) {
    aica_music_live_t *s=context;
    if(!token || voice>=64 || token<=s->backend.start_token[voice]) { fault(s);return AICA_MUSIC_FAULT; }
    if(!prepare(s,voice,AICA_CHANNEL_OP_START,c,0)) return AICA_MUSIC_FAULT;
    s->backend.start_token[voice]=token;
    aica_music_hw_apply(s->backend.physical[voice],AICA_CHANNEL_OP_START,c,0);
    return 0;
}
static void update(void *context, unsigned voice, const aica_channel_config_t *c) {
    aica_music_live_t *s=context;
    const uint32_t fields=AICA_CHANNEL_UPDATE_FREQUENCY|AICA_CHANNEL_UPDATE_VOLUME|
        AICA_CHANNEL_UPDATE_PAN|AICA_CHANNEL_UPDATE_ROUTING|AICA_CHANNEL_UPDATE_LFO;
    if(prepare(s,voice,AICA_CHANNEL_OP_UPDATE,c,fields))
        aica_music_hw_apply(s->backend.physical[voice],AICA_CHANNEL_OP_UPDATE,c,fields);
}
static void stop(void *context, unsigned voice, int release) {
    aica_music_live_t *s=context;aica_channel_config_t c;
    if(s->wire.faulted) fault(s);
    if(s->music.faulted) return;
    if(!s->backend.claimed || voice>=s->backend.voices) { fault(s);return; }
    if(!release) {
        clear(&c,sizeof(c));
        if(!aica_music_hw_validate(s->backend.physical[voice],AICA_CHANNEL_OP_UPDATE,&c,AICA_CHANNEL_UPDATE_VOLUME)) {
            fault(s);return;
        }
    }
    if(!prepare(s,voice,AICA_CHANNEL_OP_STOP,0,0)) return;
    if(!release)
        aica_music_hw_apply(s->backend.physical[voice],AICA_CHANNEL_OP_UPDATE,&c,AICA_CHANNEL_UPDATE_VOLUME);
    aica_music_hw_apply(s->backend.physical[voice],AICA_CHANNEL_OP_STOP,0,0);
}
static int ended(void *context, unsigned voice, uint32_t token) {
    (void)context;(void)voice;(void)token;
    return 0; /* No key-off/LP inference; retains logical release voices. */
}
void aica_music_live_init(aica_music_live_t *s, aica_bank_registry_t *registry,
    volatile aica_music_receipt_t *receipt) {
    aica_music_ops_t ops={resolve,cancel,retain,start,update,stop,ended,s};
    aica_music_wire_ops_t wire_ops={claim,pin,unpin,s};
    clear(s,sizeof(*s));
    aica_music_init(&s->music,64,&ops);
    aica_music_bank_init(&s->bank,registry,0);
    aica_music_wire_init(&s->wire,&s->music,receipt,&wire_ops);
}
int aica_music_live_command(aica_music_live_t *s, const aica_cmd_t *p, uint32_t session) {
    if(!session || (s->backend.session && session!=s->backend.session)) return 0;
    s->backend.session=session;s->bank.session=session;
    return aica_music_wire_command(&s->wire,p,session);
}
void aica_music_live_service(aica_music_live_t *s, uint32_t tick) {
    uint32_t elapsed=aica_music_clock_step(&s->backend,tick);
    if(s->wire.faulted) fault(s);
    aica_music_wire_service_input(&s->wire, aica_midi_input_read);
    aica_music_service(&s->music,elapsed);
    aica_music_wire_service(&s->wire);
    s->backend.quiesced=s->music.quiescing;
}
