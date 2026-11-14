#include "aica_music_sequence.h"
#include <stddef.h>
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static int valid(aica_music_sequence_binding_t *b) {
    aica_music_port_t *p=&b->owner->music->ports[b->port];
    return b->bound && p->open && p->generation==b->generation &&
        p->bank==b->bank && p->bank_generation==b->bank_generation;
}
static int retain(void *ctx,const aica_seq_program_t *p) {
    aica_music_sequence_binding_t *b=ctx;
    return valid(b) && b->owner->retain_program(b->owner->context,p)>0;
}
static void release(void *ctx,const aica_seq_program_t *p) {
    aica_music_sequence_binding_t *b=ctx;b->owner->release_program(b->owner->context,p);
}
static void silence(void *ctx,int release_notes,int pause) {
    aica_music_sequence_binding_t *b=ctx;aica_music_sequences_t *a=b->owner;
    if(valid(b)) aica_music_silence(a->music,b->port,b->generation,(unsigned)release_notes);
    b->cursor=0;b->resuming=(uint8_t)(pause!=0);
    if(!pause) for(unsigned i=0;i<AICA_TIMED_NOTES;i++)
        if(a->notes[i].live && a->notes[i].port==b->port) a->notes[i].live=0;
}
static void note_off(aica_music_sequence_binding_t *b,unsigned channel,unsigned key) {
    for(unsigned i=0;i<AICA_TIMED_NOTES;i++) {
        aica_timed_note_t *n=&b->owner->notes[i];
        if(n->live && n->port==b->port && n->channel==channel && n->key==key) {
            n->held=0;if(!b->sustain[channel]) n->live=0;
        }
    }
}
static void control(aica_music_sequence_binding_t *b,unsigned channel,unsigned cc,unsigned value) {
    if(cc==64 || cc==121) {
        b->sustain[channel]=(uint8_t)(cc==64 && value>=64);
        if(!b->sustain[channel]) for(unsigned i=0;i<AICA_TIMED_NOTES;i++) {
            aica_timed_note_t *n=&b->owner->notes[i];
            if(n->live && n->port==b->port && n->channel==channel && !n->held) n->live=0;
        }
    }
    else if(cc==120 || cc==123) for(unsigned i=0;i<AICA_TIMED_NOTES;i++) {
        aica_timed_note_t *n=&b->owner->notes[i];
        if(n->live && n->port==b->port && n->channel==channel) {
            n->held=0;if(cc==120 || !b->sustain[channel]) n->live=0;
        }
    }
}
static int emit(void *ctx,unsigned status,unsigned key,unsigned velocity,unsigned priority) {
    aica_music_sequence_binding_t *b=ctx;aica_music_sequences_t *a=b->owner;
    unsigned channel=status&15,kind=status&0xf0,index=AICA_TIMED_NOTES;int result;
    if(!valid(b)) return -1;
    if(kind==0x90 && velocity) {
        for(index=0;index<AICA_TIMED_NOTES && a->notes[index].live;index++) { }
        /* A finite timed-note table must not deadlock the sequence before its
           later note-offs. Deterministic drop is exposed, not queue backpressure. */
        if(index==AICA_TIMED_NOTES) {
            if(a->dropped!=UINT32_MAX) ++a->dropped;
            return 1;
        }
    }
    result=aica_music_message(a->music,b->port,b->generation,status,key,velocity,priority);
    if(result==AICA_MUSIC_BUSY) return 0;
    if(result) return -1;
    if(index<AICA_TIMED_NOTES) {
        /* Explicit byte stores avoid an implicit memset dependency in the
           freestanding ARM compiler's aggregate-initialization lowering. */
        aica_timed_note_t *n=&a->notes[index];
        n->live=1;n->port=b->port;n->channel=(uint8_t)channel;n->key=(uint8_t)key;
        n->velocity=(uint8_t)velocity;n->held=1;n->priority=(uint8_t)priority;n->reserved=0;
    }
    else if(kind==0x80 || (kind==0x90 && !velocity)) note_off(b,channel,key);
    else if(kind==0xb0) control(b,channel,key,velocity);
    return 1;
}
static int resume(void *ctx) {
    aica_music_sequence_binding_t *b=ctx;aica_music_sequences_t *a=b->owner;
    unsigned visits=0,admitted=0;
    if(!valid(b)) return -1;
    /* Let retained controller/program changes, and each previously admitted
       resume batch, complete before advancing to the next batch. */
    for(unsigned i=0;i<a->music->count;i++) {
        aica_music_event_t *e=&a->music->queue[(a->music->head+i)%AICA_MUSIC_QUEUE];
        if(e->port==b->port && e->generation==b->generation) return 0;
    }
    while(b->cursor<AICA_TIMED_NOTES && visits<16 && admitted<4) {
        aica_timed_note_t *n=&a->notes[b->cursor];int result;
        ++visits;
        if(!n->live || n->port!=b->port) { ++b->cursor;continue; }
        if(a->music->count>AICA_MUSIC_QUEUE-(n->held?1u:2u)) return 0;
        result=aica_music_message(a->music,b->port,b->generation,0x90u+n->channel,
                                  n->key,n->velocity,n->priority);
        if(result==AICA_MUSIC_BUSY) return 0;
        if(result) return -1;
        if(!n->held && aica_music_message(a->music,b->port,b->generation,0x80u+n->channel,
                                         n->key,0,n->priority)) return -1;
        ++b->cursor;++admitted;
    }
    if(b->cursor<AICA_TIMED_NOTES) return 0;
    b->resuming=0;return 1;
}
int aica_music_sequences_init(aica_music_sequences_t *a,aica_music_t *m,
    aica_bank_registry_t *registry,uint32_t session,
    int (*pin)(void *,const aica_seq_program_t *),
    void (*unpin)(void *,const aica_seq_program_t *),void *context) {
    if(!a || !m || !registry || !session || !pin || !unpin) return -1;
    clear(a,sizeof(*a));a->music=m;a->registry=registry;a->session=session;
    a->retain_program=pin;a->release_program=unpin;a->context=context;
    for(unsigned p=0;p<8;p++) {
        aica_seq_ops_t ops={retain,release,emit,silence,resume,&a->bindings[p]};
        a->bindings[p].owner=a;a->bindings[p].port=(uint8_t)p;
        if(aica_sequence_init(&a->sequences[p],&ops)) return -1;
    }
    return 0;
}
int aica_music_sequence_bind(aica_music_sequences_t *a,unsigned port,uint32_t generation) {
    aica_music_sequence_binding_t *b;aica_music_port_t *p;
    if(!a || port>=8 || !generation) return -1;
    b=&a->bindings[port];p=&a->music->ports[port];
    if(b->bound || !p->open || p->generation!=generation) return -1;
    if(!aica_bank_registry_pin(a->registry,a->session,p->bank,p->bank_generation,2)) return -2;
    b->generation=generation;b->bank=p->bank;b->bank_generation=p->bank_generation;
    b->bound=1;b->cursor=b->resuming=0;
    for(unsigned ch=0;ch<16;ch++) b->sustain[ch]=p->channels[ch].sustain;
    return 0;
}
int aica_music_sequence_unbind(aica_music_sequences_t *a,unsigned port) {
    aica_music_sequence_binding_t *b;
    if(!a || port>=8 || !a->bindings[port].bound) return -1;
    b=&a->bindings[port];aica_sequence_stop(&a->sequences[port]);
    if(!aica_bank_registry_unpin(a->registry,a->session,b->bank,b->bank_generation,2)) return -2;
    b->bound=0;return 0;
}
