/* Original fork-native audio owner over the bounded ADX producer service. */
#ifndef KOSDEC_ADX_PLAYER_H
#define KOSDEC_ADX_PLAYER_H
#include <kosdec/adx_service.h>
#include <kos/fiber_service.h>
#include <dc/sound/stream.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Signed pan positions; AUTO selects mono center or stereo separation. */
#define KOSDEC_ADX_PAN_AUTO (-128)
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
    uint32_t input_stall_timeout_ms; /* Zero disables the running input watchdog. */
    bool use_lsc_reload; /* Carry the player reload setting into each linked start. */
};
struct kosdec_adx_player_info {
    enum kosdec_adx_player_state state;
    enum kosdec_adx_status decoder;
    int first_error, stop_error;
    uint32_t sample_rate, output_rate; /* Source Hz and requested playback Hz. */
    uint32_t queued_sources; /* Pending sequence decodes or linked-loader reads. */
    uint32_t decoded_loops; /* Source rewinds modulo2^32, not audible loops. */
    unsigned channels;
    uint64_t copied_frames, requested_frames, callbacks, starvations;
    uint64_t source_frames, sources_started;
    uint64_t source_generation; /* Current opt-in retained output generation; otherwise zero. */
    bool production_paused;
    bool input_stalled; /* A sustained input wait caused short PCM refills. */
    bool linked; /* Requested adjacent-record follow mode, not audible state. */
    bool wait_start, ready_start; /* Owner-thread gate and preparation snapshot. */
    /* On successful stop only, caller owns this retained sound allocation.
       It remains allocated; no sample-fetch retirement/reuse is certified. */
    snd_stream_hnd_t retained_sound;
};
struct kosdec_adx_player_error {
    int code; /* Native errno, zero when no diagnostic is latched. */
    enum kosdec_adx_status decoder;
    enum kosdec_adx_player_state state; /* State when the failure was observed. */
};
struct kosdec_adx_player_input {
    const struct kosdec_adx_joint *joint; /* Identity only while the producer is bound. */
    uint32_t queued_bytes, sectors;
    uint32_t safety_limit_bytes;
    float seconds; /* Nominal compressed ADX duration. */
    bool safe;
};
struct kosdec_adx_filter_settings {
    int32_t cutoff, resonance;
};
struct kosdec_adx_source_output {
    uint64_t epoch, generation, output_frames, source_frames, next_source_frame;
    uint64_t observation_start_ms, observation_end_ms;
    uint32_t session;
    int clock_error, output_error, stop_error;
    bool valid, source_output_end, software_stopped;
};
struct kosdec_adx_audio_time {
    int64_t frames, centiseconds;
    uint32_t sample_rate;
};
enum kosdec_adx_vsync_mode {
    KOSDEC_ADX_VSYNC_NTSC = 1,
    KOSDEC_ADX_VSYNC_PAL
};
enum kosdec_adx_time_mode {
    KOSDEC_ADX_TIME_SOURCE,
    KOSDEC_ADX_TIME_NTSC = KOSDEC_ADX_VSYNC_NTSC,
    KOSDEC_ADX_TIME_PAL = KOSDEC_ADX_VSYNC_PAL
};
struct kosdec_adx_time {
    int64_t count;
    uint32_t frequency;
};
struct kosdec_adx_vsync_time {
    int64_t count;
    uint32_t frequency;
};
typedef uint32_t (*kosdec_adx_vsync_ticks)(void *context);
enum kosdec_adx_attenuation_state {
    KOSDEC_ADX_ATTENUATION_PENDING,
    KOSDEC_ADX_ATTENUATION_APPLIED,
    KOSDEC_ADX_ATTENUATION_FAILED
};
#define KOSDEC_ADX_ATTENUATION_LEFT  1u
#define KOSDEC_ADX_ATTENUATION_RIGHT 2u
struct kosdec_adx_attenuation_status {
    uint64_t epoch, request;
    uint32_t session;
    enum kosdec_adx_attenuation_state state;
    unsigned admitted, applied, rejected, unknown, acknowledged;
    uint8_t requested_tl[2], acknowledged_tl[2];
    int error;
};
struct snd_retained_pcm;
struct snd_ordinary_pcm16;
struct kosdec_adx_flycast_pause;
struct kosdec_adx_compat_service;
struct kosdec_adx_compat_controls {
    int8_t direct_db, effect_db, transpose, detune;
    uint8_t effect_bus;
};
struct kosdec_adx_playend {
    uint64_t epoch, generation;
    uint32_t session, frames, rate, stop_timeout_ms;
    int32_t anchor;
    bool pending, completed;
};
typedef void (*kosdec_adx_player_error_callback)(void *context,
    const struct kosdec_adx_player_error *error);
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
    uint64_t prefill_deadline, input_stall_since;
    uint32_t input_wait_generation; /* Atomic producer publication. */
    uint32_t input_stall_generation; /* Serialized owner observation. */
    uint32_t requested_rate; /* Zero follows source rate. */
    int16_t *scratch;
    int16_t requested_pan[2]; /* Signed -15..15 or KOSDEC_ADX_PAN_AUTO. */
    bool callback_failed, pcm_enabled, source_bound, decode_progress;
    struct kosdec_adx_compat_service *compat_service;
    unsigned compat_slot;
    uint32_t compat_prefill_frames;
    uint8_t compat_block_frames;
    bool compat_chained, compat_phased, compat_renderer_ready;
    bool compat_ready_start; /* Last prepared result for the current compatibility binding. */
    bool compat_decode_ended; /* Logical DECEND, independent of final PCM submission. */
    bool compat_renderer_failed_playing; /* Preserve the admitted title phase in GetStat. */
    bool compat_paused, compat_preoutput_playing, compat_discarded_all;
    struct kosdec_adx_player_error diagnostic;
    kosdec_adx_player_error_callback error_callback;
    void *error_context;
    bool error_pending;
    struct kosdec_adx_filter_settings filter_settings;
    uint32_t *retained_offsets;
    struct snd_retained_pcm *retained_pcm;
    unsigned retained_admissions;
    bool retained_sealed, retained_stopped, retained_close_pending;
    int32_t time_offset;
    kosdec_adx_vsync_ticks vsync_ticks;
    void *vsync_context;
    uint64_t vsync_generation;
    int64_t vsync_count;
    uint32_t vsync_frequency, vsync_last_tick;
    int32_t compat_vsync_anchor;
    uint32_t compat_vsync_anchor_tick;
    struct kosdec_adx_playend compat_playend;
    int16_t requested_attenuation;
    int16_t compat_requested_attenuation;
    bool compat_initial_attenuation;
    uint8_t compat_initial_tl; /* Staged by the owner for a fresh lease. */
    struct kosdec_adx_compat_controls compat_controls;
    uint32_t compat_start_rate;
    int16_t compat_start_pitch_units;
    bool compat_start_pitch;
    unsigned compat_route_fields;
    snd_channel_routing_t compat_original_routing[2];
    bool time_uses_vsync;
    uint32_t linked_reload_sectors;
    bool linked_reload_clamp;
    bool ordinary_reprime_pending;
    bool ordinary_stereo, ordinary_mono_clock;
    struct snd_ordinary_pcm16 *ordinary_pcm;
    struct kosdec_adx_flycast_pause *flycast_pause;
    uint64_t flycast_stopped_epoch;
    bool flycast_pause_enabled, flycast_pause_unstarted;
    struct {
        uint64_t source_first;
        uint32_t source_frames;
    } ordinary_halves[2];
};
/* All player APIs are serialized on one ordinary owner thread, not an IRQ,
   callback or shared fiber carrier. The private producer owns the source
   unless the caller explicitly selects the synchronous compatibility binding.
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
   Transfer and prefill timeouts must be nonzero. The optional running stall
   timeout uses elapsed monotonic milliseconds. It requires repeated short PCM
   refills while a live source continuously waits for input. Progress, pause,
   a full refill and terminal status disarm it. A stalled player fails with
   ETIMEDOUT and sets input_stalled. Only configure it for providers expected to
   deliver continuously. A blocked synchronous callback can still delay join. Invalid init leaves player unchanged. No allocations
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
/* PCM WAV source; provider/reader owned by the private producer until join.
   Same output/retained-resource rules as ordinary init. No source seek or reinit
   while bound; source completion remains distinct from audible completion. */
int kosdec_adx_player_init_wave(struct kosdec_adx_player *, struct kosdec_adx_wave *,
                              const struct kosdec_adx_player_config *);

/* AU PCM/mu-law source; provider/reader owned by the private producer until join.
   Same output/retained-resource rules as ordinary init. No source seek or reinit
   while bound; source completion remains distinct from audible completion. */
int kosdec_adx_player_init_au(struct kosdec_adx_player *, struct kosdec_adx_au *,
                              const struct kosdec_adx_player_config *);

/* AIFF PCM source; provider/reader owned by the private producer until join.
   Same output/retained-resource rules as ordinary init. No source seek or reinit
   while bound; source completion remains distinct from audible completion. */
int kosdec_adx_player_init_aiff(struct kosdec_adx_player *, struct kosdec_adx_aiff *,
                              const struct kosdec_adx_player_config *);
/* SPSD mono/stereo PCM16; same source lifetime and output rules. */
int kosdec_adx_player_init_spsd(struct kosdec_adx_player *, struct kosdec_adx_spsd *,
                              const struct kosdec_adx_player_config *);

/* Create/start the private fiber executor and begin prefill. No sound start yet.
   Poll rejects effective output below8000Hz with ENOTSUP before sound allocation;
   low-rate sources need an explicit in-range output override.
   On failure call stop to retire any partially acquired resources. */
int kosdec_adx_player_start(struct kosdec_adx_player *);
/* Opt in with config.use_lsc_reload. Pass a fresh linked reader and a stopped
   loader over an empty joint to start/bind. The player configures and starts
   that loader before handing it to the producer. Other source types ignore
   this profile. The default is trunc(binary32(whole sectors)*0.85f).
   The setters cache the next linked start's setting, including while playing.
   The current loader keeps its requested limit across linked records. Limits
   are in 2048-byte sectors, including zero which blocks refill after capture.
   An explicit count must fit the next joint. The time form uses the ADX
   seconds conversion and clamps to that joint. Unsupported profile or
   invalid inputs leave the cache unchanged. Rebuild player consumers. */
int kosdec_adx_player_set_linked_reload_sectors(struct kosdec_adx_player *, uint32_t sectors);
int kosdec_adx_player_set_linked_reload_time(struct kosdec_adx_player *,
    float seconds, unsigned channels, unsigned rate);
/* Serialized player owner. Latest producer-published compressed ADX input.
   For joints this is queued DATA excluding held DATA. For a read-at source it
   is the unconsumed staging interval of the stream or active sequence entry.
   Sectors are whole 2048-byte units. A direct source stages at most 512 bytes,
   so it reports zero sectors. Seconds use whole groups and the validated
   source rate, including format-ready prefill, independent of output pitch.
   Safe compares queued bytes with the live player cache. Default and time
   limits use the bound input capacity. An explicit sector limit can exceed
   staging capacity. This may differ from the active loader refill gate.
   Identity is for comparison only. Do not inspect or mutate the joint during
   producer ownership. It is null for a read-at source. Before start, after
   completed unbind/stop and for direct PCM sources, returns a zero snapshot.
   Invalid arguments return -1/EINVAL and leave the output unchanged. */
int kosdec_adx_player_get_input(const struct kosdec_adx_player *, struct kosdec_adx_player_input *);

/* Serialized player owner. Opt in before starting or at IDLE before rebind.
   Future rebinds start a fresh decoded count.
   No sample/device clock is implied. Query returns ENODATA if unbound or
   stopped, and EAGAIN if a producer publication overlaps the query. */
int kosdec_adx_player_enable_decode_progress(struct kosdec_adx_player *);
int kosdec_adx_player_get_decode_progress(const struct kosdec_adx_player *,
                                         struct kosdec_adx_decode_progress *);
/* Optional fixed-rate retained clock profile. Build the archive with
   ADX_RETAINED_CLOCK=1. Enable before the first start with a disjoint caller
   buffer of at least config.capacity uint32_t offsets, lent until stop.
   Sound needs at least 2048 and at most 65536 bytes per channel. Use default
   gain/pan and a fixed requested rate. Later source binds must match format.
   A lease supports 16 source generations and 16 spans per Audio fill. A fill
   that would exceed the span limit fails before consuming source PCM. */
int kosdec_adx_player_enable_retained_clock(struct kosdec_adx_player *,
    uint32_t *source_offsets, size_t offset_count);
/* Snapshot only. Generation zero queries output time without source attribution.
   Check valid before using the cursor. End denotes measured traversal of that
   generation's source spans. Audible end and physical retirement are separate.
   A successful stop retains the Audio lease. Snapshot access remains valid.
   The Flycast-only pause opt-in also supports source snapshots with one
   generation, slot-zero running credit, and adapter-local epochs. These
   snapshots have no retained firmware session identity. The checked
   ordinary mono profile exposes one source generation on its own lease. */
int kosdec_adx_player_get_source_output(const struct kosdec_adx_player *,
    uint64_t generation, struct kosdec_adx_source_output *);
/* Set a signed display offset in the units of the query, source frames or
   VSync count units. Starts at zero and persists across source bindings.
   The VSync drift check uses this value in source frames, as in the SDK. */
int kosdec_adx_player_set_time_offset(struct kosdec_adx_player *, int32_t offset);
/* Read cumulative credited source frames plus the display offset for one
   nonzero generation. Centiseconds use the decoded source rate and truncate
   toward zero. Requires a qualified retained clock. Failure leaves out intact. */
int kosdec_adx_player_get_audio_time(const struct kosdec_adx_player *,
    uint64_t generation, struct kosdec_adx_audio_time *out);
/* Enable after retained-clock setup and before the first start. The owner
   supplies a nonblocking counter read with stable cadence in the selected
   mode. One tick per display VBlank. Wrap at 2^32 is supported. Read it at
   least once per full counter cycle. The callback runs on the player owner,
   must not reenter the player, and remains borrowed through stop. */
int kosdec_adx_player_enable_vsync_clock(struct kosdec_adx_player *,
    enum kosdec_adx_vsync_mode, kosdec_adx_vsync_ticks, void *context);
/* Query the current admitted generation during active playback or drain.
   One tick is 100 units, at frequency 5994 (NTSC) or 5000 (PAL). With
   more than 60 ms of drift, this query returns the prior estimate and
   adjusts the next query's anchor. Failed queries leave out untouched.
   Returns ENOTSUP at source-output end, where the terminal clock depends
   on a separate Audio completion contract. */
int kosdec_adx_player_get_vsync_time(struct kosdec_adx_player *,
    uint64_t generation, struct kosdec_adx_vsync_time *out);
/* Select the owner's display units. The default is source frames. NTSC/PAL
   require a configured VSync counter. Change cadence with the display and
   keep the counter continuous. Mode changes preserve the active VSync anchor,
   do not read the counter, and persist across source bindings. The explicit
   audio and VSync query APIs remain available regardless of selection. */
int kosdec_adx_player_set_time_mode(struct kosdec_adx_player *, enum kosdec_adx_time_mode);
/* Query the selected qualified time pair, or independently query its signed
   centiseconds with truncation toward zero. Failed queries leave out intact.
   Each VSync query may adjust the anchor for the next query. */
int kosdec_adx_player_get_time(struct kosdec_adx_player *, uint64_t generation,
    struct kosdec_adx_time *out);
int kosdec_adx_player_get_time_real(struct kosdec_adx_player *, uint64_t generation,
    int64_t *centiseconds);
/* Compatibility-selected display units. NTSC/PAL may be selected without
   a tick reader for literal pre-start and checked-STOPPED results. A live
   VSync query uses the opted-in collection's delivered events or the
   player's tick callback. It still requires a qualified source clock. */
int kosdec_adx_compat_player_set_time_mode(struct kosdec_adx_player *, enum kosdec_adx_time_mode);
/* Compatibility-bound only. READY/PREFILL/STOPPED return the stored offset
   in the selected units, with denominator 1/5994/5000. PLAYING/SOURCE_END
   use the current qualified generation. Optional logical PLAYEND uses the
   latched declared duration and correction anchor after a checked stop.
   The phased unrecognized-header DECINFO case also returns the inactive
   offset and denominator. Other failed, idle, stopping and releasing
   states return ENODATA.
   Live counts use signed 32-bit wrap. The compatibility VSync anchor is
   separate from the native anchor and changes only on drift correction.
   The centisecond query independently reads the selected pair and uses
   binary32 rounding and signed 32-bit saturated truncation. Errors leave
   caller outputs unchanged. The native time queries keep wide arithmetic. */
int kosdec_adx_compat_player_get_time(struct kosdec_adx_player *, struct kosdec_adx_time *);
int kosdec_adx_compat_player_get_time_real(struct kosdec_adx_player *, int64_t *centiseconds);
/* Set before start or during prefill to hold source PCM admission. Producer
   keeps filling the bounded ring; poll reports readiness without allocating or
   starting fresh output. Existing retained output continues polling silence.
   Clearing the gate allows a subsequent poll to admit primed PCM. Changing
   it after playback starts only affects later sources, never pauses sound.
   Persists across source binds. READY/PREFILL/PLAYING/SOURCE_END/IDLE only;
   no provider/device work in this setter. This is not an audible pause API. */
int kosdec_adx_player_set_wait_start(struct kosdec_adx_player *, bool wait);

/* Compatibility title pause before output admission. Preparation continues.
   When ready and the separate wait-start gate is open, the compatibility
   title reports PLAYING and zero elapsed source frames. Clearing pause lets
   the next service pass start output when primed. Checked STOP preserves the
   request. Playback with an admitted Audio handle requires the explicit
   Flycast active-pause opt-in. */
int kosdec_adx_compat_player_pause(struct kosdec_adx_player *, bool paused);
/* Experimental Flycast-only pause profile. Enable a READY compatibility
   title with one finite direct mono or stereo ADX source at 22,050 to
   44,100 Hz and a 16,384-frame sound ring per channel. Startup output
   Hz must match the source. Enable checked ordinary output first.
   Requires matching build-gated Audio and libADX. */
int kosdec_adx_compat_player_enable_flycast_pause(struct kosdec_adx_player *);
/* With pause clear, return zero discarded without servicing the title.
   With one bound, paused title preparing its first output, a nonpositive
   request services the collection once and re-anchors time at zero elapsed
   frames. A positive request of 1..15 frames does the same after preparation
   begins for a direct nonlinked ADX stream. Larger requests on a fully
   decoded, ordinary direct source round down to 16-frame units and then
   remove at most the available common PCM. The cursor changes only after
   successful service, and the result is the actual count per channel.
   No VBlank is delivered and discarded frames do not advance playback
   time. Releasing an entirely discarded source ends decode without
   allocating Audio. Other paused requests return ENOTSUP. Service failure
   or a rejected request leaves the cursor and discarded unchanged. */
int kosdec_adx_compat_player_discard_samples(struct kosdec_adx_player *,
    int32_t requested, int32_t *discarded);
/* Owner-thread snapshot, updated by poll or compatibility title dispatch.
   Format-validated native prefill or sourceDONE sets ready. In the optional
   compatibility service the captured frame target or one decoder block of
   remaining PCM room also sets ready. Starting playback may still need to
   prime the native sound buffer. False initially and after source release,
   cancel, a new source start, failure or stop. No I/O; invalid arguments leave
   output unchanged. Explicitly holding after ready suspends the prefill
   timeout. Releasing a prepared compatibility title renews its priming window. */
int kosdec_adx_player_is_ready_start(const struct kosdec_adx_player *, bool *ready);
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
/* Rebind after source release/cancel; same-format output rules as bind_source.
   Old provider remains caller-owned; successful join precedes reader reuse. */
int kosdec_adx_player_bind_wave(struct kosdec_adx_player *, struct kosdec_adx_wave *);

/* Rebind after source release/cancel; same-format output rules as bind_source.
   Old provider remains caller-owned; successful join precedes reader reuse. */
int kosdec_adx_player_bind_au(struct kosdec_adx_player *, struct kosdec_adx_au *);
/* AIFF rebind uses the same source-release and same-format output contract. */
int kosdec_adx_player_bind_aiff(struct kosdec_adx_player *, struct kosdec_adx_aiff *);
/* SPSD mono/stereo PCM16; same source lifetime and output rules. */
int kosdec_adx_player_bind_spsd(struct kosdec_adx_player *, struct kosdec_adx_spsd *);

/* Progress prefill/start or poll output; call regularly, more often than a sound
   ring duration. May block inside checked sound operations (transfer timeout).
   Short live input is silence-padded and counted as starvation. SOURCE_END keeps
   polling silence; caller chooses when to stop. No automatic tail-delay rule or
   playback clock. Failures latch first_error; caller must stop/retry stop. */
int kosdec_adx_player_poll(struct kosdec_adx_player *);
/* Admit one live attenuation request on a retained PCM lease. Requires the
   optional ADX_RETAINED_GAIN build, a backend supporting the lease geometry
   and supporting firmware. Uses the stock
   2.00J zero-fade response; effective request clamps to [-999, 0] tenths dB.
   Success only issues a request. Service the player and check the applied
   bits for the active channels. Mono uses the left bit alone.
   Maximum TL carries no hard-mute guarantee.
   Failure leaves the previous cached setting and the request output intact. */
int kosdec_adx_player_set_attenuation(struct kosdec_adx_player *,
    int32_t tenths_db, uint64_t *request);
/* Last locally admitted original setting, interpreted as a signed halfword.
   Starts at zero. Remains available through stop. */
int kosdec_adx_player_get_requested_attenuation(const struct kosdec_adx_player *,
    int16_t *tenths_db);
/* Compatibility-bound owner only. Cache the signed low halfword of the
   original request before attempting live admission. An inactive request
   (READY, PREFILL or STOPPED) updates the cache and returns request ID zero.
   With initial attenuation enabled, a request after compatibility DECINFO
   and before fresh lease admission stages the full signed argument.
   During playback, forward the full signed request to the existing
   optional retained-gain adapter.
   A live error leaves the request ID unchanged and preserves the new
   requested setting. Nonzero IDs use the normal acknowledgement query. */
int kosdec_adx_compat_player_set_attenuation(struct kosdec_adx_player *,
    int32_t tenths_db, uint64_t *request);
/* Requested setting of this compatibility title, independent of device
   admission. Starts at zero and persists through checked STOP. */
int kosdec_adx_compat_player_get_requested_attenuation(const struct kosdec_adx_player *,
    int16_t *tenths_db);
/* READY compatibility owner with retained clock selected. Enable exact stock
   initial device attenuation on the first new mono or stereo output lease.
   DECINFO stages the cached signed volume. A subsequent request before
   lease admission replaces that setting using its full signed argument.
   Requires ADX_RETAINED_START_TL and supporting firmware. There is no live
   request ID. If START fails after attempting hardware, complete checked STOP.
   Reusing an active output does not apply an initial setting again. */
int kosdec_adx_compat_player_enable_initial_attenuation(struct kosdec_adx_player *);
/* Latest admitted request only. Mono leaves the right-side masks and TL bytes
   zero. Acknowledged TL is historical on unknown channels.
   Poll success can coexist with a pending retry. Terminal failure
   requires checked stop. Channel acknowledgements do not specify simultaneity. */
int kosdec_adx_player_get_attenuation_status(const struct kosdec_adx_player *,
    uint64_t request, struct kosdec_adx_attenuation_status *out);
enum kosdec_adx_routing_result {
    KOSDEC_ADX_ROUTING_STAGED,
    KOSDEC_ADX_ROUTING_ADMITTED
};
/* Ordinary owner of a non-retained compatibility binding. Levels are
   whole dB, clamped to [-45,0]. Bus 16 preserves its legacy readback and
   uses device bus 0. Level-only changes preserve the configured bus.
   Success reports STAGED without live output or ADMITTED after command
   submission. A live submission error can leave one channel changed
   and requires checked STOP. Getters report the last requested settings. */
int kosdec_adx_compat_player_set_direct_level(struct kosdec_adx_player *, int32_t db);
int kosdec_adx_compat_player_get_direct_level(const struct kosdec_adx_player *, int32_t *db);
int kosdec_adx_compat_player_set_fx(struct kosdec_adx_player *, int32_t bus, int32_t db);
int kosdec_adx_compat_player_set_fx_level(struct kosdec_adx_player *, int32_t db);
int kosdec_adx_compat_player_get_fx(const struct kosdec_adx_player *, int32_t *bus, int32_t *db);
/* Requested transpose/detune in semitones/cents, clamped to +/-96 and
   +/-99 respectively. This cache does not submit a pitch change. */
int kosdec_adx_compat_player_cache_transpose(struct kosdec_adx_player *, int32_t semitones, int32_t cents);
int kosdec_adx_compat_player_get_transpose(const struct kosdec_adx_player *, int32_t *semitones, int32_t *cents);
/* Stage initial pitch on a READY compatibility player and cache
   the requested values. ERANGE preserves staging when the request exceeds
   the stock pitch-unit domain after clamping. Successful staging confirms a
   request, with the exact output rate resolved when the source format becomes
   known. Prefill fails with ENOTSUP before output allocation if the source
   rate is outside 8000..48000 Hz or the exact pitch word has no representative
   in 8000..88200 Hz for ordinary output or 8000..48000 Hz for retained output.
   Retained clock and transpose can be enabled in either order. An explicit
   nonzero native rate takes precedence.
   Source rate and requested output Hz remain separate in get_info.
   Live calls return ENOTSUP before changing the cache.
   Source cancellation before allocation clears staging. Same-format reuse
   of the output keeps its actual rate, and a replacement player starts fresh. */
int kosdec_adx_compat_player_set_initial_transpose(struct kosdec_adx_player *,
    int32_t semitones, int32_t cents);
/* Opt in after binding a READY compatibility title with a direct, unlinked
   ADX source. Requires the fork's ADX_ORDINARY_STEREO=1 build and a
   stereo PCM16 source/rate in 8000..48000 Hz. Sound storage is 8192..65504
   frames per channel, divisible by 32. This output is exclusive with the
   retained clock, supports one binding until checked stop and keeps its
   allocation after stop. Live pan, gain, routing and native rate setters
   are unavailable on this output. */
int kosdec_adx_compat_player_enable_stereo_transpose(struct kosdec_adx_player *);
/* Opt a READY compatibility title into checked ordinary mono PCM16 and
   nominal source time. Use a direct finite mono ADX source with the
   same ring geometry as checked stereo. */
int kosdec_adx_compat_player_enable_mono_clock(struct kosdec_adx_player *);
/* Request live transpose for a PLAYING ordinary compatibility title.
   Checked ordinary playback also accepts a decoded tail while its last qualified
   slot-zero cursor still has source PCM. Default mono success means host
   queue admission. With a checked ordinary output, success means every
   participating slot acknowledged its update. Stereo slots can apply separately.
   Checked ordinary live rates above 48000 Hz require at least 32768
   ring frames per channel. Above 88200 Hz use 65504. EAGAIN before
   posting preserves the admitted rate. Service and explicitly retry.
   An eligible player's requested cache updates even when conversion or
   submission fails. get_info reports the last admitted rate, and a hard
   backend failure requires checked STOP. The Flycast pause profile admits
   22,050 to 44,100 Hz in PLAYING with no pending or held cut. Following
   resume, the device keeps the admitted pitch and resets its phase.
   Retained playback is excluded. */
int kosdec_adx_compat_player_set_live_transpose(struct kosdec_adx_player *,
    int32_t semitones, int32_t cents);
/* Checked linear0..255 per-channel gain and pan (0left,128center,255right).
   May set before start or during playback. Default full volume; mono centered,
   stereo separated. Channel1 settings ignored for mono. This is KOS's scale,
   not CRI's attenuation/pan ABI. Failure may partially update hardware: stop. */
int kosdec_adx_player_set_output(struct kosdec_adx_player *,
    unsigned left_volume, unsigned right_volume, unsigned left_pan, unsigned right_pan);
/* Signed per-channel pan: -15 left,0 center,+15 right, or KOSDEC_ADX_PAN_AUTO.
   Defaults AUTO independently on both channels; resolves mono center/stereo
   separation on source admission and live updates. Channel1 may be configured
   before format discovery and is ignored by mono output. Only PAN is updated;
   volume/rate and the other channel's requested setting are preserved.
   Same serialized owner and checked-failure/stop rules as set_output. Invalid
   channel/value/state returns -1/EINVAL without changing requested state.
   Legacy set_output clears AUTO on both channels; get_pan then reports each
   linear pan's equivalent signed hardware position (directional zero is0). */
int kosdec_adx_player_set_pan(struct kosdec_adx_player *, unsigned channel, int pan);
/* Last successfully configured software setting, including AUTO; not device
   readback. Valid after failure/stop too. Null output or channel>1 is EINVAL. */
int kosdec_adx_player_get_pan(const struct kosdec_adx_player *, unsigned channel, int *pan);
/* Dreamcast compatibility settings. The 2.00J player stores these raw values
   without filtering the output. They start at zero and persist across sources
   and stop until reinit. This API is separate from the producer PCM callback. */
int kosdec_adx_player_set_filter_settings(struct kosdec_adx_player *,
    int32_t cutoff, int32_t resonance);
int kosdec_adx_player_get_filter_settings(const struct kosdec_adx_player *,
    struct kosdec_adx_filter_settings *);
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
bool kosdec_adx_player_take_receipt(struct kosdec_adx_player *,
                                   struct kosdec_adx_command_receipt *);
/* Snapshot owner counters plus synchronized producer status; no hardware query.
   Counters wrap modulo2^64 and count copies/requests, not consumed audio.
   source_frames counts the current (or last released) source; sources_started
   counts admitted sources. Neither is a hardware playback position. */
int kosdec_adx_player_get_info(struct kosdec_adx_player *, struct kosdec_adx_player_info *);
/* First operational failure observed since init/clear. Decoder/state are captured
   at observation, not live queries. Bad API arguments, WAIT, normal starvation,
   EOF and requested cancellation do not latch diagnostics. Query leaves output
   unchanged on invalid arguments. Native errno/status, not CRI numeric codes.
   Init failure has no initialized player to query: inspect its return/errno. */
int kosdec_adx_player_get_error(const struct kosdec_adx_player *,
    struct kosdec_adx_player_error *);
/* Acknowledge the diagnostic and discard pending notification only. Does NOT
   clear first_error/stop_error, resume playback, close/join users or retire any
   resources. A new observation (including stop's error check) can latch again.
   Repeated poll of an already FAILED player does not create a new observation. */
int kosdec_adx_player_clear_error(struct kosdec_adx_player *);
/* Register a per-player typed hook; NULL unregisters without clearing the latch.
   Registration does not notify or rearm an already delivered record. Context
   remains borrowed until replacement/unregister or player destruction after all
   users join. No process-global hook or SDK text-message ABI is supplied. */
int kosdec_adx_player_set_error_callback(struct kosdec_adx_player *,
    kosdec_adx_player_error_callback, void *context);
/* Explicit serialized owner-thread dispatch, e.g. after poll/stop. 1 delivered,
   0 no pending record or no callback, -1 invalid player. Once per latched record;
   get/clear are independent of delivery. Never implicitly called by sound or
   producer callbacks. Callback receives a temporary const snapshot, may copy it,
   must not retain its pointer, reenter player APIs, block/yield or change FPU
   modes. Successful dispatch preserves errno even if the callback changes it.
   Available through STOPPED; it neither frees nor reactivates the player. */
int kosdec_adx_player_dispatch_error(struct kosdec_adx_player *);
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
/* Begin preparing a fresh direct ADX stream on the same ordinary allocation.
   The caller must retain exclusive ownership of the stopped handle, ring and
   player following successful checked stop. For a single nonphased stereo
   compatibility title at the original source rate, without a rate override.
   START requires two full halves from the new producer. Zero admits source
   preparation. Drive the existing compatibility service to start playback.
   Any failure after the new binding is installed requires checked stop again
   before releasing the new source. Normal read/cancel lifetimes still apply.
   A new output starts a new clock interval. This provides no final-fetch or
   transfer-of-ownership guarantee. */
int kosdec_adx_compat_player_reprime(struct kosdec_adx_player *, struct kosdec_adx_stream *);
#ifdef __cplusplus
}
#endif
#endif
