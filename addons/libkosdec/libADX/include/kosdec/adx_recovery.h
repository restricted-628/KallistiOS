/* Original owner-driven failure policy over checked native player shutdown. */
#ifndef KOSDEC_ADX_RECOVERY_H
#define KOSDEC_ADX_RECOVERY_H
#include <kosdec/adx_player.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_recovery_mode { KOSDEC_ADX_RECOVERY_NOACT,
    KOSDEC_ADX_RECOVERY_STOP, KOSDEC_ADX_RECOVERY_REPLAY };
enum kosdec_adx_recovery_phase { KOSDEC_ADX_RECOVERY_RUNNING,
    KOSDEC_ADX_RECOVERY_WAIT_STOP, KOSDEC_ADX_RECOVERY_STOPPING,
    KOSDEC_ADX_RECOVERY_STOPPED };
/* Called once per admitted replay AFTER checked software stop/join succeeds.
   The old sound allocation in stopped->retained_sound REMAINS ALLOCATED. Owner
   must retain it until the Audio contract permits reclamation; do not destroy or
   reuse it based on this callback. Return0 with *next a properly initialized
   READY player, zero first_error and no retained sound allocation. Rewind/reopen
   input only after its own users/transport retire; immutable input may be reused.
   All new/old storage remains caller-owned even if preparation fails. On failure
   return-1/errno; no automatic preparation retry. No global sound reset. */
typedef int (*kosdec_adx_replay_prepare)(void *context,
    const struct kosdec_adx_player_info *stopped, struct kosdec_adx_player **next);
struct kosdec_adx_recovery_info {
    enum kosdec_adx_recovery_phase phase;
    int first_error, stop_error, prepare_error;
    uint32_t replays;
    uint64_t deadline;
    snd_stream_hnd_t retained_sound;
};
struct kosdec_adx_recovery {
    uint32_t magic, delay_ms, max_replays;
    enum kosdec_adx_recovery_mode mode;
    struct kosdec_adx_player *player;
    kosdec_adx_replay_prepare prepare;
    void *context;
    struct kosdec_adx_recovery_info info;
    uint64_t last_time;
    bool clock_started, replay_pending;
};
/* Initialize on the player's serialized ordinary owner thread; only after any
   previous recovery users stop. Caller owns all player/source/storage lifetimes.
   REPLAY requires a callback and max_replays>0. No automatic sound reclamation.
   Delay is milliseconds from observed failure, not SDK server-call counts. */
int kosdec_adx_recovery_init(struct kosdec_adx_recovery *, struct kosdec_adx_player *,
    enum kosdec_adx_recovery_mode, uint32_t delay_ms, uint32_t max_replays,
    kosdec_adx_replay_prepare, void *context);
/* Owner calls regularly with nondecreasing monotonic milliseconds. Starts READY
   players or polls active ones. NOACT returns their errors without cleanup.
   STOP schedules then performs checked stop; failures retain resources and retry
   on the next step. REPLAY additionally prepares/starts a replacement ONLY for
   a terminal source IO_ERROR, up to max_replays. Other errors stop. A successful
   replay starts at the position supplied by prepare (owner must rewind for SDK
   file-from-start behavior). Normal SOURCE_END is not a fault or audible end.
   Return0 active/waiting/replayed,1 stopped,-1 operation/preparation error.
   Stop timeout has the existing player limitation: a provider/join may block.
   No implicit diagnostic callback dispatch. Do not service the same player from
   another owner/group while a recovery step is in progress. */
int kosdec_adx_recovery_step(struct kosdec_adx_recovery *, uint64_t now_ms,
    uint32_t stop_timeout_ms);
int kosdec_adx_recovery_get_info(const struct kosdec_adx_recovery *,
    struct kosdec_adx_recovery_info *);
#ifdef __cplusplus
}
#endif
#endif
