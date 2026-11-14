/* Original fixtures exercise the actual core, resolver and registry together. */
#include "aica_music_bank.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define C(x) do { ++checks;if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static unsigned checks,scanned,yields,visits,updates,attempts,started,stopped,retains;
static unsigned fail_retain,fail_start,bad_tone,bad_more,bad_zero;
static int retain_failure;
static unsigned char image[64+4*32+96*32+128];
static aica_bank_header_t header;
static aica_bank_record_t records[128];
static aica_bank_registry_t registry;
static aica_music_bank_t bank;
static aica_music_t music;
static uint32_t generation,bases[64],tokens[64];
static const uint8_t *map(uint32_t base,uint32_t bytes) {
    C(base==0x30000 && bytes==sizeof(image));return image;
}
static void put(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static int resolve(void *ctx,const aica_music_event_t *e,unsigned b,unsigned p,unsigned d,
                   aica_music_tone_t *tone,unsigned *budget) {
    unsigned before=*budget;int rc=aica_music_bank_resolve(ctx,e,b,p,d,tone,budget);
    C(*budget<=before && before<=32);scanned+=before-*budget;
    if(rc==AICA_MUSIC_MORE || rc==AICA_MUSIC_FINAL) {
        ++yields;
        if(bad_tone && retains+1==bad_tone) tone->root_key=128;
        if(bad_more && rc==AICA_MUSIC_FINAL) rc=AICA_MUSIC_MORE;
        if(bad_zero && retains+1==bad_zero) rc=0;
    }
    return rc;
}
static int retain(void *ctx,unsigned voice,const aica_music_tone_t *tone) {
    if(++retains==fail_retain) return retain_failure;
    return aica_music_bank_retain(ctx,voice,tone);
}
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;C(voice<64 && token && records[0].voice_refs && records[0].producer_refs==1);
    bases[attempts]=c->base;tokens[attempts++]=token;
    C(music.token==token); /* Identity reserved even when the device callback fails. */
    if(attempts==fail_start) return -1;
    ++started;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) {
    (void)ctx;(void)c;C(voice<64 && records[0].voice_refs);++updates;
}
static void stop(void *ctx,unsigned voice,int release) {
    (void)ctx;(void)release;C(voice<64 && records[0].voice_refs);++stopped;
}
static int ended(void *ctx,unsigned voice,uint32_t token) {
    (void)ctx;C(voice<64 && token);++visits;return 0;
}
static unsigned service(void) {
    unsigned v=visits;scanned=yields=0;
    unsigned done=aica_music_service(&music,1);
    C(scanned<=32 && yields<=1 && visits-v<=16 && done<=4);return done;
}
static void init_format(unsigned pool,unsigned version,unsigned mask) {
    unsigned positions[4]={80,70,90,35};
    scanned=yields=visits=updates=attempts=started=stopped=retains=0;
    fail_retain=fail_start=bad_tone=bad_more=bad_zero=0;retain_failure=AICA_MUSIC_BUSY;
    memset(image,0,sizeof(image));memcpy(image,"KOSABK",6);image[8]=(uint8_t)version;image[10]=64;
    put(image+12,sizeof(image));put(image+16,4);put(image+20,96);put(image+24,64);
    put(image+28,192);put(image+32,sizeof(image)-128);put(image+36,128);
    for(unsigned i=0;i<4;i++) {
        uint8_t *s=image+64+i*32;put(s,sizeof(image)-128+i*32);put(s+4,32);
        put(s+8,16);put(s+12,22050);s[24]=2;
    }
    for(unsigned i=0;i<96;i++) {
        uint8_t *z=image+192+i*32;z[4]=1;z[5]=(uint8_t)i;
        z[8]=127;z[9]=1;z[10]=127;z[11]=60;z[14]=255;z[15]=128;
        z[16]=z[19]=31;z[22]=15;
    }
    for(unsigned layer=0;layer<4;layer++) {
        if(!(mask&(1u<<layer))) continue;
        uint8_t *z=image+192+positions[layer]*32;put(z,layer);z[4]=z[5]=0;z[25]=version==2?(uint8_t)layer:0;
        z[7]=z[8]=60;z[9]=64;z[10]=100;
    }
    registry=(aica_bank_registry_t){.header=&header,.records=records,.map=map};
    aica_bank_registry_init(&registry);
    uint32_t packet[14]={14,AICA_CMD_BANK_REGISTER,0,1,0,0,0,0,1,0,1,0x30000,sizeof(image),1};
    C(aica_bank_registry_command(&registry,(aica_cmd_t *)(void *)packet,1));
    while(records[0].state==AICA_BANK_VALIDATING) C(aica_bank_registry_service(&registry)<=32);
    C(records[0].state==AICA_BANK_ACTIVE);
    aica_music_bank_init(&bank,&registry,1);
    aica_music_ops_t ops={.resolve=resolve,.cancel_resolve=aica_music_bank_cancel,
        .retain=retain,.start=start,.update=update,.stop=stop,.ended=ended,.context=&bank};
    C(aica_music_init(&music,pool,&ops)==0);C(aica_music_open(&music,0,0,1,&generation)==0);
}
static void init(unsigned pool) { init_format(pool,2,15); }
static void note(unsigned channel,unsigned priority) {
    C(aica_music_message(&music,0,generation,0x90+channel,60,100,priority)==0);
}
static void first(void) {
    unsigned before=started;
    C(service()==0 && scanned==32 && started==before);
    C(service()==0 && scanned==32 && started==before);
    C(service()==0 && scanned==32 && !yields && !music.layer_count);
    C(service()==0 && scanned==1 && yields==1 && music.layer_count==1);
}
static void drain(void) { unsigned n=0;while(music.count && !music.faulted) { service();C(++n<32); } }
static void four(void) {
    init(64);note(0,8);note(1,8);first();C(started==1 && records[0].producer_refs==1);
    music.voice_cursor=0;unsigned v=visits;C(aica_music_parameter(&music,0,generation,AICA_MUSIC_PAN,10,0)==0);
    C(service()==0 && started==2 && visits>v && updates);
    C(service()==0 && started==3);C(service()==1 && started==4 && music.count==1);
    C(scanned==1 && !records[0].producer_refs && music.applied==1 && !music.layer_count);
    for(unsigned i=0;i<4;i++) C(bases[i]==0x30000+sizeof(image)-128+i*32 && tokens[i]==i+1);
    music.voice_cursor=0;v=visits;C(service()==0 && scanned==32 && visits>v);drain();C(started==8 && records[0].voice_refs==8);
    C(aica_music_message(&music,0,generation,0xb0,64,127,0)==0);
    C(aica_music_message(&music,0,generation,0x80,60,0,0)==0);service();
    for(unsigned i=0;i<4;i++) C(music.voices[i].state==AICA_MUSIC_SUSTAINED);
    C(aica_music_message(&music,0,generation,0xb0,64,0,0)==0);service();C(stopped==4);
    aica_music_quiesce(&music);C(records[0].voice_refs==8 && !records[0].producer_refs);
}
static void capacity(void) {
    for(unsigned pool=1;pool<=4;pool++) {
        init(pool);note(0,8);drain();C(started==pool && !music.stolen && music.declined==4-pool);
        C(records[0].voice_refs==pool && !records[0].producer_refs && music.applied==1);
    }
    init(1);note(9,31);drain();unsigned before=started;
    note(9,8);first();C(started==before && music.layer_count==1);
    C(aica_music_gm(&music,0,generation,1)==0);drain();C(started==before && !music.faulted);
    C(music.applied==2 && music.declined==7 && !records[0].producer_refs);
}
static void boundaries(void) {
    for(unsigned mask=0;mask<16;mask++) {
        init_format(64,2,mask);note(0,8);drain();unsigned n=0;
        for(unsigned i=0;i<4;i++) if(mask&(1u<<i)) {
            C(bases[n]==0x30000+sizeof(image)-128+i*32);++n;
        }
        C(started==n && !music.faulted && !bank.pinned && records[0].voice_refs==n);
        C(music.applied==(mask?1u:0u) && music.rejected==(mask?0u:1u));
    }
    for(unsigned key=59;key<=61;key++) for(unsigned velocity=63;velocity<=101;velocity++) {
        if(velocity>64 && velocity<100) continue;
        init(64);C(aica_music_message(&music,0,generation,0x90,key,velocity,8)==0);drain();
        C(started==(key==60 && velocity>=64 && velocity<=100?4u:0u));
    }
    /* Actual v1 lookups share one budget across two consecutive FIFO notes. */
    init_format(64,1,8);note(0,8);note(1,8);
    C(service()==0 && scanned==32 && !started);
    C(service()==1 && scanned==32 && started==1 && music.count==1);
    C(service()==1 && scanned==8 && started==2 && !music.count && !bank.pinned);
}
static void faults(void) {
    for(unsigned i=1;i<=4;i++) for(unsigned kind=0;kind<4;kind++) {
        init(64);if(kind==0) fail_retain=i;else if(kind==1) fail_start=i;else if(kind==2) bad_tone=i;
        else { fail_retain=i;retain_failure=AICA_MUSIC_RETRY; }
        note(0,8);drain();C(music.faulted && !music.count && music.rejected==1);
        C(started==i-1 && !music.layer_count && !bank.pinned && !records[0].producer_refs);
        unsigned owned=kind==1?i:i-1;C(records[0].voice_refs==owned && music.token==owned);
        unsigned a=attempts;service();C(attempts==a);aica_music_quiesce(&music);C(records[0].voice_refs==owned);
    }
    init(64);music.token=UINT32_MAX-1;note(0,8);first();C(started==1 && music.token==UINT32_MAX);
    C(service()==1 && music.faulted && !music.count && started==1 && music.token==UINT32_MAX);
    C(music.last_result==AICA_MUSIC_EXHAUSTED && records[0].voice_refs==1 && !bank.pinned);
    init(64);bad_more=1;note(0,8);drain();C(music.faulted && started==3 && records[0].voice_refs==3);
    init(64);bad_zero=2;note(0,8);drain();C(music.faulted && started==1 && !music.count && !bank.pinned);
}
static void cancellation(void) {
    for(unsigned kind=0;kind<4;kind++) {
        init(64);note(9,8);first();C(started==1);
        if(kind==0) { C(aica_music_mute(&music,0,generation,1u<<9)==0);C(aica_music_mute(&music,0,generation,0)==0); }
        if(kind==1) C(aica_music_silence(&music,0,generation,0)==0);
        if(kind==2) { C(aica_music_close(&music,0,generation)==0);
            uint32_t old=generation;C(aica_music_open(&music,0,0,1,&generation)==0 && generation!=old); }
        if(kind==3) aica_music_quiesce(&music);
        C(!music.count && !music.layer_count && !music.layer_voices[0] && !music.layer_voices[1]);
        C(!bank.pinned && !records[0].producer_refs && records[0].voice_refs==1);
        service();C(started==1);
        if(kind==2) { note(0,8);drain();C(started==5 && !music.faulted); }
    }
    init(64);note(9,8);service();C(bank.cursor==32);
    C(aica_music_gm(&music,0,generation,1)==0);service();C(bank.cursor==32 && !started);
    drain();C(music.rejected==1 && !started && !bank.pinned);
    init(64);note(9,8);first();C(aica_music_gm(&music,0,generation,1)==0);drain();
    C(started==4 && music.applied==1 && !music.rejected);
}
int main(void) { four();capacity();boundaries();faults();cancellation();printf("Layered music: %u actual-C checks PASS; no physical retirement\n",checks);return 0; }
