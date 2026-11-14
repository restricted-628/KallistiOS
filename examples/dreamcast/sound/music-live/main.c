#include <kos.h>
#include <dc/sound/sound.h>
#include <errno.h>
#include <stdio.h>
#include "music_live.h"
#include "bank.h"
static abk_bank_t bank_owner;
static abk_sample_t samples[ABK_MAX_SAMPLES];
static abk_zone_t zones[ABK_MAX_ZONES];
static aica_music_owner_t music_owner;
#define REQUIRE(x) do { if(!(x)) { printf("MUSIC LIVE FAIL line=%d errno=%d client=%u result=%ld bank=%d\n",__LINE__,errno,music_owner.client.state,(long)music_owner.client.receipt.result,bank_owner.state);goto hold; } } while(0)
static int await_command(void) {
    uint64_t end=timer_ms_gettime64()+5000;
    while(timer_ms_gettime64()<end) {
        int rc=aica_music_owner_poll(&music_owner);
        if(rc<0) return -1;
        if(rc==1) return music_owner.client.receipt.flags&4u?-1:music_owner.client.receipt.result;
        thd_pass();
    }
    return -1; /* Timeout retains every object and physical reservation. */
}
int main(void) {
    uint64_t deadline;uint32_t generation,args[4]={0};
    REQUIRE(snd_init()==0);
    REQUIRE(abk_bank_stage(&bank_owner,0,bank,sizeof(bank),samples,ABK_MAX_SAMPLES,
                           zones,ABK_MAX_ZONES,4096,1000)==0);
    deadline=timer_ms_gettime64()+10000;
    while(bank_owner.state==ABKB_STAGING) {
        int rc=abk_bank_poll(&bank_owner,1000);
        REQUIRE(rc==0 || errno==EAGAIN || errno==EBUSY);
        REQUIRE(timer_ms_gettime64()<deadline);thd_pass();
    }
    REQUIRE(bank_owner.state==ABKB_STAGED && abk_bank_publish(&bank_owner,1000)==0);
    while(bank_owner.state==ABKB_PUBLISHING) {
        int rc=abk_bank_poll(&bank_owner,1000);
        REQUIRE(rc==0 || errno==EAGAIN);
        REQUIRE(timer_ms_gettime64()<deadline);thd_pass();
    }
    REQUIRE(bank_owner.state==ABKB_ACTIVE);
    REQUIRE(aica_music_owner_init(&music_owner,8,1000)==0);
    REQUIRE(await_command()==0 && aica_music_owner_consume(&music_owner)==0);
    REQUIRE(aica_music_owner_open(&music_owner,0,&bank_owner)>=0);
    REQUIRE(await_command()==0 && bank_owner.pending==0);
    generation=music_owner.client.receipt.generation;
    REQUIRE(generation && aica_music_owner_consume(&music_owner)==0);
    args[0]=0x90;args[1]=60;args[2]=100;args[3]=10;
    REQUIRE(aica_music_owner_command(&music_owner,AICA_MUSIC_WIRE_MESSAGE,0,generation,args)>=0);
    REQUIRE(await_command()==0 && aica_music_owner_consume(&music_owner)==0);
    deadline=timer_ms_gettime64()+5000;
    for(;;) {
        memset(args,0,sizeof(args));
        REQUIRE(aica_music_owner_command(&music_owner,AICA_MUSIC_WIRE_QUERY,0,generation,args)>=0);
        REQUIRE(await_command()==0);
        unsigned active=music_owner.client.receipt.active;
        REQUIRE(aica_music_owner_consume(&music_owner)==0);
        if(active) break;
        REQUIRE(timer_ms_gettime64()<deadline);thd_pass();
    }
    thd_sleep(100);
    REQUIRE(aica_music_owner_quiesce(&music_owner)>=0 && await_command()==0);
    REQUIRE(music_owner.client.receipt.active==0 && music_owner.retained && music_owner.count==8);
    int close_rc=abk_bank_close(&bank_owner,1000);
    REQUIRE(close_rc==0 || errno==EAGAIN || errno==EBUSY);
    unsigned busy_seen=close_rc<0 && errno==EBUSY;
    deadline=timer_ms_gettime64()+1000;
    while(timer_ms_gettime64()<deadline) {
        int rc=abk_bank_poll(&bank_owner,1000);
        REQUIRE(rc==0 || errno==EAGAIN || errno==EBUSY);
        if(rc<0 && errno==EBUSY) busy_seen=1;
        REQUIRE(bank_owner.state!=ABKB_CLOSED && bank_owner.address);thd_sleep(10);
    }
    REQUIRE(busy_seen);
    printf("MUSIC LIVE PASS: MIDI start/query/quiesce; played bank and8channels retained; no natural-end or final-fetch claim\n");
hold:
    /* This fixture intentionally remains alive with its ownership. An external
       test runner may terminate emulation; that is not a shutdown proof. */
    for(;;) thd_sleep(1000);
    return 0;
}
