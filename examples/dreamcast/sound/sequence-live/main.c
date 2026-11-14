#include <kos.h>
#include <dc/sound/sound.h>
#include <errno.h>
#include <stdio.h>
#include "live-owner.h"
#include "bank.h"
#include "score.h"
static abk_bank_t tone;
static abk_sample_t samples[ABK_MAX_SAMPLES];
static abk_zone_t zones[ABK_MAX_ZONES];
static aica_music_owner_t music;
static asq_owner_t resources;
static asql_owner_t sequences;
#define REQUIRE(x) do { if(!(x)) { printf("SEQUENCE LIVE FAIL line=%d errno=%d client=%lu result=%ld state=%lu\n",__LINE__,errno,(unsigned long)sequences.client.state,(long)sequences.client.receipt.result,(unsigned long)sequences.client.receipt.state);goto hold; } } while(0)
static int await_music(void) {
    uint64_t end=timer_ms_gettime64()+5000;
    while(timer_ms_gettime64()<end) {
        int rc=aica_music_owner_poll(&music);if(rc<0) return -99;
        if(rc==1) { int result=music.client.receipt.result;return aica_music_owner_consume(&music)<0?-99:result; }
        thd_pass();
    }
    return -99;
}
static int await_resource(unsigned slot,unsigned state) {
    uint64_t end=timer_ms_gettime64()+5000;
    while(resources.slots[slot].state!=state) {
        if(asq_owner_pump(&resources)<0 || timer_ms_gettime64()>=end) return -1;
        thd_pass();
    }
    return 0;
}
static int command(unsigned op,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    const uint32_t args[4]={a,b,c,d};uint64_t end=timer_ms_gettime64()+5000;
    if(asql_owner_command(&sequences,op,0,op==ASQL_STOP_ALL?0:1,args)<0) return -99;
    while(timer_ms_gettime64()<end) {
        int rc=asql_owner_poll(&sequences);if(rc<0) return -99;
        if(rc==1) { int result=sequences.client.receipt.result;return asql_owner_consume(&sequences)<0?-99:result; }
        thd_pass();
    }
    return -99;
}
int main(void) {
    uint64_t deadline;uint32_t available,args[4]={0};
    REQUIRE(snd_init()==0);
    REQUIRE(abk_bank_stage(&tone,0,bank,sizeof(bank),samples,ABK_MAX_SAMPLES,zones,ABK_MAX_ZONES,4096,1000)==0);
    deadline=timer_ms_gettime64()+10000;
    while(tone.state==ABKB_STAGING) {
        int rc=abk_bank_poll(&tone,1000);REQUIRE(rc==0 || errno==EAGAIN || errno==EBUSY);
        REQUIRE(timer_ms_gettime64()<deadline);thd_pass();
    }
    REQUIRE(tone.state==ABKB_STAGED && abk_bank_publish(&tone,1000)==0);
    while(tone.state==ABKB_PUBLISHING) {
        int rc=abk_bank_poll(&tone,1000);REQUIRE(rc==0 || errno==EAGAIN);
        REQUIRE(timer_ms_gettime64()<deadline);thd_pass();
    }
    REQUIRE(tone.state==ABKB_ACTIVE && aica_music_owner_init(&music,64,1000)==0 && await_music()==0);
    REQUIRE(aica_music_owner_open(&music,0,&tone)>=0 && await_music()==0);
    REQUIRE(music.client.receipt.generation==1);
    REQUIRE(asq_owner_init(&resources,4096,1000)==0);available=snd_mem_available();
    for(unsigned slot=0;slot<2;slot++) {
        REQUIRE(asq_owner_begin(&resources,slot,score,sizeof(score))==0 && await_resource(slot,ASQH_READY)==0);
    }
    REQUIRE(asql_owner_init(&sequences,&music,&resources,1000)==0);
    REQUIRE(command(ASQL_BIND,0,0,0,0)==0 && sequences.client.receipt.bound);
    /* A direct producer cannot mutate a port while its sequence binding exists. */
    args[0]=0x90;args[1]=60;args[2]=100;args[3]=10;
    REQUIRE(aica_music_owner_command(&music,AICA_MUSIC_WIRE_MESSAGE,0,1,args)>=0 && await_music()==AICA_MUSIC_BUSY);
    REQUIRE(command(ASQL_PLAY,0,1,0,20)==0);thd_sleep(100);
    REQUIRE(command(ASQL_QUERY,0,0,0,0)==0 && sequences.client.receipt.notes==20);
    memset(args,0,sizeof(args));
    REQUIRE(aica_music_owner_command(&music,AICA_MUSIC_WIRE_QUERY,0,1,args)>=0 && await_music()==0);
    REQUIRE(music.client.receipt.active>0);
    REQUIRE(command(ASQL_PAUSE,0,0,0,0)==0);
    uint32_t ticks[2]={sequences.client.receipt.ticks[0],sequences.client.receipt.ticks[1]};
    thd_sleep(100);REQUIRE(command(ASQL_QUERY,0,0,0,0)==0);
    REQUIRE(sequences.client.receipt.state==AICA_SEQ_PAUSED && sequences.client.receipt.notes==20);
    REQUIRE(sequences.client.receipt.ticks[0]==ticks[0] && sequences.client.receipt.ticks[1]==ticks[1]);
    REQUIRE(asq_owner_query(&resources,0)==0 && await_resource(0,ASQH_READY)==0 && resources.client.receipt.playing==1);
    REQUIRE(command(ASQL_CONTINUE,0,0,0,0)==0 && sequences.client.receipt.state==AICA_SEQ_RUNNING);
    REQUIRE(command(ASQL_STOP,0,0,0,0)==0);
    REQUIRE(command(ASQL_PLAY,0,1,1,20)==0);thd_sleep(100);
    REQUIRE(command(ASQL_CHAIN,1,1,2,0)==0);
    deadline=timer_ms_gettime64()+10000;
    for(;;) {
        REQUIRE(command(ASQL_QUERY,0,0,0,0)==0);
        if(sequences.client.receipt.state==AICA_SEQ_IDLE) break;
        REQUIRE(timer_ms_gettime64()<deadline);thd_sleep(10);
    }
    REQUIRE(sequences.client.receipt.slot==1 && sequences.client.receipt.program==2);
    REQUIRE(command(ASQL_STOP_ALL,0,0,0,0)==0 && command(ASQL_UNBIND,0,0,0,0)==0);
    for(unsigned slot=0;slot<2;slot++) {
        REQUIRE(asq_owner_query(&resources,slot)==0 && await_resource(slot,ASQH_READY)==0);
        REQUIRE(resources.client.receipt.playing==0 && resources.client.receipt.borrowed==0);
        REQUIRE(asq_owner_close(&resources,slot)==0 && await_resource(slot,ASQH_CLOSED)==0);
    }
    REQUIRE(snd_mem_available()==available);
    REQUIRE(aica_music_owner_quiesce(&music)>=0 && await_music()==0);
    REQUIRE(music.retained && music.count==64 && tone.address && tone.state!=ABKB_CLOSED);
    printf("SEQUENCE LIVE PASS: play/pause/continue/loop/chain/stop; CPU program allocations restored; played tone bank and64channels retained; no physical retirement or audio-quality claim\n");
hold:
    for(;;) thd_sleep(1000);
    return 0;
}
