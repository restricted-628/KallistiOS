#include "aica_music_sequence.h"
#include "aica_music_bank.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,program_refs,starts,stops;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static aica_bank_header_t header;
static aica_bank_record_t records[128];
static aica_bank_registry_t registry;
static aica_music_bank_t bank;
static aica_music_t music;
static aica_music_sequences_t adapter;
static uint32_t generations[8];
static uint8_t image[2176];
static const uint8_t *map(uint32_t base,uint32_t bytes) {
    CHECK(base==0x30000 && bytes<=sizeof(image));return image;
}
static void put(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static int pin(void *ctx,const aica_seq_program_t *p) {
    (void)ctx;CHECK(p->events!=NULL);++program_refs;return 1;
}
static void unpin(void *ctx,const aica_seq_program_t *p) {
    (void)ctx;CHECK(p->events && program_refs);--program_refs;
}
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;CHECK(voice<64 && token && c->length==16 && records[0].voice_refs);++starts;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) {
    (void)ctx;(void)c;CHECK(voice<64);
}
static void stop(void *ctx,unsigned voice,int release) {
    (void)ctx;CHECK(voice<64 && (release==0 || release==1) && records[0].voice_refs);++stops;
}
static void init(unsigned zones) {
    /* Fresh simulated device, not a live adapter reset. The actual registry's
       ACTIVE record is supplied here; registration validation has its own suite. */
    CHECK(!program_refs);starts=stops=0;memset(image,0,sizeof(image));
    registry=(aica_bank_registry_t){.header=&header,.records=records,.map=map};
    aica_bank_registry_init(&registry);header.session=1;
    records[0]=(aica_bank_record_t){.session=1,.generation=1,.state=AICA_BANK_ACTIVE,
        .image_base=0x30000,.image_bytes=128+zones*32,.sample_count=1,.zone_count=zones};
    put(image+64,96+zones*32);put(image+68,32);put(image+72,16);put(image+76,22050);image[88]=2;
    for(unsigned i=0;i<zones;i++) {
        uint8_t *z=image+96+i*32;z[4]=(uint8_t)(i+1==zones?0:1);
        z[8]=127;z[9]=1;z[10]=127;z[11]=60;z[14]=255;z[15]=128;z[16]=31;z[19]=31;z[22]=15;
    }
    aica_music_bank_init(&bank,&registry,1);
    aica_music_ops_t ops={.resolve=aica_music_bank_resolve,.cancel_resolve=aica_music_bank_cancel,
        .retain=aica_music_bank_retain,.start=start,.update=update,.stop=stop,.context=&bank};
    CHECK(aica_music_init(&music,64,&ops)==0);
    CHECK(aica_music_sequences_init(&adapter,&music,&registry,1,pin,unpin,NULL)==0);
    for(unsigned p=0;p<8;p++) CHECK(aica_music_open(&music,p,0,1,&generations[p])==0);
}
static aica_seq_validation_t validate(const aica_seq_event_t *e,unsigned count) {
    aica_seq_validation_t v;aica_seq_program_t p={e,count,480,500000};
    CHECK(aica_seq_validate_begin(&v,&p)==0);
    int r;do { r=aica_seq_validate_step(&v); } while(!r);
    CHECK(r==1);return v;
}
static void resume_port(unsigned p) {
    unsigned calls=0;int r;
    do {
        r=aica_sequence_continue(&adapter.sequences[p]);CHECK(r>=0 && ++calls<=64);
        aica_music_service(&music,0);
    } while(!r);
}
static unsigned live_notes(void) {
    unsigned count=0;for(unsigned i=0;i<64;i++) count+=adapter.notes[i].live;return count;
}
static unsigned states(unsigned state) {
    unsigned count=0;for(unsigned i=0;i<64;i++) count+=(music.voices[i].state==state);return count;
}
static const aica_seq_event_t held[]={
    {.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=127},
    {.delta=480,.kind=AICA_SEQ_MIDI,.status=0x80,.a=60},
    {.kind=AICA_SEQ_END}};
static void pause_resume(void) {
    aica_seq_validation_t v=validate(held,3);init(1);
    CHECK(aica_music_sequence_bind(&adapter,0,generations[0])==0 && records[0].producer_refs==1);
    CHECK(aica_sequence_play(&adapter.sequences[0],&v,8)==1);
    CHECK(aica_sequence_service(&adapter.sequences[0],0)==1 && live_notes()==1);
    CHECK(aica_music_service(&music,0)==1 && starts==1 && states(AICA_MUSIC_HELD)==1);
    CHECK(aica_sequence_pause(&adapter.sequences[0])==0 && live_notes()==1);
    CHECK(stops==1 && records[0].producer_refs==1 && records[0].voice_refs==1);
    CHECK(aica_sequence_service(&adapter.sequences[0],1000000)==0);
    resume_port(0);CHECK(starts==2 && states(AICA_MUSIC_HELD)==1 && live_notes()==1);
    CHECK(aica_sequence_service(&adapter.sequences[0],500000)==2 && !program_refs && !live_notes());
    CHECK(aica_music_sequence_unbind(&adapter,0)==0 && !records[0].producer_refs);
    CHECK(records[0].voice_refs==2 && !music.count);
}
static void sustain(void) {
    const aica_seq_event_t e[]={
        {.kind=AICA_SEQ_MIDI,.status=0xb0,.a=64,.b=127},
        {.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=100},
        {.kind=AICA_SEQ_MIDI,.status=0x80,.a=60},
        {.delta=480,.kind=AICA_SEQ_END}};
    aica_seq_validation_t v=validate(e,4);init(1);
    CHECK(aica_music_sequence_bind(&adapter,0,generations[0])==0);
    CHECK(aica_sequence_play(&adapter.sequences[0],&v,8)==1);
    CHECK(aica_sequence_service(&adapter.sequences[0],0)==3);
    CHECK(aica_music_service(&music,0)==3 && states(AICA_MUSIC_SUSTAINED)==1);
    CHECK(live_notes()==1 && !adapter.notes[0].held);
    CHECK(aica_sequence_pause(&adapter.sequences[0])==0);resume_port(0);
    CHECK(states(AICA_MUSIC_SUSTAINED)==1 && starts==2);
    CHECK(aica_music_sequence_unbind(&adapter,0)==0 && !live_notes() && !program_refs);
}
static void queued_cancel(void) {
    aica_seq_validation_t v=validate(held,3);init(64);
    CHECK(aica_music_sequence_bind(&adapter,0,generations[0])==0);
    CHECK(aica_sequence_play(&adapter.sequences[0],&v,8)==1);
    CHECK(aica_sequence_service(&adapter.sequences[0],0)==1);
    CHECK(aica_music_message(&music,0,generations[0],0xb0,7,99,0)==0);
    CHECK(aica_music_message(&music,1,generations[1],0x90,72,100,0)==0);
    CHECK(aica_music_service(&music,0)==0 && records[0].producer_refs==2);
    CHECK(aica_sequence_pause(&adapter.sequences[0])==0 && music.count==2);
    CHECK(records[0].producer_refs==1 && !bank.pinned);
    CHECK(aica_music_service(&music,0)==1 && music.ports[0].channels[0].volume==99);
    CHECK(aica_music_service(&music,0)==1 && starts==1);
    resume_port(0);CHECK(starts==2);
    CHECK(aica_music_sequence_unbind(&adapter,0)==0 && !program_refs && !records[0].producer_refs);
    /* Exercise stable note-only compaction at every physical queue head. */
    for(unsigned head=0;head<64;head++) {
        music.head=(uint16_t)head;music.count=0;
        for(unsigned i=0;i<64;i++) CHECK(aica_music_message(&music,i%2,generations[i%2],
            i%3?0xb0:0x90,i,100,0)==0);
        unsigned kept=0;CHECK(aica_music_silence(&music,0,generations[0],0)==0);
        for(unsigned i=0;i<64;i++) if(i%2 || i%3) {
            aica_music_event_t *e=&music.queue[(head+kept++)%64];CHECK(e->a==i);
        }
        CHECK(music.count==kept);
    }
    music.count=0;
}
static void exhaustion(void) {
    aica_seq_event_t e[67];
    for(unsigned i=0;i<65;i++) e[i]=(aica_seq_event_t){.kind=AICA_SEQ_MIDI,.status=0x90,.a=(uint8_t)i,.b=100};
    e[65]=(aica_seq_event_t){.delta=480,.kind=AICA_SEQ_MIDI,.status=0xb0,.a=120};
    e[66]=(aica_seq_event_t){.kind=AICA_SEQ_END};
    aica_seq_validation_t v=validate(e,67);init(1);
    CHECK(aica_music_sequence_bind(&adapter,0,generations[0])==0);
    CHECK(aica_sequence_play(&adapter.sequences[0],&v,8)==1);
    while(aica_sequence_service(&adapter.sequences[0],0)==32) aica_music_service(&music,0);
    CHECK(adapter.dropped==1 && live_notes()==64);
    while(music.count) aica_music_service(&music,0);
    CHECK(starts==64);
    CHECK(aica_sequence_pause(&adapter.sequences[0])==0);resume_port(0);
    CHECK(starts==128 && live_notes()==64 && records[0].voice_refs==64);
    CHECK(aica_sequence_service(&adapter.sequences[0],500000)==2 && !live_notes() && !program_refs);
    CHECK(aica_music_sequence_unbind(&adapter,0)==0 && !records[0].producer_refs);
}
static void stale_and_ports(void) {
    aica_seq_validation_t v=validate(held,3);init(1);
    for(unsigned p=0;p<8;p++) CHECK(aica_music_sequence_bind(&adapter,p,generations[p])==0);
    CHECK(records[0].producer_refs==8);
    CHECK(aica_sequence_play(&adapter.sequences[0],&v,8)==1);
    CHECK(aica_music_close(&music,0,generations[0])==0);
    CHECK(aica_music_open(&music,0,0,1,&generations[0])==0);
    CHECK(aica_sequence_service(&adapter.sequences[0],0)==0 && adapter.sequences[0].state==AICA_SEQ_FAULT);
    CHECK(!program_refs && !starts);
    for(unsigned p=0;p<8;p++) CHECK(aica_music_sequence_unbind(&adapter,p)==0);
    CHECK(!records[0].producer_refs && !records[0].voice_refs);
}
int main(void) {
    pause_resume();sustain();queued_cancel();exhaustion();stale_and_ports();
    printf("sequence MIDI adapter: %u checks PASS; state bytes=%zu\n",checks,sizeof(adapter));return 0;
}
