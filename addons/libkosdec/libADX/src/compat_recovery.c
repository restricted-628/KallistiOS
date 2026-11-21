/* Apply previously admitted compatibility faults on the ordinary owner. */
#include "adx_recovery_internal.h"
#include <kosdec/adx_compat_service.h>
#include <errno.h>

static int fail(int error) {
    errno = error;
    return -1;
}

int kosdec_adx_recovery_step_compat(struct kosdec_adx_recovery *r,uint64_t now,
                                    uint32_t timeout) {
    if(media_adx_recovery_clock(r,now,timeout) < 0)
        return -1;
    if(r->compat_stop_timeout)
        return fail(EBUSY);
    if(r->info.phase == KOSDEC_ADX_RECOVERY_STOPPED)
        return 1;

    struct kosdec_adx_compat_service *service = r->player->compat_service;
    if(!service || service->servicing) {
        errno = service ? EBUSY : EINVAL;
        return -1;
    }
    if(r->info.phase == KOSDEC_ADX_RECOVERY_RUNNING) {
        struct kosdec_adx_player_info info;
        if(kosdec_adx_player_get_info(r->player,&info) < 0)
            return -1;
        if(info.state == KOSDEC_ADX_PLAYER_STOPPED) {
            r->info.retained_sound = info.retained_sound;
            r->info.phase = KOSDEC_ADX_RECOVERY_STOPPED;
            return 1;
        }

        uint32_t faults;
        if(kosdec_adx_compat_get_faults(service,r->player->compat_slot,&faults) < 0)
            return -1;
        if(!faults)
            return 0;
        r->info.compat_faults = faults;
        if(!r->info.first_error)
            r->info.first_error = info.first_error;
        if(r->mode == KOSDEC_ADX_RECOVERY_NOACT)
            return 0;

        /* A stalled decoder makes the whole observation ineligible for
           automatic replay, including when it coincides with low DATA. */
        media_adx_recovery_schedule(r,now,!(faults & KOSDEC_ADX_COMPAT_FAULT_STALLED));
    }
    static const struct media_adx_recovery_ops ops = {
        .stop = kosdec_adx_player_stop, .replace = media_adx_player_replace_compat,
        .start = kosdec_adx_player_start
    };
    return media_adx_recovery_finish(r,now,timeout,&ops);
}

static int take_successor(void *context, const struct kosdec_adx_player_info *stopped,
                          struct kosdec_adx_player **next) {
    (void)stopped;
    struct kosdec_adx_recovery *r = context;
    if(!r->compat_successor)
        return fail(ENODATA);
    *next = r->compat_successor;
    r->compat_successor = NULL;
    return 0;
}

static int recover_in_service(struct kosdec_adx_recovery *r, bool admitted) {
    if(r->info.phase != KOSDEC_ADX_RECOVERY_STOPPING) {
        if(!admitted || r->info.phase != KOSDEC_ADX_RECOVERY_RUNNING)
            return 0;
        struct kosdec_adx_player *p = r->player;
        uint32_t faults;
        if(kosdec_adx_compat_get_faults(p->compat_service,p->compat_slot,&faults) < 0)
            return -1;
        if(!faults)
            return 0;
        r->info.compat_faults = faults;
        if(!r->info.first_error)
            r->info.first_error = p->info.first_error;
        if(r->mode == KOSDEC_ADX_RECOVERY_NOACT)
            return 0;
        media_adx_recovery_schedule(r,0,!(faults & KOSDEC_ADX_COMPAT_FAULT_STALLED));
    }

    static const struct media_adx_recovery_ops ops = {
        .stop = media_adx_player_stop_compat_in_service,
        .replace = media_adx_player_replace_compat_in_service,
        .start = media_adx_player_start_compat_in_service
    };
    int result = media_adx_recovery_finish(r,0,r->compat_stop_timeout,&ops);
    if(r->info.phase == KOSDEC_ADX_RECOVERY_STOPPED) {
        struct kosdec_adx_player *p = r->player;
        p->compat_service->titles[p->compat_slot].error = KOSDEC_ADX_COMPAT_NO_ERROR;
    }
    return result < 0 ? -1 : 1;
}

int kosdec_adx_recovery_attach_compat(struct kosdec_adx_recovery *r, struct kosdec_adx_player *p,
    enum kosdec_adx_recovery_mode mode, uint32_t timeout, uint32_t max_replays) {
    struct kosdec_adx_player_info info;
    if(!r || !timeout || kosdec_adx_player_get_info(p,&info) < 0 ||
       info.state >= KOSDEC_ADX_PLAYER_RELEASING)
        return fail(EINVAL);
    if(!p->compat_phased)
        return fail(ENOTSUP);
    struct kosdec_adx_compat_service *service = p->compat_service;
    struct kosdec_adx_compat_title *title = &service->titles[p->compat_slot];
    if(service->servicing || title->recovery)
        return fail(EBUSY);
    if(kosdec_adx_recovery_init(r,p,mode,0,max_replays,take_successor,r) < 0)
        return -1;
    r->compat_stop_timeout = timeout;
    title->recovery = r;
    title->recover = recover_in_service;
    return 0;
}

static struct kosdec_adx_compat_title *recovery_binding(struct kosdec_adx_recovery *r) {
    struct kosdec_adx_recovery_info info;
    if(kosdec_adx_recovery_get_info(r,&info) < 0)
        return NULL;
    struct kosdec_adx_compat_service *service = r->player->compat_service;
    if(!r->compat_stop_timeout || !service ||
       service->titles[r->player->compat_slot].recovery != r) {
        errno = EINVAL;
        return NULL;
    }
    if(service->servicing || info.phase == KOSDEC_ADX_RECOVERY_STOPPING) {
        errno = EBUSY;
        return NULL;
    }
    return &service->titles[r->player->compat_slot];
}

int kosdec_adx_recovery_stage_compat(struct kosdec_adx_recovery *r, struct kosdec_adx_player *next) {
    if(!recovery_binding(r))
        return -1;
    struct kosdec_adx_player_info info;
    if(next && (r->mode != KOSDEC_ADX_RECOVERY_REPLAY ||
       r->info.phase != KOSDEC_ADX_RECOVERY_RUNNING ||
       kosdec_adx_player_get_info(next,&info) < 0 ||
       info.state != KOSDEC_ADX_PLAYER_READY || info.first_error ||
       info.retained_sound != SND_STREAM_INVALID || next->compat_service || !next->source_bound))
        return fail(EINVAL);
    if(next && r->info.replays >= r->max_replays)
        return fail(ENOSPC);
    r->compat_successor = next;
    return 0;
}

int kosdec_adx_recovery_detach_compat(struct kosdec_adx_recovery *r) {
    struct kosdec_adx_compat_title *title = recovery_binding(r);
    if(!title)
        return -1;
    title->recover = NULL;
    title->recovery = NULL;
    r->compat_successor = NULL;
    return 0;
}
