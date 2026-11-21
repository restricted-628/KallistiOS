/* Optional owner-serviced ADX pacing for applications with synchronous phases. */
#ifndef KOSDEC_ADX_COMPAT_SERVICE_H
#define KOSDEC_ADX_COMPAT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOSDEC_ADX_COMPAT_MAX_TITLES 32u

enum kosdec_adx_compat_phase {
    KOSDEC_ADX_COMPAT_DECODER,
    KOSDEC_ADX_COMPAT_RENDERER,
    KOSDEC_ADX_COMPAT_FILES,
    KOSDEC_ADX_COMPAT_STREAMS,
    KOSDEC_ADX_COMPAT_RELOAD,
    KOSDEC_ADX_COMPAT_PHASE_COUNT
};

enum kosdec_adx_compat_state {
    KOSDEC_ADX_COMPAT_STOP,
    KOSDEC_ADX_COMPAT_DECINFO,
    KOSDEC_ADX_COMPAT_PREP,
    KOSDEC_ADX_COMPAT_PLAYING,
    KOSDEC_ADX_COMPAT_DECEND,
    KOSDEC_ADX_COMPAT_PLAYEND
};

enum kosdec_adx_compat_error {
    KOSDEC_ADX_COMPAT_NO_ERROR,
    KOSDEC_ADX_COMPAT_LOW_DATA = -1,
    KOSDEC_ADX_COMPAT_STALLED = -2
};

enum kosdec_adx_compat_fault {
    KOSDEC_ADX_COMPAT_FAULT_LOW_DATA = 1u << 0,
    KOSDEC_ADX_COMPAT_FAULT_STALLED = 1u << 1,
    KOSDEC_ADX_COMPAT_FAULT_TRANSPORT = 1u << 2
};

struct kosdec_adx_player;
struct kosdec_adx_recovery;

/* Mark the next binding final on a retained-clock compatibility player.
   Call before first start or after releasing a sealed source, while IDLE.
   With one occupied title, owner service requires natural decoder DONE and
   a qualified sealed drain for every generation on the lease before checked
   stop/join can publish PLAYEND. Release, cancellation and explicit STOP
   disarm it. Timeout preserves STOPPING and retries the same decision.
   PLAYEND does not authorize output resource reuse. */
int kosdec_adx_compat_player_enable_playend(struct kosdec_adx_player *, uint32_t stop_timeout_ms);
/* Owner-only observation. A PLAYING dispatch may publish DECEND after the
   final samples are decoded with under 32 bytes of PCM16 pending per channel.
   Output still submits the remaining frames. Native SOURCE_END also waits
   for source validation and full PCM submission. A phased direct stream with
   an unrecognized header can retain DECINFO after failure. Check the service
   result and kosdec_adx_player_get_error for the failure before waiting again. */
int kosdec_adx_compat_player_get_state(const struct kosdec_adx_player *, enum kosdec_adx_compat_state *);
int kosdec_adx_compat_player_is_completed(const struct kosdec_adx_player *, bool *);

struct kosdec_adx_compat_observation {
    uint32_t decoded_position;  /* Cumulative decoded samples, across loop rewinds. */
    uint32_t input_bytes;       /* Staged encoded or PCM DATA bytes. */
    bool decoder_done, paused;
};

struct kosdec_adx_compat_title_ops {
    enum kosdec_adx_compat_state (*state)(void *);
    int (*dispatch)(void *, enum kosdec_adx_compat_state);
    /* 0 observed, positive defers the sample, negative sets errno. */
    int (*observe)(void *, struct kosdec_adx_compat_observation *);
    /* Optional, side-effect-free snapshot of the current input transport. */
    bool (*transport_failed)(void *);
};

struct kosdec_adx_compat_phase_op {
    int (*service)(void *);
    void *context;
};

struct kosdec_adx_compat_config {
    uint64_t (*clock_us)(void *);
    void *clock_context;
    struct kosdec_adx_compat_phase_op phases[KOSDEC_ADX_COMPAT_PHASE_COUNT];
};

/* Caller-supplied storage. Change fields only through the functions below. */
struct kosdec_adx_compat_title {
    struct kosdec_adx_compat_title_ops ops;
    void *context;
    int (*detach)(void *); /* Private native binding hook. */
    int (*service_phase)(void *, enum kosdec_adx_compat_phase); /* Private. */
    struct kosdec_adx_recovery *recovery; /* Private same-call binding. */
    int (*recover)(struct kosdec_adx_recovery *, bool admitted); /* Private. */
    uint32_t position, frequency, faults;
    uint16_t low_checks, stalled_checks;
    enum kosdec_adx_compat_error error;
};

struct kosdec_adx_compat_service {
    struct kosdec_adx_compat_config config;
    struct kosdec_adx_compat_title *titles;
    uint64_t last_check_us;
    uint32_t vsync_count;
    unsigned capacity;
    bool servicing;
    bool use_vsync_clock;
};

struct kosdec_adx_compat_result {
    uint32_t visited, watchdog_checks, watchdog_deferred, failed_titles;
    uint32_t serviced_phases, failed_phases;          /* Phase bit masks. */
    int first_error;                                 /* Callback or service errno. */
};

/* Zero-based clock in microseconds, monotonic over this collection's lifetime.
   One ordinary owner serializes the collection and its callbacks. No IRQ,
   thread, audio or fiber registration. Init on fresh storage or after all
   slots are unbound. Slots and callbacks remain valid until unbound.
   A supplied callback follows any native player operations for its phase.
   An absent phase runs no work. A callback returns 0 only after its declared
   synchronous work, or -1 with errno. Waking a fiber does not service a phase.
   Do not dispatch the same title from another scheduler. */
int kosdec_adx_compat_service_init(struct kosdec_adx_compat_service *,
    struct kosdec_adx_compat_title *, unsigned capacity,
    const struct kosdec_adx_compat_config *);
/* Select this collection's delivered VBlank count for bound players'
   compatibility clock. Enable on an initialized collection before binding
   titles. The native clock can still use its own supplied tick reader. */
int kosdec_adx_compat_enable_vsync_clock(struct kosdec_adx_compat_service *);
int kosdec_adx_compat_bind(struct kosdec_adx_compat_service *, unsigned slot,
    const struct kosdec_adx_compat_title_ops *, void *context);
int kosdec_adx_compat_unbind(struct kosdec_adx_compat_service *, unsigned slot);

/* Bind a READY, initialized ADX player before start. The collection alone
   polls this player until unbind. Use the existing player start, stop and
   source rebind calls on the ordinary owner outside collection dispatch.
   Detach only while READY or after checked STOP. The player, collection and
   slots must outlive the binding. No completion beyond SOURCE_END is inferred. */
int kosdec_adx_compat_bind_player(struct kosdec_adx_compat_service *, unsigned slot,
                                 struct kosdec_adx_player *);

/* Opt into synchronous collection service for finite read-at ADX, WAVE, AIFF,
   AU and SPSD sources, ADX sequences, or batched ADX joints backed by sealed MEMORY,
   a sector provider or a linked loader. Bind before start. Rebind and
   recovery require a supported phased source. One ordinary owner drives
   all phases, controls and playback. Finite read-at staging runs in FILE.
   Sector providers and linked loaders run in STREAM. DECODER consumes already
   published DATA and handles resident MEMORY without a transport phase.
   Providers, filters and phase callbacks must not reenter playback.
   Direct title calls use the previous phase results. Checked stop releases
   player ownership; the caller still closes the reader, loader and provider.
   Rebuild consumers against the matching headers and archive. */
int kosdec_adx_compat_bind_phased_player(struct kosdec_adx_compat_service *, unsigned slot,
                                        struct kosdec_adx_player *);

/* Optional prepared-start latch for a compatibility-bound native player.
   PREP sets it even if a hold or native sound priming delays playback.
   It persists across ownership stop, cancel, release and failed output.
   The next admitted source start clears it. Unbinding ends this snapshot.
   The owner may query without running a phase. Invalid or unbound calls
   leave ready unchanged and return -1 with EINVAL or ENOTSUP. */
int kosdec_adx_compat_player_is_ready_start(const struct kosdec_adx_player *, bool *ready);

/* Default 60 Hz per bound title. Changes affect the next admitted check.
   A prepared title keeps its existing target until its next format validation. */
int kosdec_adx_compat_set_frequency(struct kosdec_adx_compat_service *,
    unsigned slot, int32_t hz);

/* At format validation compute the legacy PCM frame target at the current
   frequency. A synchronous adapter uses the result to set its preparation
   threshold. This query is safe from within the title dispatch callback. */
int kosdec_adx_compat_prefill(const struct kosdec_adx_compat_service *,
    unsigned slot, uint32_t rate, uint32_t block_frames, bool has_loop,
    uint32_t *frames);

/* Service decoder/renderer/files/streams, bound slots in index order, then
   reload. Direct title service visits one bound slot with the same cadence
   latch. A slot dispatches its entry state once (STOP has no handler), then
   checks cadence before reading its latest state and decoder observation.
   Errors in a callback are recorded and the collection continues. Watchdog
   conditions are reported by the separate cached error query. */
int kosdec_adx_compat_exec_server(struct kosdec_adx_compat_service *,
    struct kosdec_adx_compat_result *);
/* Deliver one VBlank event on the ordinary owner. Increment the wrapping
   collection count before the usual server pass, including on a pass that
   reports a callback failure. Invalid or reentrant calls leave it intact.
   Other service entries leave it intact. Deliver events outside IRQ; this
   call runs the normal phases synchronously. It owns no display callback. */
int kosdec_adx_compat_exec_vsync(struct kosdec_adx_compat_service *,
    struct kosdec_adx_compat_result *);
/* Owner observation, also allowed inside the collection's callbacks. */
int kosdec_adx_compat_get_vsync_count(const struct kosdec_adx_compat_service *, uint32_t *count);
int kosdec_adx_compat_exec_title(struct kosdec_adx_compat_service *,
    unsigned slot, struct kosdec_adx_compat_result *);

int kosdec_adx_compat_get_error(const struct kosdec_adx_compat_service *,
    unsigned slot, enum kosdec_adx_compat_error *);
int kosdec_adx_compat_clear_error(struct kosdec_adx_compat_service *, unsigned slot);
/* Reasons observed on the latest admitted check. Several can coincide.
   Transport failure is independent of the signed watchdog error. On a
   deferred decoder sample only the independently observed transport can
   be reported. A phased FILE read failure may be visible while valid
   bytes remain staged and the decoder is still running. Clearing the
   error resets both snapshots and counts. */
int kosdec_adx_compat_get_faults(const struct kosdec_adx_compat_service *,
    unsigned slot, uint32_t *faults);

#ifdef __cplusplus
}
#endif
#endif
