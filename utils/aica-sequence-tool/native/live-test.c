#include "live.h"
#include "live-client.h"
#include "aica_music_bank.h"
#include "live-fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,starts,stops;
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static aica_bank_header_t header;
static aica_bank_record_t records[128];
static aica_bank_registry_t registry;
static aica_music_bank_t bank;
static aica_music_t music;
static aica_music_sequences_t adapter;
static aica_music_wire_t mw;
static aica_music_receipt_t mr;
static asq_catalog_t catalog;
static asql_t live;
static asql_receipt_t receipt;
static aica_seq_event_t events[2][256];
static unsigned char sources[2][sizeof(score)],image[2176];
static uint32_t cookie,mcookie,tick;
static const uint8_t *map(uint32_t base,uint32_t bytes) { C(base==0x30000 && bytes<=sizeof(image));return image; }
static void put(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;C(voice<64 && token && c->length==16 && records[0].voice_refs);++starts;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) { (void)ctx;(void)c;C(voice<64); }
static void stop(void *ctx,unsigned voice,int release) { (void)ctx;C(voice<64 && (release==0 || release==1));++stops; }
static int claim(void *ctx,uint32_t a,uint32_t b) { (void)ctx;(void)a;(void)b;return 1; }
static int pin(void *ctx,uint32_t s,uint32_t b,uint32_t g) { (void)ctx;return aica_bank_registry_pin(&registry,s,b,g,2); }
static int unpin(void *ctx,uint32_t s,uint32_t b,uint32_t g) { (void)ctx;return aica_bank_registry_unpin(&registry,s,b,g,2); }
static void packet(uint32_t *p,unsigned command,uint32_t id,unsigned op,unsigned port,uint32_t g,
                   uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    memset(p,0,64);p[0]=16;p[1]=command;p[3]=id;p[8]=1;p[9]=op;p[10]=port;p[11]=g;
    p[12]=a;p[13]=b;p[14]=c;p[15]=d;
}
static int mcommand(unsigned op,unsigned p,uint32_t g,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    uint32_t words[16];packet(words,AICA_MUSIC_WIRE_COMMAND,++mcookie,op,p,g,a,b,c,d);
    C(aica_music_wire_command(&mw,(const aica_cmd_t *)(const void *)words,1));return mr.result;
}
static void step(unsigned delta) {
    tick+=delta;aica_music_service(&music,delta/4);aica_music_wire_service(&mw);asql_service(&live,tick);
}
static void send(unsigned op,unsigned p,uint32_t g,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    uint32_t words[16];packet(words,ASQL_COMMAND,++cookie,op,p,g,a,b,c,d);
    C(asql_command(&live,(const aica_cmd_t *)(const void *)words,1));C(receipt.result==ASQL_PENDING);
}
static int command(unsigned op,unsigned p,uint32_t g,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    send(op,p,g,a,b,c,d);unsigned passes=0;
    do { step(0);C(++passes<64); } while(live.pending);
    C(receipt.cookie==cookie && !(receipt.sequence&1u));return receipt.result;
}
static void init_zones(unsigned zone_count) {
    /* A fresh simulated device per case; real registration is tested separately. */
    starts=stops=cookie=mcookie=tick=0;memset(image,0,sizeof(image));
    registry=(aica_bank_registry_t){.header=&header,.records=records,.map=map};
    aica_bank_registry_init(&registry);header.session=1;
    records[0]=(aica_bank_record_t){.session=1,.generation=1,.state=AICA_BANK_ACTIVE,
        .image_base=0x30000,.image_bytes=128+zone_count*32,.sample_count=1,.zone_count=zone_count};
    put(image+64,96+zone_count*32);put(image+68,32);put(image+72,16);put(image+76,22050);image[88]=2;
    for(unsigned i=0;i<zone_count;i++) {
        uint8_t *z=image+96+i*32;z[4]=(uint8_t)(i+1==zone_count?0:1);
        z[8]=127;z[9]=1;z[10]=127;z[11]=60;z[14]=255;z[15]=128;z[16]=31;z[19]=31;z[22]=15;
    }
    aica_music_bank_init(&bank,&registry,1);
    aica_music_ops_t ops={.resolve=aica_music_bank_resolve,.cancel_resolve=aica_music_bank_cancel,
        .retain=aica_music_bank_retain,.start=start,.update=update,.stop=stop,.context=&bank};
    aica_music_wire_ops_t transport={claim,pin,unpin,NULL};
    C(aica_music_init(&music,64,&ops)==0);C(aica_music_wire_init(&mw,&music,&mr,&transport));
    C(mcommand(AICA_MUSIC_WIRE_CLAIM,0,0,UINT32_MAX,UINT32_MAX,0,0)==0);
    for(unsigned p=0;p<8;p++) C(mcommand(AICA_MUSIC_WIRE_OPEN,p,0,0,1,0,0)==0);
    C(asq_catalog_init(&catalog)==0);
    for(unsigned slot=0;slot<2;slot++) {
        memcpy(sources[slot],score,sizeof(score));
        C(asq_catalog_begin(&catalog,slot,1,sources[slot],sizeof(score),events[slot],256)==0);
        int result;do { result=asq_catalog_step(&catalog,slot,1); } while(!result);C(result==1);
    }
    memset(&adapter,0,sizeof(adapter));C(asql_init(&live,&adapter,&mw,&registry,&catalog,&receipt));
}
static void init(void) { init_zones(1); }
static void binding(void) {
    for(unsigned state=1;state<=3;state++) {
        init();music.voices[0].state=(uint8_t)state;music.voices[0].port=0;
        C(command(ASQL_BIND,0,1,0,0,0,0)==ASQL_BUSY);C(!live.initialized && !mw.protected_ports && records[0].producer_refs==8);
    }
    init();C(mcommand(AICA_MUSIC_WIRE_MESSAGE,0,1,0x90,60,100,10)==0);
    send(ASQL_BIND,0,1,0,0,0,0);asql_service(&live,tick);C(receipt.result==ASQL_BUSY && !live.initialized);
    init_zones(64);C(mcommand(AICA_MUSIC_WIRE_MESSAGE,0,1,0x90,60,100,10)==0);
    C(aica_music_service(&music,0)==0 && bank.pinned);uint32_t pins=records[0].producer_refs;
    send(ASQL_BIND,0,1,0,0,0,0);asql_service(&live,tick);
    C(receipt.result==ASQL_BUSY && !live.initialized && bank.pinned && records[0].producer_refs==pins);
    init();mw.pending=1;send(ASQL_BIND,0,1,0,0,0,0);asql_service(&live,tick);
    C(receipt.result==ASQL_BUSY && !mw.protected_ports && mw.pending);
    init();C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(records[0].producer_refs==9 && mw.protected_ports==1);
    unsigned writes=starts+stops;
    for(unsigned op=2;op<=13;op++) {
        if(op==AICA_MUSIC_WIRE_QUERY) { C(mcommand(op,0,1,0,0,0,0)==0);continue; }
        C(mcommand(op,0,op>=11?0:1,0,0,0,0)==AICA_MUSIC_BUSY);
        C(records[0].producer_refs==9 && starts+stops==writes && !mw.pending);
    }
    mw.pending=1;aica_music_receipt_t saved=mr;uint32_t words[16];
    packet(words,AICA_MUSIC_WIRE_COMMAND,++mcookie,AICA_MUSIC_WIRE_QUIESCE,0,0,0,0,0,0);
    C(!aica_music_wire_command(&mw,(const aica_cmd_t *)(const void *)words,1));
    C(!memcmp(&saved,&mr,sizeof(mr)) && mw.pending && !music.quiescing);
    mw.pending=0;C(command(ASQL_UNBIND,0,1,0,0,0,0)==0 && !mw.protected_ports && records[0].producer_refs==8);
}
static void playback(void) {
    init();C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,0,20)==0);
    C(catalog.slots[0].playing==1 && !catalog.slots[0].borrowed && !starts);
    step(1);C(adapter.sequences[0].cursor==20 && music.count==20);
    C(asq_catalog_drop(&catalog,0,1)==ASQ_CATALOG_BUSY);
    for(unsigned i=0;i<5;i++) step(0);C(starts==20);
    uint32_t fraction=live.clocks[0].remainder;uint64_t remaining=adapter.sequences[0].remaining;
    C(command(ASQL_PLAY,0,1,0,1,2,1)==ASQL_DECLINED);
    C(live.clocks[0].remainder==fraction && adapter.sequences[0].remaining==remaining && catalog.slots[0].playing==1);
    C(command(ASQL_PLAY,0,1,0,2,2,31)==ASQL_INVALID && live.clocks[0].remainder==fraction);
    C(command(ASQL_SPEED,0,1,4096,1000,0,0)==0);
    C(command(ASQL_PAUSE,0,1,0,0,0,0)==0);uint64_t position=aica_sequence_ticks(&adapter.sequences[0]);
    uint64_t debt=adapter.sequences[0].pending;remaining=adapter.sequences[0].remaining;
    step(441);C(aica_sequence_ticks(&adapter.sequences[0])==position && adapter.sequences[0].pending==debt);
    C(adapter.sequences[0].remaining==remaining && live.clocks[0].remainder==fraction);
    C(adapter.sequences[0].speed_elapsed==100000); /* wall-time fade, frozen position */
    send(ASQL_CONTINUE,0,1,0,0,0,0);unsigned passes=0;
    do { step(1);C(++passes<32); } while(live.pending);
    C(passes>1 && receipt.result==0 && adapter.sequences[0].state==AICA_SEQ_RUNNING);
    C(adapter.sequences[0].remaining==remaining && adapter.sequences[0].pending==debt && live.clocks[0].remainder==fraction);
    C(command(ASQL_QUERY,0,1,0,0,0,0)==0 && receipt.notes==20 && receipt.resource_generation==1);
    C(command(ASQL_STOP,0,1,0,0,0,0)==0 && !catalog.slots[0].playing);
    C(asq_catalog_drop(&catalog,0,1)==0 && records[0].voice_refs);
    C(command(ASQL_UNBIND,0,1,0,0,0,0)==0 && !mw.protected_ports);
}
static void cancel_resume(void) {
    init();C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,0,10)==0);step(0);
    for(unsigned i=0;i<5;i++) step(0);
    C(command(ASQL_PAUSE,0,1,0,0,0,0)==0);send(ASQL_CONTINUE,0,1,0,0,0,0);step(0);C(live.pending);
    uint32_t old[16],bad[16];packet(old,ASQL_COMMAND,cookie,ASQL_CONTINUE,0,1,0,0,0,0);
    packet(bad,ASQL_COMMAND,cookie+1,ASQL_STOP,0,2,0,0,0,0);
    asql_receipt_t before=receipt;C(!asql_command(&live,(const aica_cmd_t *)(const void *)bad,1));C(!memcmp(&before,&receipt,sizeof(before)));
    C(command(ASQL_STOP,0,1,0,0,0,0)==0 && !catalog.slots[0].playing);
    C(receipt.cancelled_cookie==old[3] && receipt.cancelled_result==ASQL_CANCELLED && !memcmp(receipt.cancelled_request,old+8,32));
    before=receipt;C(asql_command(&live,(const aica_cmd_t *)(const void *)old,1));C(!memcmp(&before,&receipt,sizeof(before)));
    old[12]=1;C(!asql_command(&live,(const aica_cmd_t *)(const void *)old,1));
    C(command(ASQL_PLAY,0,1,0,1,2,10)==0);old[12]=0;C(!asql_command(&live,(const aica_cmd_t *)(const void *)old,1));
    C(command(ASQL_STOP_ALL,0,0,0,0,0,0)==0);
}
static void chain_and_clock(void) {
    init();tick=UINT32_MAX-100;C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,1,10)==0);
    uint64_t elapsed=0;uint32_t seed=47;
    for(unsigned i=0;i<600;i++) {
        seed=seed*1664525u+1013904223u;uint32_t delta=seed%17;elapsed+=delta;step(delta);
        C(!live.faulted && live.clocks[0].remainder==(elapsed*100000u)%441u);
    }
    init();tick=UINT32_MAX-5;C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,1,10)==0);
    step(10);C(!live.faulted && live.clocks[0].tick==4);
    C(command(ASQL_CHAIN,0,1,1,1,2,0)==0 && catalog.slots[1].playing==1);
    step(4410);C(live.current[0].slot==1 && catalog.slots[0].playing==0);
    C(command(ASQL_QUERY,0,1,0,0,0,0)==0 && receipt.slot==1);
    step(2205);C(!catalog.slots[1].playing);
    C(command(ASQL_PLAY,0,1,0,1,0,10)==0);step(20000000);
    C(live.faulted && (receipt.flags&2u) && !catalog.slots[0].playing && mw.protected_ports==1);
    init();C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,0,10)==0);
    records[0].producer_refs=0; /* Inject failed bank unpin, retain guard/binding. */
    C(command(ASQL_UNBIND,0,1,0,0,0,0)==ASQL_FAULT);
    C(live.faulted && mw.protected_ports==1 && adapter.bindings[0].bound && !catalog.slots[0].playing);
}
static void bounded_and_global(void) {
    init();C(command(ASQL_BIND,0,1,0,0,0,0)==0);C(command(ASQL_PLAY,0,1,0,1,3,10)==0);
    asql_service(&live,441);C(adapter.sequences[0].cursor==32 && music.count==32);
    asql_service(&live,441);C(adapter.sequences[0].cursor==64 && music.count==64);
    uint64_t debt=adapter.sequences[0].pending;asql_service(&live,441);
    C(adapter.sequences[0].cursor==64 && adapter.sequences[0].pending==debt);
    tick=441;send(ASQL_PAUSE,0,1,0,0,0,0);asql_service(&live,tick);
    C(receipt.result==0 && adapter.sequences[0].state==AICA_SEQ_PAUSED && music.count==64);
    C(adapter.sequences[0].pending==debt && catalog.slots[0].playing==1);
    for(unsigned cycle=0;cycle<3;cycle++) {
        uint32_t remainder=live.clocks[0].remainder;step(7);
        C(adapter.sequences[0].pending==debt && live.clocks[0].remainder==remainder);
        C(command(ASQL_CONTINUE,0,1,0,0,0,0)==0);
        C(live.clocks[0].remainder==remainder);
        C(command(ASQL_PAUSE,0,1,0,0,0,0)==0);debt=adapter.sequences[0].pending;
    }
    init();
    for(unsigned p=0;p<8;p++) {
        C(command(ASQL_BIND,p,1,0,0,0,0)==0);C(command(ASQL_PLAY,p,1,p%2,1,0,10)==0);
    }
    C(catalog.slots[0].playing==4 && catalog.slots[1].playing==4 && mw.protected_ports==255);
    C(command(ASQL_STOP_ALL,0,0,0,0,0,0)==0);
    C(!catalog.slots[0].playing && !catalog.slots[1].playing && mw.protected_ports==255);
    for(unsigned p=0;p<8;p++) C(command(ASQL_UNBIND,p,1,0,0,0,0)==0);
    C(records[0].producer_refs==8 && !mw.protected_ports);
    init();for(unsigned p=0;p<2;p++) { C(command(ASQL_BIND,p,1,0,0,0,0)==0);C(command(ASQL_PLAY,p,1,0,1,0,10)==0); }
    music.ports[1].generation=2;
    C(command(ASQL_STOP_ALL,0,0,0,0,0,0)==ASQL_FAULT && live.faulted);
    C(!catalog.slots[0].playing && mw.protected_ports==3 && (receipt.flags&2u));
}
static void identity(void) {
    init();uint32_t words[16];packet(words,ASQL_COMMAND,1,ASQL_BIND,0,1,0,0,0,0);
    C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,2));C(!live.cookie);
    words[12]=1;C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,1));words[12]=0;
    C(asql_command(&live,(const aica_cmd_t *)(const void *)words,1));asql_receipt_t saved=receipt;
    C(asql_command(&live,(const aica_cmd_t *)(const void *)words,1) && !memcmp(&saved,&receipt,sizeof(saved)));
    words[3]=2;words[9]=ASQL_QUERY;C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,1));
    C(!memcmp(&saved,&receipt,sizeof(saved)));asql_service(&live,0);C(receipt.result==0);
    saved=receipt;words[3]=1;words[9]=ASQL_BIND;C(asql_command(&live,(const aica_cmd_t *)(const void *)words,1));
    C(!memcmp(&saved,&receipt,sizeof(saved)));words[10]=1;C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,1));
    words[3]=2;words[10]=0;words[9]=ASQL_QUERY;words[8]=2;
    C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,2));C(mw.protected_ports==1);
    words[8]=1;receipt.sequence=UINT32_MAX-3;
    C(!asql_command(&live,(const aica_cmd_t *)(const void *)words,1));C(live.faulted && receipt.sequence==UINT32_MAX);
}
static asql_client_t client;
static unsigned transport,corrupt,torn,ioerror;
static uint32_t active;
static int client_session(void *ctx,uint32_t *value) { (void)ctx;*value=active;return 0; }
static int client_read(void *ctx,uint32_t *words,unsigned count) {
    (void)ctx;if(ioerror) return -1;memcpy(words,&receipt,count*4u);
    if(count==64 && corrupt) words[63]=1;
    if(count==64 && torn) words[0]+=2;
    return 0;
}
static int client_submit(void *ctx,const uint32_t words[16]) {
    (void)ctx;if(transport==1) return 0;
    int rc=asql_command(&live,(const aica_cmd_t *)(const void *)words,active);
    return transport==2?-1:rc;
}
static void client_init(void) {
    init();active=1;transport=corrupt=torn=ioerror=0;
    asq_client_ops_t ops={client_read,client_session,client_submit,NULL};
    C(asql_client_init(&client,1,&ops)==0);C(asql_client_poll(&client)==1 && client.state==ASQC_IDLE);
}
static int client_command(unsigned op,const uint32_t a[4]) {
    C(asql_client_begin(&client,op,0,1,a)>=0);unsigned n=0;
    while(asql_client_poll(&client)==0) { step(0);C(++n<64); }
    C(client.state==ASQC_DONE);int rc=client.receipt.result;C(asql_client_consume(&client)==0);return rc;
}
static void clients(void) {
    const uint32_t zero[4]={0},play[4]={0,1,0,10};
    for(unsigned mode=1;mode<=2;mode++) {
        client_init();transport=mode;
        C(asql_client_begin(&client,ASQL_BIND,0,1,zero)==0 && client.state==ASQC_WAIT);
        uint32_t old[16];memcpy(old,client.packet,64);
        C(asql_client_begin(&client,ASQL_QUERY,0,1,zero)<0);
        transport=0;C(asql_client_retry(&client)==1 && !memcmp(old,client.packet,64));step(0);
        C(asql_client_poll(&client)==1 && client.receipt.bound && asql_client_consume(&client)==0);
        C(client_command(ASQL_PLAY,play)==0);step(0);for(unsigned i=0;i<5;i++) step(0);
        C(client_command(ASQL_PAUSE,zero)==0);
        C(asql_client_begin(&client,ASQL_CONTINUE,0,1,zero)==1);C(asql_client_cancel(&client,0)<0);
        C(asql_client_poll(&client)==0);memcpy(old,client.packet,64);
        C(asql_client_cancel(&client,0)==1);step(0);C(asql_client_poll(&client)==1);
        C(client.receipt.cancelled_cookie==old[3] && !catalog.slots[0].playing);
    }
    client_init();C(asql_client_begin(&client,ASQL_BIND,0,1,zero)==1);step(0);
    torn=1;C(asql_client_poll(&client)==0 && client.state==ASQC_WAIT);torn=0;
    ioerror=1;C(asql_client_poll(&client)==0);ioerror=0;
    corrupt=1;C(asql_client_poll(&client)<0 && client.state==ASQC_LOST && mw.protected_ports==1);
    client_init();C(asql_client_begin(&client,ASQL_BIND,0,1,zero)==1);step(0);active=2;
    C(asql_client_poll(&client)<0 && client.state==ASQC_LOST && mw.protected_ports==1);
    client_init();C(asql_client_begin(&client,ASQL_BIND,0,1,zero)==1);step(0);receipt.generation=9;
    C(asql_client_poll(&client)<0 && client.state==ASQC_LOST);
    client_init();client.cookie=UINT32_MAX;C(asql_client_begin(&client,ASQL_BIND,0,1,zero)<0 && client.state==ASQC_LOST);
}
static void fairness(void) {
    init();uint32_t before[8];
    for(unsigned p=0;p<8;p++) C(command(ASQL_BIND,p,1,0,0,0,0)==0);
    for(unsigned p=0;p<8;p++) C(command(ASQL_PLAY,p,1,0,1,4,10)==0);
    for(unsigned i=0;i<16;i++) step(11);
    for(unsigned p=0;p<8;p++) before[p]=adapter.sequences[p].processed;
    for(unsigned i=0;i<256;i++) step(11);
    C(music.count==64); /* Continuously overloaded; all ports still progress. */
    for(unsigned p=0;p<8;p++) C(adapter.sequences[p].processed-before[p]>=64);
    C(command(ASQL_STOP_ALL,0,0,0,0,0,0)==0 && !catalog.slots[0].playing);
}
int main(void) {
    binding();playback();cancel_resume();chain_and_clock();bounded_and_global();identity();clients();fairness();
    printf("ASQ live controls: %u checks PASS\n",checks);return 0;
}
