#include "aica_music_bank.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks,maps;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static uint8_t image[64+32+1024*32+32];
static aica_bank_header_t header;
static aica_bank_record_t records[128];
static aica_bank_registry_t registry;
static aica_music_bank_t adapter;
static aica_music_tone_t tone;
static aica_music_event_t event={.generation=1,.bank_generation=1,.status=0x90,.a=60,.b=100};
static const uint8_t *map(uint32_t base,uint32_t bytes) {
    CHECK(base==0x30000 && bytes==sizeof(image));++maps;return image;
}
static void put(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void command(uint32_t op,uint32_t cookie) {
    aica_cmd_t *p=calloc(1,sizeof(*p)+6*sizeof(uint32_t));
    uint32_t *data;
    CHECK(p!=NULL);data=(uint32_t *)p->cmd_data;
    p->cmd=op;p->cmd_id=cookie;p->size=op==AICA_CMD_BANK_REGISTER?14:11;
    data[0]=1;data[2]=1;data[3]=0x30000;data[4]=sizeof(image);data[5]=1;
    CHECK(aica_bank_registry_command(&registry,p,1));free(p);
}
static void init(void) {
    uint8_t *s=image+64;unsigned i;
    memset(image,0,sizeof(image));memcpy(image,"KOSABK",6);
    image[8]=1;image[10]=64;put(image+12,sizeof(image));put(image+16,1);
    put(image+20,1024);put(image+24,64);put(image+28,96);
    put(image+32,sizeof(image)-32);put(image+36,32);
    put(s,sizeof(image)-32);put(s+4,32);put(s+8,16);put(s+12,22050);s[24]=2;
    for(i=0;i<1024;i++) {
        uint8_t *z=image+96+i*32;
        /* Distinct selector pairs, so the final match needs all 1024 zones. */
        z[4]=(uint8_t)(i/128);z[5]=(uint8_t)(i%128);z[8]=127;z[9]=1;z[10]=127;
        z[11]=60;z[14]=255;z[15]=128;z[16]=31;z[19]=31;z[22]=15;
    }
    registry=(aica_bank_registry_t){.header=&header,.records=records,.map=map};
    aica_bank_registry_init(&registry);command(AICA_CMD_BANK_REGISTER,1);
    while(records[0].state==AICA_BANK_VALIDATING)
        CHECK(aica_bank_registry_service(&registry)<=32);
    CHECK(records[0].state==AICA_BANK_ACTIVE);
    aica_music_bank_init(&adapter,&registry,1);maps=0;
}
static void lookup(unsigned bank,unsigned program) {
    int result;unsigned calls=0;
    do { result=aica_music_bank_resolve(&adapter,&event,bank,program,0,&tone);++calls;
        CHECK(calls<=32 && records[0].producer_refs==1);
    } while(result==AICA_MUSIC_RETRY);
    CHECK(result==0);
}
static unsigned starts,stops;
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;CHECK(voice<64 && token && c->loopend==16 && c->base>=0x30000);
    CHECK(records[0].voice_refs && records[0].producer_refs==1);++starts;return 0;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) {
    (void)ctx;(void)c;CHECK(voice<64 && records[0].voice_refs);
}
static void stop(void *ctx,unsigned voice,int release) {
    (void)ctx;(void)release;CHECK(voice<64 && records[0].voice_refs);++stops;
}
static void integration(void) {
    static aica_music_t music;
    aica_music_ops_t ops={.context=&adapter,.resolve=aica_music_bank_resolve,
        .cancel_resolve=aica_music_bank_cancel,.retain=aica_music_bank_retain,
        .start=start,.update=update,.stop=stop};
    uint32_t generations[8];unsigned p,ch;
    init();CHECK(aica_music_init(&music,64,&ops)==0);
    for(p=0;p<8;p++) CHECK(aica_music_open(&music,p,0,1,&generations[p])==0);
    for(p=0;p<8;p++) for(ch=0;ch<16;ch++) {
        CHECK(aica_music_message(&music,p,generations[p],0x90+ch,60,127,8)==0);
        CHECK(aica_music_service(&music,1)==1);
    }
    CHECK(starts==128 && music.stolen==64 && records[0].voice_refs==64);
    CHECK(!records[0].producer_refs && music.rejected==0);
    for(p=0;p<8;p++) for(ch=0;ch<16;ch++) {
        CHECK(aica_music_message(&music,p,generations[p],0xb0+ch,48,ch+p,0)==0);
        CHECK(aica_music_service(&music,0)==1 && music.ports[p].mailbox==ch+p);
    }
    aica_music_quiesce(&music);CHECK(stops==64 && records[0].voice_refs==64);
    for(p=0;p<8;p++) CHECK(!music.ports[p].open);
    command(AICA_CMD_BANK_UNREGISTER,2);CHECK(records[0].result==AICA_BANK_BUSY);
}
int main(void) {
    init();
    for(unsigned i=0;i<31;i++) {
        CHECK(aica_music_bank_resolve(&adapter,&event,7,127,0,&tone)==AICA_MUSIC_RETRY);
        CHECK(adapter.cursor==(i+1)*32 && records[0].producer_refs==1);
    }
    command(AICA_CMD_BANK_UNREGISTER,2);CHECK(records[0].result==AICA_BANK_BUSY);
    CHECK(aica_music_bank_resolve(&adapter,&event,7,127,0,&tone)==0 && maps==32);
    CHECK(tone.config.base==0x30000+sizeof(image)-32 && tone.config.type==AICA_SM_16BIT);
    CHECK(tone.config.length==16 && tone.config.loopend==16 && !tone.config.loop);
    CHECK(tone.config.attack_rate==31 && tone.config.release_rate==31 && tone.config.direct_level==15);
    CHECK(aica_music_bank_retain(&adapter,63,&tone)==0 && records[0].voice_refs==1);
    CHECK(aica_music_bank_retain(&adapter,63,&tone)==0 && records[0].voice_refs==1);
    aica_music_bank_cancel(&adapter);CHECK(!records[0].producer_refs && records[0].voice_refs==1);
    command(AICA_CMD_BANK_UNREGISTER,3);CHECK(records[0].result==AICA_BANK_BUSY);
    lookup(7,127);CHECK(aica_music_bank_retain(&adapter,0,&tone)==0 && records[0].voice_refs==2);
    aica_music_bank_cancel(&adapter);
    CHECK(aica_music_bank_retain(&adapter,0,&tone)==AICA_MUSIC_INVALID);
    CHECK(aica_music_bank_resolve(&adapter,&event,7,127,0,&tone)==AICA_MUSIC_RETRY);
    /* Changed GM/program/bank selector restarts under a fresh producer pin. */
    CHECK(aica_music_bank_resolve(&adapter,&event,0,0,0,&tone)==0 && adapter.cursor==1);
    CHECK(records[0].producer_refs==1);aica_music_bank_cancel(&adapter);
    for(unsigned i=0;i<32;i++) {
        int r=aica_music_bank_resolve(&adapter,&event,127,127,0,&tone);
        CHECK(r==(i<31?AICA_MUSIC_RETRY:AICA_MUSIC_MISSING));
    }
    aica_music_bank_cancel(&adapter);CHECK(!records[0].producer_refs);
    event.bank_generation=2;maps=0;
    CHECK(aica_music_bank_resolve(&adapter,&event,0,0,0,&tone)==AICA_MUSIC_MISSING && !maps);
    event.bank_generation=1;
    for(unsigned voice=0;voice<64;voice++) {
        lookup(0,0);CHECK(aica_music_bank_retain(&adapter,voice,&tone)==0);
        aica_music_bank_cancel(&adapter);
    }
    CHECK(records[0].voice_refs==64 && !records[0].producer_refs);
    lookup(0,0);records[0].sequence=UINT32_MAX-1;
    aica_music_bank_cancel(&adapter);
    CHECK(records[0].sequence==UINT32_MAX && records[0].producer_refs==1);
    CHECK(aica_music_bank_resolve(&adapter,&event,0,0,0,&tone)==AICA_MUSIC_MISSING);
    integration();
    printf("resident bank: %u checks PASS; adapter bytes=%zu\n",checks,sizeof(adapter));return 0;
}
