/* Actual player/fiber/pipe composition over the existing host Audio model. */
#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_recovery.h>
#include "player_mock.h"
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks,prepares;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"recovery_player:%u %s errno=%d\n",(unsigned)__LINE__,#x,errno); abort(); } } while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_stream source;
    uint8_t *data;size_t bytes;bool fault;
    uint32_t visible;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static struct kosdec_adx_input_result input(void *context,uint32_t off,uint8_t *out,size_t n) {
    struct fixture *f=context;
    if(f->fault) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(f->visible) {
        if(off >= f->visible)
            return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
        if(n > f->visible - off)
            n=f->visible - off;
    }
    if(n>17) n=17;
    memcpy(out,f->data+off,n);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void init(struct fixture *f) {
    memset(f,0,sizeof(*f));f->data=stream(50,18,2,32,65,&f->bytes);
    memset(f->data+20,0,24);
    kosdec_adx_stream_init(&f->source,input,f,(uint32_t)f->bytes,false);
    struct kosdec_adx_player_config config={.pcm=f->pcm,.scratch=f->scratch,.pcm_samples=1024,
        .capacity=512,.scratch_samples=257,.stack=f->stack,.stack_bytes=sizeof(f->stack),
        .sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init(&f->player,&f->source,NULL,&config));
}
static int prepare(void *context,const struct kosdec_adx_player_info *old,struct kosdec_adx_player **next) {
    CHECK(old->state==KOSDEC_ADX_PLAYER_STOPPED && old->first_error==EIO);
    ++prepares;*next=&((struct fixture *)context)->player;return 0;
}
static void linked_player(void) {
    mock_reset();struct fixture f;init(&f);
    f.data=realloc(f.data,2*f.bytes);CHECK(f.data);
    memcpy(f.data+f.bytes,f.data,f.bytes);f.bytes*=2;
    kosdec_adx_stream_init(&f.source,input,&f,(uint32_t)f.bytes,false);
    CHECK(kosdec_adx_stream_set_linked(&f.source,true)==KOSDEC_ADX_DONE);
    CHECK(!kosdec_adx_player_start(&f.player));bool done=false;
    for(unsigned n=0;n<3000;++n) {
        CHECK(!kosdec_adx_player_poll(&f.player));
        struct kosdec_adx_player_info info;CHECK(!kosdec_adx_player_get_info(&f.player,&info));
        if(info.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
            CHECK(info.source_frames==130 && f.source.linked_entries==2 && info.linked);done=true;break;
        }
        mock_pause();
    }
    CHECK(done && !kosdec_adx_player_stop(&f.player,1000));free(f.data);
}
static uint64_t clock_us(void *context) {
    return *(const uint64_t *)context;
}

static void paced_track(struct fixture *f) {
    enum { HEADER=50, BLOCK=36, FRAMES=1024, FOOTER=18 };
    size_t bytes=HEADER + FRAMES/32*BLOCK + FOOTER;
    uint8_t *track=calloc(bytes,1);
    CHECK(track);
    memcpy(track,f->data,HEADER);
    memcpy(track+bytes-FOOTER,f->data+f->bytes-FOOTER,FOOTER);
    put32(track+12,FRAMES);
    free(f->data);
    f->data=track;
    f->bytes=bytes;
    f->visible=HEADER+8*BLOCK;
    kosdec_adx_stream_init(&f->source,input,f,(uint32_t)bytes,false);
}

static int prepare_compat(void *context,const struct kosdec_adx_player_info *old,
                          struct kosdec_adx_player **next) {
    CHECK(old->state==KOSDEC_ADX_PLAYER_STOPPED);
    ++prepares;
    *next=&((struct fixture *)context)->player;
    return 0;
}

static void paced_recovery(uint32_t reasons) {
    const bool transport=reasons==KOSDEC_ADX_COMPAT_FAULT_TRANSPORT;
    const bool stalled=(reasons & KOSDEC_ADX_COMPAT_FAULT_STALLED)!=0;
    const bool low=(reasons & KOSDEC_ADX_COMPAT_FAULT_LOW_DATA)!=0;
    const bool coincident=stalled && low;
    mock_reset();
    prepares=0;
    struct fixture first,next;
    init(&first);
    init(&next);
    first.fault=transport;
    if(!transport)
        paced_track(&first);

    uint64_t now=0;
    const struct kosdec_adx_compat_config config={.clock_us=clock_us,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result serviced;
    struct kosdec_adx_recovery recovery;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_player(&service,0,&first.player));
    uint64_t gain_request = 91;
    CHECK(!kosdec_adx_compat_player_set_attenuation(&first.player,-60,&gain_request) && !gain_request);
    CHECK(kosdec_adx_compat_player_set_direct_level(&first.player,-15)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(kosdec_adx_compat_player_set_fx(&first.player,7,-12)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(!kosdec_adx_compat_set_frequency(&service,0,1000));
    CHECK(!kosdec_adx_recovery_init(&recovery,&first.player,KOSDEC_ADX_RECOVERY_REPLAY,
                                   0,1,prepare_compat,&next));
    CHECK(kosdec_adx_recovery_step(&recovery,0,1000)<0 && errno==EBUSY);
    CHECK(!kosdec_adx_player_start(&first.player));

    bool settled=false;
    for(unsigned i=0;i<1000;++i) {
        struct kosdec_adx_decode_progress progress;
        struct kosdec_adx_player_info player;
        if(!transport)
            CHECK(!kosdec_adx_compat_exec_server(&service,&serviced));
        CHECK(!kosdec_adx_player_get_info(&first.player,&player));
        if(!kosdec_adx_player_get_decode_progress(&first.player,&progress) &&
           (transport ? progress.status==KOSDEC_ADX_IO_ERROR :
            player.state==KOSDEC_ADX_PLAYER_PLAYING && progress.decoded_frames==256 &&
            progress.input_bytes==0)) {
            settled=true;
            break;
        }
        mock_pause();
    }
    CHECK(settled);
    CHECK(!kosdec_adx_compat_set_frequency(&service,0,2));
    /* Place each selected counter at its boundary before the same admitted check. */
    title.position=256;
    title.stalled_checks=stalled ? 10 : 0;
    title.low_checks=low ? 10 : 0;
    now=1000000;
    int poll=kosdec_adx_compat_exec_server(&service,&serviced);
    CHECK(transport ? poll<0 && errno==EIO && serviced.failed_titles==1 : poll==0);
    CHECK(serviced.watchdog_checks==1 && !serviced.watchdog_deferred);
    uint32_t reported;
    CHECK(!kosdec_adx_compat_get_faults(&service,0,&reported) && reported==reasons);
    enum kosdec_adx_compat_error code;
    CHECK(!kosdec_adx_compat_get_error(&service,0,&code) &&
          code==(transport ? KOSDEC_ADX_COMPAT_NO_ERROR :
                 low ? KOSDEC_ADX_COMPAT_LOW_DATA : KOSDEC_ADX_COMPAT_STALLED));

    struct kosdec_adx_recovery_info snapshot;
    if(coincident) {
        mock_failure=MOCK_STOP_TIMEOUT;
        CHECK(kosdec_adx_recovery_step_compat(&recovery,now/1000,1000)<0 && errno==ETIMEDOUT);
        CHECK(!kosdec_adx_recovery_get_info(&recovery,&snapshot));
        CHECK(snapshot.phase==KOSDEC_ADX_RECOVERY_STOPPING && snapshot.stop_error==ETIMEDOUT &&
              snapshot.compat_faults==reasons && !snapshot.first_error &&
              !snapshot.prepare_error && !snapshot.replays && !prepares);
        struct kosdec_adx_player_info stopping;
        CHECK(!kosdec_adx_player_get_info(&first.player,&stopping) &&
              stopping.state==KOSDEC_ADX_PLAYER_STOPPING);
        CHECK(kosdec_adx_compat_unbind(&service,0)<0 && errno==EBUSY);
        CHECK(mock_stops==1 && !mock_joins && title.context==&first.player);
        mock_failure=MOCK_OK;
    }
    CHECK(kosdec_adx_recovery_step_compat(&recovery,now/1000,1000)==(stalled ? 1 : 0));
    CHECK(!kosdec_adx_recovery_get_info(&recovery,&snapshot));
    CHECK(snapshot.compat_faults==reasons && !snapshot.stop_error && !snapshot.prepare_error &&
          snapshot.first_error==(transport ? EIO : 0));
    CHECK(snapshot.replays==(unsigned)!stalled && prepares==(unsigned)!stalled);
    CHECK(snapshot.phase==(stalled ? KOSDEC_ADX_RECOVERY_STOPPED : KOSDEC_ADX_RECOVERY_RUNNING));
    if(stalled) {
        CHECK(mock_stops==(coincident ? 2u : 1u) && mock_joins==1);
        now+=1000000;
        CHECK(!kosdec_adx_compat_exec_server(&service,&serviced));
        CHECK(kosdec_adx_recovery_step_compat(&recovery,now/1000,1000)==1);
        CHECK(title.context==&first.player && mock_starts==1 && !prepares);
        CHECK(!kosdec_adx_recovery_get_info(&recovery,&snapshot) &&
              snapshot.compat_faults==reasons);
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        CHECK(!kosdec_adx_compat_bind_player(&service,0,&next.player));
        CHECK(!kosdec_adx_player_start(&next.player));
    } else {
        CHECK(title.context==&next.player && !first.player.compat_service &&
              next.player.compat_service==&service && title.frequency==2);
        CHECK(!kosdec_adx_compat_get_faults(&service,0,&reported) && !reported);
    }
    int16_t cached;
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(&next.player,&cached));
    CHECK(cached == (stalled ? 0 : -60));
    int32_t bus, db;
    CHECK(!kosdec_adx_compat_player_get_direct_level(&next.player,&db) && db==(stalled ? 0 : -15));
    CHECK(!kosdec_adx_compat_player_get_fx(&next.player,&bus,&db) &&
          bus==(stalled ? 0 : 7) && db==(stalled ? -45 : -12));
    CHECK(next.player.output.channel[0].routing.direct_level==(stalled ? 15 : 12));
    CHECK(next.player.output.channel[0].routing.effect_channel==(stalled ? 0 : 7));
    CHECK(next.player.output.channel[0].routing.effect_send==(stalled ? 0 : 13));

    bool finished=false;
    for(unsigned i=0;i<1000;++i) {
        struct kosdec_adx_player_info player;
        CHECK(!kosdec_adx_compat_exec_server(&service,&serviced));
        CHECK(!kosdec_adx_player_get_info(&next.player,&player));
        if(player.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
            CHECK(player.source_frames==65);
            finished=true;
            break;
        }
        mock_pause();
    }
    CHECK(finished);
    CHECK(!kosdec_adx_player_stop(&next.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(first.data);
    free(next.data);
}

struct recovery_order {
    struct kosdec_adx_recovery *recovery;
    struct kosdec_adx_player *player;
    enum kosdec_adx_player_state state;
    unsigned step;
};

static enum kosdec_adx_compat_state other_state(void *unused) {
    (void)unused;
    return KOSDEC_ADX_COMPAT_PLAYING;
}

static int other_observe(void *unused, struct kosdec_adx_compat_observation *observation) {
    (void)unused;
    *observation = (struct kosdec_adx_compat_observation){.decoder_done=true};
    return 0;
}

static void check_order(const struct recovery_order *order) {
    struct kosdec_adx_player_info info;
    CHECK(order->recovery->player==order->player);
    CHECK(!kosdec_adx_player_get_info(order->player,&info) && info.state==order->state);
}

static int other_title(void *context, enum kosdec_adx_compat_state state) {
    struct recovery_order *order=context;
    CHECK(state==KOSDEC_ADX_COMPAT_PLAYING && order->step++==0);
    check_order(order);
    return 0;
}

static int observe_reload(void *context) {
    struct recovery_order *order=context;
    CHECK(order->step++==1);
    check_order(order);
    return 0;
}

static void recovery_in_visit(enum kosdec_adx_recovery_mode mode, bool direct, bool offer) {
    mock_reset();
    struct fixture first,next;
    init(&first);
    init(&next);
    first.fault=true;
    struct kosdec_adx_recovery recovery;
    struct recovery_order order={&recovery,&first.player,KOSDEC_ADX_PLAYER_FAILED,0};
    uint64_t now=0;
    const struct kosdec_adx_compat_config config={
        .clock_us=clock_us,.clock_context=&now,
        .phases={[KOSDEC_ADX_COMPAT_RELOAD]={observe_reload,&order}}
    };
    const struct kosdec_adx_compat_title_ops witness={
        .state=other_state,.dispatch=other_title,.observe=other_observe
    };
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title titles[3];
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_service_init(&service,titles,3,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&first.player));
    CHECK(!kosdec_adx_compat_bind(&service,2,&witness,&order));
    CHECK(!kosdec_adx_recovery_attach_compat(&recovery,&first.player,mode,1000,1));
    CHECK(kosdec_adx_compat_unbind(&service,0)<0 && errno==EBUSY);
    CHECK(kosdec_adx_recovery_step_compat(&recovery,0,1000)<0 && errno==EBUSY);
    if(offer)
        CHECK(!kosdec_adx_recovery_stage_compat(&recovery,&next.player));
    CHECK(!kosdec_adx_player_start(&first.player));
    if(direct) {
        CHECK(kosdec_adx_compat_exec_server(&service,&pass)<0 && errno==EIO);
        CHECK(!pass.watchdog_checks && order.step==2 && !recovery.info.compat_faults);
    }
    const bool replay=mode==KOSDEC_ADX_RECOVERY_REPLAY && offer;
    order.player=replay?&next.player:&first.player;
    order.state=replay?KOSDEC_ADX_PLAYER_PREFILL:mode==KOSDEC_ADX_RECOVERY_NOACT?
        KOSDEC_ADX_PLAYER_FAILED:KOSDEC_ADX_PLAYER_STOPPED;
    order.step=0;
    now=13332;
    int result=direct?kosdec_adx_compat_exec_title(&service,0,&pass):
        kosdec_adx_compat_exec_server(&service,&pass);
    CHECK(result<0 && errno==EIO && pass.first_error==EIO && pass.watchdog_checks==1);
    CHECK(pass.visited==(direct?1u:5u) && order.step==(direct?0u:2u));
    check_order(&order);
    CHECK(titles[0].context==order.player && titles[0].frequency==60);
    CHECK(service.last_check_us==now &&
          recovery.info.compat_faults==KOSDEC_ADX_COMPAT_FAULT_TRANSPORT);
    CHECK(recovery.info.first_error==EIO && !recovery.info.stop_error);
    CHECK(recovery.info.replays==(unsigned)replay);
    CHECK(recovery.info.prepare_error==(mode==KOSDEC_ADX_RECOVERY_REPLAY && !offer?ENODATA:0));
    CHECK(!kosdec_adx_recovery_detach_compat(&recovery));
    CHECK(!kosdec_adx_player_stop(order.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    CHECK(!kosdec_adx_compat_unbind(&service,2));
    free(first.data);
    free(next.data);
}

static void recovery_in_visit_retry(bool coincident, bool explicit_stop) {
    mock_reset();
    struct fixture first,next;
    init(&first);
    init(&next);
    paced_track(&first);
    struct kosdec_adx_recovery recovery;
    uint64_t now=0;
    const struct kosdec_adx_compat_config config={.clock_us=clock_us,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&first.player));
    CHECK(!kosdec_adx_compat_set_frequency(&service,0,1000));
    CHECK(!kosdec_adx_recovery_attach_compat(&recovery,&first.player,KOSDEC_ADX_RECOVERY_REPLAY,1000,1));
    CHECK(!kosdec_adx_recovery_stage_compat(&recovery,&next.player));
    CHECK(!kosdec_adx_player_start(&first.player));
    for(unsigned i=0;i<30 && first.player.info.state!=KOSDEC_ADX_PLAYER_PLAYING;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&pass));
    CHECK(first.player.info.state==KOSDEC_ADX_PLAYER_PLAYING && mock_starts==1);
    struct kosdec_adx_decode_progress progress;
    CHECK(!kosdec_adx_player_get_decode_progress(&first.player,&progress));
    CHECK(progress.decoded_frames==256 && !progress.input_bytes);
    CHECK(!kosdec_adx_compat_set_frequency(&service,0,2));
    title.low_checks=10;
    title.stalled_checks=coincident?10:0;
    title.position=256;
    now=1000000;
    mock_failure=MOCK_STOP_TIMEOUT;
    CHECK(kosdec_adx_compat_exec_title(&service,0,&pass)<0 && errno==ETIMEDOUT);
    const uint32_t faults=KOSDEC_ADX_COMPAT_FAULT_LOW_DATA |
        (coincident?KOSDEC_ADX_COMPAT_FAULT_STALLED:0u);
    CHECK(pass.watchdog_checks==1 && pass.failed_titles==1 && mock_stops==1);
    CHECK(recovery.info.phase==KOSDEC_ADX_RECOVERY_STOPPING && recovery.info.compat_faults==faults);
    CHECK(recovery.info.stop_error==ETIMEDOUT && !recovery.info.first_error);
    CHECK(first.player.info.state==KOSDEC_ADX_PLAYER_STOPPING && title.context==&first.player);
    CHECK(recovery.compat_successor==&next.player && !recovery.info.replays);
    CHECK(kosdec_adx_recovery_detach_compat(&recovery)<0 && errno==EBUSY);
    CHECK(kosdec_adx_recovery_stage_compat(&recovery,NULL)<0 && errno==EBUSY);
    CHECK(kosdec_adx_compat_unbind(&service,0)<0 && errno==EBUSY);
    mock_failure=MOCK_OK;
    if(explicit_stop)
        CHECK(!kosdec_adx_player_stop(&first.player,1000));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&pass) && !pass.watchdog_checks);
    const bool replay=!coincident && !explicit_stop;
    CHECK(recovery.info.phase==(replay?KOSDEC_ADX_RECOVERY_RUNNING:KOSDEC_ADX_RECOVERY_STOPPED));
    CHECK(recovery.info.compat_faults==faults && !recovery.info.stop_error);
    CHECK(recovery.info.replays==(unsigned)replay && mock_stops==2);
    CHECK(recovery.info.retained_sound==7 && first.player.info.state==KOSDEC_ADX_PLAYER_STOPPED);
    CHECK(next.player.info.state==(replay?KOSDEC_ADX_PLAYER_PREFILL:KOSDEC_ADX_PLAYER_READY));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&pass) && mock_stops==2);
    enum kosdec_adx_compat_error code;
    CHECK(!kosdec_adx_compat_get_error(&service,0,&code) && code==KOSDEC_ADX_COMPAT_NO_ERROR);
    CHECK(!kosdec_adx_recovery_detach_compat(&recovery));
    if(replay)
        CHECK(!kosdec_adx_player_stop(&next.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(first.data);
    free(next.data);
}

int main(void) {
    for(unsigned kind=0;kind<3;++kind) {
        mock_reset();prepares=0;
        struct fixture first,next;init(&first);init(&next);
        if(kind==2) first.data[0]=0;else first.fault=true;
        struct kosdec_adx_recovery r;
        CHECK(!kosdec_adx_recovery_init(&r,&first.player,kind==0?KOSDEC_ADX_RECOVERY_STOP:
            KOSDEC_ADX_RECOVERY_REPLAY,2,1,prepare,&next));
        bool finished=false;
        for(unsigned n=0;n<3000;++n) {
            int result=kosdec_adx_recovery_step(&r,n,1000);
            CHECK(result>=0);
            struct kosdec_adx_player_info pi;
            CHECK(!kosdec_adx_player_get_info(r.player,&pi));
            if(kind==1 && pi.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
                CHECK(prepares==1 && pi.source_frames==65);
                CHECK(!kosdec_adx_player_stop(r.player,1000));
                CHECK(kosdec_adx_recovery_step(&r,n,1000)==1);finished=true;break;
            }
            if(result==1) { CHECK(kind!=1 && prepares==0);finished=true;break; }
            mock_pause();
        }
        CHECK(finished);
        struct kosdec_adx_recovery_info info;
        CHECK(!kosdec_adx_recovery_get_info(&r,&info));
        CHECK(info.first_error==EIO && info.replays==(kind==1));
        CHECK(!kosdec_adx_player_stop(&first.player,1000));
        CHECK(!kosdec_adx_player_stop(&next.player,1000));
        free(first.data);free(next.data);
    }
    linked_player();
    paced_recovery(KOSDEC_ADX_COMPAT_FAULT_LOW_DATA);
    paced_recovery(KOSDEC_ADX_COMPAT_FAULT_STALLED);
    paced_recovery(KOSDEC_ADX_COMPAT_FAULT_TRANSPORT);
    paced_recovery(KOSDEC_ADX_COMPAT_FAULT_STALLED | KOSDEC_ADX_COMPAT_FAULT_LOW_DATA);
    for(unsigned direct=0;direct<2;++direct) {
        recovery_in_visit(KOSDEC_ADX_RECOVERY_NOACT,direct,false);
        recovery_in_visit(KOSDEC_ADX_RECOVERY_STOP,direct,false);
        recovery_in_visit(KOSDEC_ADX_RECOVERY_REPLAY,direct,true);
    }
    recovery_in_visit(KOSDEC_ADX_RECOVERY_REPLAY,false,false);
    recovery_in_visit_retry(true,false);
    recovery_in_visit_retry(false,false);
    recovery_in_visit_retry(false,true);
    printf("ADX_RECOVERY_PLAYER checks=%u failures=0\n",checks);return 0;
}
