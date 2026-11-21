#include "adx_recovery_internal.h"
#include <errno.h>
#define RECOVERY_MAGIC UINT32_C(0x41445859)
static int fail(int e) { errno=e?e:EIO;return -1; }
static bool valid(const struct kosdec_adx_recovery *r) { return r && r->magic==RECOVERY_MAGIC; }
int kosdec_adx_recovery_init(struct kosdec_adx_recovery *r,struct kosdec_adx_player *p,
    enum kosdec_adx_recovery_mode mode,uint32_t delay,uint32_t max,
    kosdec_adx_replay_prepare prepare,void *context) {
    struct kosdec_adx_player_info info;
    if(!r || !p || mode<KOSDEC_ADX_RECOVERY_NOACT || mode>KOSDEC_ADX_RECOVERY_REPLAY ||
       (mode==KOSDEC_ADX_RECOVERY_REPLAY && (!prepare || !max))) return fail(EINVAL);
    if(kosdec_adx_player_get_info(p,&info)<0) return -1;
    *r=(struct kosdec_adx_recovery){.magic=RECOVERY_MAGIC,.mode=mode,.delay_ms=delay,
        .max_replays=max,.prepare=prepare,.context=context,.player=p,
        .info={.retained_sound=SND_STREAM_INVALID}};
    return 0;
}
int kosdec_adx_recovery_get_info(const struct kosdec_adx_recovery *r,
    struct kosdec_adx_recovery_info *out) {
    if(!valid(r) || !out) return fail(EINVAL);
    *out=r->info;return 0;
}
int media_adx_recovery_clock(struct kosdec_adx_recovery *r,uint64_t now,uint32_t timeout) {
    if(!valid(r) || !timeout || (r->clock_started && now<r->last_time)) return fail(EINVAL);
    r->last_time=now;r->clock_started=true;
    return 0;
}
void media_adx_recovery_schedule(struct kosdec_adx_recovery *r,uint64_t now,bool replay_eligible) {
    r->replay_pending=r->mode==KOSDEC_ADX_RECOVERY_REPLAY &&
        replay_eligible && r->info.replays<r->max_replays;
    r->info.deadline=UINT64_MAX-now<r->delay_ms?UINT64_MAX:now+r->delay_ms;
    r->info.phase=KOSDEC_ADX_RECOVERY_WAIT_STOP;
}
int media_adx_recovery_finish(struct kosdec_adx_recovery *r,uint64_t now,uint32_t timeout,
                              const struct media_adx_recovery_ops *ops) {
    if(r->info.phase==KOSDEC_ADX_RECOVERY_WAIT_STOP && now<r->info.deadline) return 0;
    r->info.phase=KOSDEC_ADX_RECOVERY_STOPPING;
    if(ops->stop(r->player,timeout)<0) {
        r->info.stop_error=errno?errno:EIO;return fail(r->info.stop_error);
    }
    r->info.stop_error=0;
    struct kosdec_adx_player_info stopped;
    if(kosdec_adx_player_get_info(r->player,&stopped)<0) return -1;
    r->info.retained_sound=stopped.retained_sound;
    r->info.phase=KOSDEC_ADX_RECOVERY_STOPPED;
    if(!r->replay_pending) return 1;
    r->replay_pending=false;
    struct kosdec_adx_player *next=NULL;
    if(r->prepare(r->context,&stopped,&next)<0) {
        r->info.prepare_error=errno?errno:EIO;return fail(r->info.prepare_error);
    }
    struct kosdec_adx_player_info ready;
    if(!next || kosdec_adx_player_get_info(next,&ready)<0 ||
       ready.state!=KOSDEC_ADX_PLAYER_READY || ready.first_error ||
       ready.retained_sound!=SND_STREAM_INVALID) {
        r->info.prepare_error=EINVAL;return fail(EINVAL);
    }
    if(ops->replace && ops->replace(r->player,next)<0) {
        r->info.prepare_error=errno?errno:EIO;
        return fail(r->info.prepare_error);
    }
    r->player=next;
    r->info.phase=KOSDEC_ADX_RECOVERY_RUNNING;
    int status=ops->start(next);
    if(status<0 && r->compat_stop_timeout) {
        r->info.prepare_error=errno?errno:EIO;
        return fail(r->info.prepare_error);
    }
    ++r->info.replays;
    return status;
}

int kosdec_adx_recovery_step(struct kosdec_adx_recovery *r,uint64_t now,uint32_t timeout) {
    if(valid(r) && (r->player->compat_service || r->compat_stop_timeout)) return fail(EBUSY);
    if(media_adx_recovery_clock(r,now,timeout)<0) return -1;
    if(r->info.phase==KOSDEC_ADX_RECOVERY_STOPPED) return 1;
    if(r->info.phase==KOSDEC_ADX_RECOVERY_RUNNING) {
        struct kosdec_adx_player_info info;
        if(kosdec_adx_player_get_info(r->player,&info)<0) return -1;
        if(info.state==KOSDEC_ADX_PLAYER_STOPPED) {
            r->info.retained_sound=info.retained_sound;
            r->info.phase=KOSDEC_ADX_RECOVERY_STOPPED;return 1;
        }
        int result=info.state==KOSDEC_ADX_PLAYER_READY?
            kosdec_adx_player_start(r->player):kosdec_adx_player_poll(r->player);
        if(result==0) return 0;
        int error=errno?errno:EIO;
        if(!r->info.first_error) r->info.first_error=error;
        if(r->mode==KOSDEC_ADX_RECOVERY_NOACT) return fail(error);
        if(kosdec_adx_player_get_info(r->player,&info)<0) return -1;
        media_adx_recovery_schedule(r,now,info.decoder==KOSDEC_ADX_IO_ERROR || info.input_stalled);
    }
    static const struct media_adx_recovery_ops ops={
        .stop=kosdec_adx_player_stop,.start=kosdec_adx_player_start
    };
    return media_adx_recovery_finish(r,now,timeout,&ops);
}
