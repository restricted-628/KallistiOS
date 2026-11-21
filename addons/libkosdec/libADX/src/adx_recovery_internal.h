#ifndef MEDIA_ADX_RECOVERY_INTERNAL_H
#define MEDIA_ADX_RECOVERY_INTERNAL_H

#include <kosdec/adx_recovery.h>

typedef int (*media_adx_recovery_replace)(struct kosdec_adx_player *, struct kosdec_adx_player *);

struct media_adx_recovery_ops {
    int (*stop)(struct kosdec_adx_player *, uint32_t);
    media_adx_recovery_replace replace;
    int (*start)(struct kosdec_adx_player *);
};
struct kosdec_adx_compat_title;
void media_adx_compat_reset_title_error(struct kosdec_adx_compat_title *);
int media_adx_recovery_clock(struct kosdec_adx_recovery *, uint64_t now, uint32_t timeout);
void media_adx_recovery_schedule(struct kosdec_adx_recovery *, uint64_t now, bool replay_eligible);
int media_adx_recovery_finish(struct kosdec_adx_recovery *, uint64_t now, uint32_t timeout,
                              const struct media_adx_recovery_ops *);
int media_adx_player_replace_compat(struct kosdec_adx_player *stopped, struct kosdec_adx_player *next);
int media_adx_player_replace_compat_in_service(struct kosdec_adx_player *, struct kosdec_adx_player *);
int media_adx_player_stop_compat_in_service(struct kosdec_adx_player *, uint32_t);
int media_adx_player_start_compat_in_service(struct kosdec_adx_player *);

#endif
