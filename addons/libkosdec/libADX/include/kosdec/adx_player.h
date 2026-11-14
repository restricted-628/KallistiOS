/* Original fork-native audio owner over the bounded ADX producer service. */
#ifndef KOSDEC_ADX_PLAYER_H
#define KOSDEC_ADX_PLAYER_H
#include <kosdec/adx_service.h>
#include <kos/fiber_service.h>
#include <dc/sound/stream.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_player_state {
    KOSDEC_ADX_PLAYER_READY,
    KOSDEC_ADX_PLAYER_PREFILL,
    KOSDEC_ADX_PLAYER_PLAYING,
    KOSDEC_ADX_PLAYER_SOURCE_END, /* All source PCM copied, NOT audible end. */
    KOSDEC_ADX_PLAYER_IDLE, /* Joined/unbound source; output still running. */
    KOSDEC_ADX_PLAYER_RELEASING, /* Cancel requested; join must succeed/retry. */
    KOSDEC_ADX_PLAYER_FAILED,
    KOSDEC_ADX_PLAYER_STOPPING,
    KOSDEC_ADX_PLAYER_STOPPED
};
struct kosdec_adx_player_config {
    int16_t *pcm, *scratch;
    size_t pcm_samples, capacity, scratch_samples;
    void *stack;
    size_t stack_bytes, sound_bytes; /* Sound bytes PER CHANNEL. */
    uint32_t transfer_timeout_ms, prefill_timeout_ms;
};
struct kosdec_adx_player_info {
    enum kosdec_adx_player_state state;
    enum kosdec_adx_status decoder;
    int first_error, stop_error;
    uint32_t sample_rate, output_rate; /* Source Hz and requested playback Hz. */
    uint32_t decoded_loops; /* Source rewinds modulo2^32, not audible loops. */
    unsigned channels;
    uint64_t copied_frames, requested_frames, callbacks, starvations;
    uint64_t source_frames, sources_started;
    bool production_paused;
    /* On successful stop only, caller owns this retained sound allocation.
       It remains allocated; no sample-fetch retirement/reuse is certified. */
    snd_stream_hnd_t retained_sound;
};
/* Fields are implementation-owned; inspect through get_info, not direct edits. */
struct kosdec_adx_player {
    uint32_t magic;
    struct kosdec_adx_player_config memory;
    struct kosdec_adx_service producer;
    struct kosdec_adx_player_info info;
    fiber_service_executor_t *executor;
    fiber_service_t *worker;
    snd_stream_hnd_t sound;
    snd_stream_config_t output;
    uint64_t prefill_deadline;
    uint32_t requested_rate; /* Zero follows source rate. */
    int16_t *scratch;
    bool custom_pan, callback_failed, pcm_enabled, source_bound;
};
/* All player APIs are serialized on one ordinary owner thread, not an IRQ,
   callback or shared fiber carrier. Only the private producer uses the source.
   Application initializes the sound stream system with adequate limits BEFORE
   start, and retains global sound resources; player never init/shuts down sound.
   Do not externally poll/control/attach a sound service to the owned handle.

   Same initialized-source/storage contracts as service_init. Source unpaused,
   sequence started. Caller lends disjoint player/source/provider/PCM/scratch/
   stack storage. capacity power2, >=sound_bytes/2 frames; pcm_samples>=2*capacity.
   sound_bytes multiple64,64..SND_STREAM_BUFFER_MAX_PCM16. scratch_samples >=
   sound_bytes/2+1 (extra sample for exact-split workaround); natural16 alignment.
   Stack>=16384bytes,32-byte-aligned, size multiple32; this provision is not a
   complete stack bound for arbitrary providers. Nonblocking disc input is suited
   to the fork executor; synchronous providers may block and delay stop/join.
   Both timeouts nonzero. Invalid init leaves player unchanged. No allocations
   until start. One output lifecycle: never reinit/restart a live or retained
   player. Sources may be released/rebound while that output keeps running. */
int kosdec_adx_player_init(struct kosdec_adx_player *,
    struct kosdec_adx_stream *, struct kosdec_adx_sequence *,
    const struct kosdec_adx_player_config *);
/* Joint reader alternative with the same output lifecycle. Once started, the
   private producer fiber exclusively owns reader/joint and optional feed.
   No application/IRQ joint mutation while bound. After checked release/cancel
   or stop joins the producer, close reader and separately retire its provider.
   No looping/seek or automatic joint reset; production pause is supported. */
int kosdec_adx_player_init_joint(struct kosdec_adx_player *,
    struct kosdec_adx_joint_reader *, const struct kosdec_adx_player_config *);
/* Create/start the private fiber executor and begin prefill. No sound start yet.
   Poll rejects effective output below8000Hz with ENOTSUP before sound allocation;
   low-rate sources need an explicit in-range output override.
   On failure call stop to retire any partially acquired resources. */
int kosdec_adx_player_start(struct kosdec_adx_player *);
/* Only at SOURCE_END: disable PCM admission and join the producer, entering
   IDLE while output continues. Discards outstanding command receipts. Failure
   retains source/storage for retry or stop; PCM stays disabled. A successful
   return retires CPU source users, NOT provider DMA or audible sound. Caller
   must retire its provider separately before reclaiming/reinitializing it.
   Keep polling during IDLE; zero-byte callbacks become driver-padded silence.
   Join can block; this is not an uninterrupted or gapless transition contract. */
int kosdec_adx_player_release_source(struct kosdec_adx_player *);
/* Cancel a bound source at READY/PREFILL/PLAYING/SOURCE_END, discard queued CPU
   PCM/receipts and join producer. Existing sound keeps running: already submitted
   PCM can still be audible. This is NOT audible pause or immediate device stop.
   Success enters IDLE with output, otherwise unbound READY. Providers retire
   separately before free/reinit. Failure to join leaves RELEASING: poll silence,
   retry cancel_source or stop; retain source/storage. Existing decode errors
   survive cancellation and require stop. Join can block on a provider callback.
   Already unbound READY/IDLE is a no-op. No new sound allocation or restart. */
int kosdec_adx_player_cancel_source(struct kosdec_adx_player *);
/* At IDLE or unbound READY: bind an initialized stream OR started sequence and immediately
   begin prefill, using existing buffers/executor stack. Same source ownership
   as init. Invalid binding leaves IDLE unchanged. An accepted bind resets
   source_frames; other counters remain cumulative. Startup failure requires
   stop. Poll checks rate/channels against the running output before copying
   any new PCM; mismatch fails with EPROTO. No sound alloc/start/reinit occurs.
   The old output tail may still play. Use sequence for queued transitions. */
int kosdec_adx_player_bind_source(struct kosdec_adx_player *,
    struct kosdec_adx_stream *, struct kosdec_adx_sequence *);
/* Same-format joint rebind after prior source release/join. Same ownership and
   lifetime as init_joint; does not close the old reader/provider for caller. */
int kosdec_adx_player_bind_joint(struct kosdec_adx_player *, struct kosdec_adx_joint_reader *);
/* Progress prefill/start or poll output; call regularly, more often than a sound
   ring duration. May block inside checked sound operations (transfer timeout).
   Short live input is silence-padded and counted as starvation. SOURCE_END keeps
   polling silence; caller chooses when to stop. No automatic tail-delay rule or
   playback clock. Failures latch first_error; caller must stop/retry stop. */
int kosdec_adx_player_poll(struct kosdec_adx_player *);
/* Checked linear0..255 per-channel gain and pan (0left,128center,255right).
   May set before start or during playback. Default full volume; mono centered,
   stereo separated. Channel1 settings ignored for mono. This is KOS's scale,
   not CRI's attenuation/pan ABI. Failure may partially update hardware: stop. */
int kosdec_adx_player_set_output(struct kosdec_adx_player *,
    unsigned left_volume, unsigned right_volume, unsigned left_pan, unsigned right_pan);
/* Requested output frequency8000..48000Hz; zero restores the source frequency.
   Before first output, store for start; during output, checked frequency update.
   Restoring a source rate below8000Hz during output returns EINVAL unchanged.
   Hardware quantizes frequency. Speed and pitch change together; no resampling
   or time-stretching, semitone units or fade. Source format/counters are unchanged.
   Rate persists across source bindings. Failure may partly update channels: stop.
   Faster playback shortens the required poll interval and raises decode demand. */
int kosdec_adx_player_set_rate(struct kosdec_adx_player *, uint32_t hz);
/* Forward producer controls and wake the fiber; collect receipt separately.
   Production pause is not audible pause. Valid while a source is bound after
   start, until stop begins. Release discards its remaining receipt.
   One outstanding command; identical ownership/status rules to service API. */
enum kosdec_adx_status kosdec_adx_player_post(struct kosdec_adx_player *,
    const struct kosdec_adx_command *);
bool kosdec_adx_player_take(struct kosdec_adx_player *, enum kosdec_adx_status *);
/* Snapshot owner counters plus synchronized producer status; no hardware query.
   Counters wrap modulo2^64 and count copies/requests, not consumed audio.
   source_frames counts the current (or last released) source; sources_started
   counts admitted sources. Neither is a hardware playback position. */
int kosdec_adx_player_get_info(struct kosdec_adx_player *, struct kosdec_adx_player_info *);
/* Close admission, cancel/wake producer, checked sound stop, detach callback and
   join executor. 0 certifies CPU users stopped; providers still retire separately.
   Retained sound handle/allocation is returned in get_info; NEVER destroyed here.
   -1 retains state/storage; retry stop. first_error survives cleanup; stop_error
   records latest failed stop/join, clears after successful retry. No calls may
   race stop, including status/post. CPU storage can be reclaimed only after
   successful stop and provider retirement, retaining the sound handle separately.
   timeout bounds sound transfer waits, not arbitrary blocking provider or join.
   No final AICA fetch, sound RAM/channel release or restart guarantee. */
int kosdec_adx_player_stop(struct kosdec_adx_player *, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
