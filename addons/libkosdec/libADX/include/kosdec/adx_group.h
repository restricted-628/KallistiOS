/* Original explicit multi-player service/cleanup collection. */
#ifndef KOSDEC_ADX_GROUP_H
#define KOSDEC_ADX_GROUP_H
#include <kosdec/adx_player.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_GROUP_MAX 32u
struct kosdec_adx_group_entry { uint64_t id; struct kosdec_adx_player *player; };
struct kosdec_adx_group {
    uint32_t magic;
    uint64_t next_id;
    unsigned capacity, cursor;
    struct kosdec_adx_group_entry *entries;
};
struct kosdec_adx_group_result {
    uint32_t visited, failed; /* Workspace slot masks, not handle IDs. */
    int first_error;
};
/* Caller-owned 1..32 slots. Init only initially/after finish. Each initialized
   player belongs to at most one collection; all collection/player operations
   serialized on its ordinary owner thread. No scheduler/audio global init.
   Do not copy live collection/workspace/players. IDs are lifetime-monotonic. */
int kosdec_adx_group_init(struct kosdec_adx_group *,
    struct kosdec_adx_group_entry *, unsigned capacity);
int kosdec_adx_group_add(struct kosdec_adx_group *, struct kosdec_adx_player *, uint64_t *id);
/* Visit at most budget slots (1..capacity), round-robin including empty slots.
   Poll active players; READY/STOPPED need no poll; FAILED/STOPPING are reported
   without restarting them. Every selected slot is visited despite failures.
   Private producers still run on their existing fibers. This services output;
   caller must meet every player's refill cadence. No implicit error dispatch.
   0 no selected failure, -1 with masks/first_error; bad args leave out unchanged. */
int kosdec_adx_group_poll(struct kosdec_adx_group *, unsigned budget,
    struct kosdec_adx_group_result *);
/* Checked stop of every member, continuing after failures. Failed members stay
   registered for retry. Successful ones retain sound allocations in their player
   info. Does not remove members, return file loans or retire any providers. */
int kosdec_adx_group_stop_all(struct kosdec_adx_group *, uint32_t timeout_ms,
    struct kosdec_adx_group_result *);
/* Remove only READY (never started) or STOPPED. Required snapshot preserves
   retained_sound/first_error information for caller. No destroy/free/close;
   caller retires providers and retains sound resources separately. */
int kosdec_adx_group_remove(struct kosdec_adx_group *, uint64_t id,
    struct kosdec_adx_player_info *out);
/* Empty only; EBUSY otherwise. Resets collection ownership, never global sound. */
int kosdec_adx_group_finish(struct kosdec_adx_group *);
#ifdef __cplusplus
}
#endif
#endif
