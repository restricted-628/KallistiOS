#include "aica_music_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,claims,producer_refs,starts,stops,pin_error,unpin_error;
static uint64_t physical_leases;
static aica_music_t music;
static aica_music_wire_t wire;
static aica_music_receipt_t receipt;
static uint32_t cookie,g;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static int resolve(void *ctx,const aica_music_event_t *e,unsigned bank,unsigned program,
                   unsigned drum,aica_music_tone_t *t) {
    (void)ctx;(void)bank;(void)program;(void)drum;CHECK(producer_refs);
    *t=(aica_music_tone_t){.bank=e->bank,.generation=e->bank_generation,.root_key=60,
        .config={.base=0x31000,.length=16,.loopend=16,.freq=22050,.vol=255,.pan=128,.direct_level=15}};return 0;
}
static int retain(void *ctx,unsigned voice,const aica_music_tone_t *t) {
    (void)ctx;(void)t;CHECK(producer_refs);physical_leases|=(uint64_t)1<<voice;return 0;
}
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;(void)c;CHECK(token && ((physical_leases>>voice)&1));++starts;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) {
    (void)ctx;(void)c;CHECK((physical_leases>>voice)&1);
}
static void stop(void *ctx,unsigned voice,int release) {
    (void)ctx;(void)release;CHECK((physical_leases>>voice)&1);++stops;
}
static int claim(void *ctx,uint32_t low,uint32_t high) {
    (void)ctx;CHECK(low || high);++claims;return 1;
}
static int pin(void *ctx,uint32_t session,uint32_t bank,uint32_t generation) {
    (void)ctx;CHECK(session==1 && bank<128 && generation);
    if(pin_error) return 0;++producer_refs;return 1;
}
static int unpin(void *ctx,uint32_t session,uint32_t bank,uint32_t generation) {
    (void)ctx;CHECK(session==1 && bank<128 && generation && producer_refs);
    if(unpin_error) return 0;--producer_refs;return 1;
}
static aica_cmd_t *packet(unsigned op,unsigned p,uint32_t gen,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    aica_cmd_t *r=calloc(1,64);CHECK(r!=NULL);uint32_t *v=(uint32_t *)r->cmd_data;
    r->cmd=AICA_MUSIC_WIRE_COMMAND;r->size=16;r->cmd_id=++cookie;
    v[0]=1;v[1]=op;v[2]=p;v[3]=gen;v[4]=a;v[5]=b;v[6]=c;v[7]=d;return r;
}
static int command(unsigned op,unsigned p,uint32_t gen,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    aica_cmd_t *r=packet(op,p,gen,a,b,c,d);
    CHECK(aica_music_wire_command(&wire,r,1)==1);free(r);
    CHECK(receipt.cookie==cookie && !(receipt.sequence&1));return receipt.result;
}
static void init(void) {
    claims=producer_refs=starts=stops=pin_error=unpin_error=0;physical_leases=0;cookie=0;
    aica_music_ops_t ops={.resolve=resolve,.retain=retain,.start=start,.update=update,.stop=stop};
    aica_music_wire_ops_t transport={claim,pin,unpin,NULL};
    CHECK(aica_music_init(&music,64,&ops)==0);
    CHECK(aica_music_wire_init(&wire,&music,&receipt,&transport)==1);
    CHECK(receipt.ready && receipt.version==1 && receipt.sequence==0);
}
static void open_port(void) {
    CHECK(command(AICA_MUSIC_WIRE_CLAIM,0,0,15,0,0,0)==0 && music.voice_count==4 && claims==1);
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==0 && producer_refs==1);
    g=receipt.generation;CHECK(g==1 && receipt.open);
}
static void ordering(void) {
    init();open_port();
    CHECK(command(AICA_MUSIC_WIRE_MESSAGE,0,g,0x90,60,127,8)==0 && music.count==1 && !starts);
    aica_cmd_t *p=packet(AICA_MUSIC_WIRE_PARAMETER,0,g,AICA_MUSIC_VOLUME,(uint32_t)-127,0,0);
    CHECK(aica_music_wire_command(&wire,p,1)==1 && receipt.result==AICA_MUSIC_WIRE_PENDING && wire.pending);
    uint32_t seq=receipt.sequence;
    CHECK(aica_music_wire_command(&wire,p,1)==1 && receipt.sequence==seq);
    ((uint32_t *)p->cmd_data)[5]=0;
    CHECK(aica_music_wire_command(&wire,p,1)==0 && receipt.sequence==seq);
    free(p);aica_music_wire_service(&wire);CHECK(wire.pending);
    CHECK(aica_music_service(&music,0)==1 && starts==1);
    aica_music_wire_service(&wire);
    CHECK(!wire.pending && receipt.result==0 && receipt.volume==-127 && receipt.held==1 && receipt.updates_pending==1);
    aica_music_service(&music,0);
    CHECK(command(AICA_MUSIC_WIRE_QUERY,0,g,0,0,0,0)==0 && !receipt.updates_pending);
    CHECK(command(AICA_MUSIC_WIRE_MUTE,0,g,1,0,0,0)==0 && stops==1 && receipt.active==0);
    CHECK(command(AICA_MUSIC_WIRE_CLOSE,0,g,0,0,0,0)==0 && !producer_refs && physical_leases);
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==0 && receipt.generation==g+1);
    CHECK(command(AICA_MUSIC_WIRE_MESSAGE,0,g,0x90,60,127,8)==AICA_MUSIC_INVALID);
    CHECK(command(AICA_MUSIC_WIRE_QUIESCE,0,0,0,0,0,0)==0 && !producer_refs && (receipt.flags&2));
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==AICA_MUSIC_BUSY);
}
static void admission_and_quiesce(void) {
    init();open_port();
    for(unsigned i=0;i<64;i++) CHECK(command(AICA_MUSIC_WIRE_MESSAGE,0,g,0x90,60,100,8)==0);
    CHECK(command(AICA_MUSIC_WIRE_MESSAGE,0,g,0x90,60,100,8)==AICA_MUSIC_BUSY && music.count==64);
    CHECK(command(AICA_MUSIC_WIRE_GM,0,g,1,0,0,0)==AICA_MUSIC_WIRE_PENDING);
    uint32_t pending_cookie=cookie;
    aica_cmd_t *bad=packet(AICA_MUSIC_WIRE_QUIESCE,1,0,0,0,0,0);
    CHECK(aica_music_wire_command(&wire,bad,1)==0 && wire.pending && receipt.cookie==pending_cookie);free(bad);
    CHECK(command(AICA_MUSIC_WIRE_QUIESCE,0,0,0,0,0,0)==0);
    CHECK(receipt.cancelled_cookie==pending_cookie && !wire.pending && !music.count && !producer_refs && !starts);
    aica_music_wire_service(&wire);CHECK(!music.ports[0].gm);
}
static void failures(void) {
    init();
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==AICA_MUSIC_BUSY);
    CHECK(command(AICA_MUSIC_WIRE_CLAIM,0,0,0,0,0,0)==AICA_MUSIC_INVALID && !claims);
    CHECK(command(AICA_MUSIC_WIRE_CLAIM,0,0,0,0x80000000,0,0)==0 && music.voice_count==1);
    pin_error=1;CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==AICA_MUSIC_MISSING && !producer_refs);
    pin_error=0;CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==0);g=receipt.generation;
    unpin_error=1;CHECK(command(AICA_MUSIC_WIRE_CLOSE,0,g,0,0,0,0)==AICA_MUSIC_FAULT);
    CHECK(wire.bank_pins==1 && producer_refs==1 && wire.faulted);
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==AICA_MUSIC_FAULT);
    unpin_error=0;CHECK(command(AICA_MUSIC_WIRE_QUIESCE,0,0,0,0,0,0)==AICA_MUSIC_FAULT && !producer_refs);
    init();open_port();receipt.sequence=UINT32_MAX-5;
    CHECK(command(AICA_MUSIC_WIRE_MESSAGE,0,g,0x90,60,127,8)==0);
    aica_cmd_t *p=packet(AICA_MUSIC_WIRE_QUERY,0,g,0,0,0,0);
    CHECK(!aica_music_wire_command(&wire,p,1) && receipt.sequence==UINT32_MAX && wire.faulted);free(p);
    CHECK(producer_refs==1); /* Protocol exhaustion never implies safe unpin. */
}
static void malformed(void) {
    init();open_port();
    for(unsigned field=0;field<8;field++) {
        aica_cmd_t *p=packet(AICA_MUSIC_WIRE_QUERY,0,g,0,0,0,0);
        uint32_t *words=(uint32_t *)p;words[field]^=0x80000000u;
        uint32_t seq=receipt.sequence;
        if(field==3) { /* command id remains a valid larger cookie */
            CHECK(aica_music_wire_command(&wire,p,1)==1);
            cookie=p->cmd_id;
        }
        else CHECK(!aica_music_wire_command(&wire,p,1) && receipt.sequence==seq);
        free(p);
    }
    aica_cmd_t *p=packet(AICA_MUSIC_WIRE_QUERY,0,g,0,0,0,0);
    CHECK(!aica_music_wire_command(&wire,p,2));
    ((uint32_t *)p->cmd_data)[0]=2;CHECK(!aica_music_wire_command(&wire,p,1));free(p);
    CHECK(producer_refs==1 && !starts);
}
static void all_ports(void) {
    init();CHECK(command(AICA_MUSIC_WIRE_CLAIM,0,0,UINT32_MAX,UINT32_MAX,0,0)==0);
    CHECK(music.voice_count==64);
    for(unsigned p=0;p<8;p++) {
        CHECK(command(AICA_MUSIC_WIRE_OPEN,p,0,p,1,0,0)==0);
        CHECK(command(AICA_MUSIC_WIRE_PARAMETER,p,receipt.generation,0,100,0,0)==0);
    }
    CHECK(producer_refs==8 && wire.bank_pins==255);
    CHECK(command(AICA_MUSIC_WIRE_RESET_ALL,0,0,0,0,0,0)==0);
    for(unsigned p=0;p<8;p++) CHECK(music.ports[p].fades[0].value==0);
    CHECK(command(AICA_MUSIC_WIRE_CLOSE_ALL,0,0,0,0,0,0)==0 && !producer_refs);
    CHECK(!music.quiescing && wire.claimed && claims==1);
    CHECK(command(AICA_MUSIC_WIRE_OPEN,0,0,0,1,0,0)==0 && receipt.generation==2);
    CHECK(command(AICA_MUSIC_WIRE_CLOSE_ALL,0,0,0,0,0,0)==0 && !producer_refs);
}
int main(void) {
    ordering();admission_and_quiesce();failures();malformed();all_ports();
    printf("private music wire: %u checks PASS; receipt bytes=%zu\n",checks,sizeof(receipt));return 0;
}
