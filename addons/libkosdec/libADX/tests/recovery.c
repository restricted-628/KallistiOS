/* Owner policy fault model; device retirement is deliberately not modeled. */
#include <kosdec/adx_recovery.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, starts, polls, stops, prepares;
static int start_error, poll_error, stop_error, prepare_error, prepare_kind;
static struct kosdec_adx_player players[2];
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"recovery:%u %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
int kosdec_adx_player_get_info(struct kosdec_adx_player *p,struct kosdec_adx_player_info *out) {
    if(!p || !p->magic) { errno=EINVAL;return -1; } *out=p->info;return 0;
}
int kosdec_adx_player_start(struct kosdec_adx_player *p) {
    ++starts;
    if(start_error) { p->info.state=KOSDEC_ADX_PLAYER_FAILED;errno=start_error;return -1; }
    p->info.state=KOSDEC_ADX_PLAYER_PREFILL;return 0;
}
int kosdec_adx_player_poll(struct kosdec_adx_player *p) {
    ++polls;
    if(poll_error) { p->info.state=KOSDEC_ADX_PLAYER_FAILED;errno=poll_error;return -1; }
    return 0;
}
int kosdec_adx_player_stop(struct kosdec_adx_player *p,uint32_t timeout) {
    CHECK(timeout==77);++stops;
    if(stop_error) { errno=stop_error;return -1; }
    p->info.state=KOSDEC_ADX_PLAYER_STOPPED;p->info.retained_sound=7;return 0;
}
static int prepare(void *context,const struct kosdec_adx_player_info *old,struct kosdec_adx_player **out) {
    CHECK(context==players && old->state==KOSDEC_ADX_PLAYER_STOPPED && old->retained_sound==7);
    ++prepares;
    if(prepare_error) { errno=prepare_error;return -1; }
    *out=prepare_kind==1?NULL:&players[1];
    if(prepare_kind==2) players[1].info.state=KOSDEC_ADX_PLAYER_PLAYING;
    if(prepare_kind==3) players[1].info.first_error=EIO;
    if(prepare_kind==4) players[1].info.retained_sound=9;
    return 0;
}
static void reset(void) {
    memset(players,0,sizeof(players));
    for(unsigned i=0;i<2;++i) {
        players[i].magic=1;players[i].info.state=KOSDEC_ADX_PLAYER_READY;
        players[i].info.retained_sound=SND_STREAM_INVALID;
    }
    starts=polls=stops=prepares=0;
    start_error=poll_error=stop_error=prepare_error=prepare_kind=0;
}
static void init(struct kosdec_adx_recovery *r,enum kosdec_adx_recovery_mode mode,unsigned delay) {
    CHECK(!kosdec_adx_recovery_init(r,players,mode,delay,1,prepare,players));
}
int main(void) {
    struct kosdec_adx_recovery r;
    struct kosdec_adx_recovery_info info;
    reset();
    CHECK(kosdec_adx_recovery_init(&r,players,KOSDEC_ADX_RECOVERY_REPLAY,0,1,NULL,NULL)<0);
    CHECK(kosdec_adx_recovery_init(&r,players,KOSDEC_ADX_RECOVERY_REPLAY,0,0,prepare,players)<0);
    init(&r,KOSDEC_ADX_RECOVERY_NOACT,0);
    CHECK(kosdec_adx_recovery_step(&r,0,0)<0 && !starts);
    CHECK(!kosdec_adx_recovery_step(&r,10,77) && starts==1);
    CHECK(kosdec_adx_recovery_step(&r,9,77)<0 && errno==EINVAL && !polls);
    players[0].info.state=KOSDEC_ADX_PLAYER_SOURCE_END;
    CHECK(!kosdec_adx_recovery_step(&r,10,77) && polls==1 && !stops);
    poll_error=EPIPE;
    CHECK(kosdec_adx_recovery_step(&r,11,77)<0 && errno==EPIPE && !stops);
    CHECK(!kosdec_adx_recovery_get_info(&r,&info) && info.first_error==EPIPE);
    for(unsigned mode=KOSDEC_ADX_RECOVERY_STOP;mode<=KOSDEC_ADX_RECOVERY_REPLAY;++mode) {
        for(unsigned source=0;source<2;++source) {
            reset();init(&r,(enum kosdec_adx_recovery_mode)mode,25);
            CHECK(!kosdec_adx_recovery_step(&r,100,77));
            players[0].info.decoder=source?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_TRUNCATED;
            poll_error=EIO;
            CHECK(!kosdec_adx_recovery_step(&r,101,77) && !stops);
            CHECK(!kosdec_adx_recovery_step(&r,125,77) && !stops);
            stop_error=EBUSY;
            CHECK(kosdec_adx_recovery_step(&r,126,77)<0 && errno==EBUSY && stops==1 && !prepares);
            CHECK(!kosdec_adx_recovery_get_info(&r,&info) && info.first_error==EIO && info.stop_error==EBUSY);
            stop_error=0;
            bool replay=mode==KOSDEC_ADX_RECOVERY_REPLAY && source;
            CHECK(kosdec_adx_recovery_step(&r,127,77)==(replay?0:1));
            CHECK(stops==2 && prepares==(unsigned)replay && starts==1+(unsigned)replay);
            CHECK(!kosdec_adx_recovery_get_info(&r,&info) && !info.stop_error && info.retained_sound==7);
            if(replay) {
                players[1].info.decoder=KOSDEC_ADX_IO_ERROR;
                CHECK(!kosdec_adx_recovery_step(&r,128,77));
                CHECK(kosdec_adx_recovery_step(&r,153,77)==1 && prepares==1);
            }
            CHECK(kosdec_adx_recovery_step(&r,154,77)==1);
        }
    }
    for(unsigned kind=0;kind<=5;++kind) {
        reset();init(&r,KOSDEC_ADX_RECOVERY_REPLAY,0);
        players[0].info.state=KOSDEC_ADX_PLAYER_FAILED;players[0].info.decoder=KOSDEC_ADX_IO_ERROR;
        poll_error=EIO;prepare_kind=(int)kind;
        if(kind==0) prepare_error=ENOMEM;
        if(kind==5) start_error=ENOSPC;
        CHECK(kosdec_adx_recovery_step(&r,0,77)<0 && prepares==1 && stops==1);
        CHECK(!kosdec_adx_recovery_get_info(&r,&info) && info.first_error==EIO);
        if(kind<5) CHECK(info.phase==KOSDEC_ADX_RECOVERY_STOPPED && info.prepare_error);
        else {
            CHECK(info.phase==KOSDEC_ADX_RECOVERY_RUNNING && info.replays==1);
            CHECK(kosdec_adx_recovery_step(&r,1,77)==1 && prepares==1 && stops==2);
        }
    }
    for(unsigned stall=0;stall<2;++stall) {
        reset();init(&r,KOSDEC_ADX_RECOVERY_REPLAY,0);
        CHECK(!kosdec_adx_recovery_step(&r,0,77));
        players[0].info.input_stalled=stall;
        poll_error=ETIMEDOUT;
        CHECK(kosdec_adx_recovery_step(&r,1,77)==(stall?0:1));
        CHECK(prepares==stall && stops==1);
    }
    reset();init(&r,KOSDEC_ADX_RECOVERY_STOP,99);start_error=ENOMEM;
    CHECK(!kosdec_adx_recovery_step(&r,UINT64_MAX-10,77) && !stops);
    CHECK(!kosdec_adx_recovery_step(&r,UINT64_MAX-1,77) && !stops);
    CHECK(kosdec_adx_recovery_step(&r,UINT64_MAX,77)==1 && stops==1);
    reset();init(&r,KOSDEC_ADX_RECOVERY_STOP,0);players[0].info.state=KOSDEC_ADX_PLAYER_STOPPED;
    players[0].info.retained_sound=6;
    CHECK(kosdec_adx_recovery_step(&r,0,77)==1 && !stops);
    CHECK(!kosdec_adx_recovery_get_info(&r,&info) && info.retained_sound==6);
    printf("ADX_RECOVERY checks=%u failures=0\n",checks);return 0;
}
