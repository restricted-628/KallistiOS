#ifndef KOS_AUDIO_ADX_RETAINED_CLOCK_INTERFACE_H
#define KOS_AUDIO_ADX_RETAINED_CLOCK_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct snd_retained_pcm snd_retained_pcm_t;

enum snd_retained_pcm_kind {
    SND_RETAINED_PCM_SOURCE,
    SND_RETAINED_PCM_STARVATION,
    SND_RETAINED_PCM_PAUSE_SILENCE,
    SND_RETAINED_PCM_TERMINAL_SILENCE
};

typedef struct snd_retained_pcm_span {
    uint64_t generation;
    uint64_t first_frame;
    uint32_t frames;
    enum snd_retained_pcm_kind kind;
} snd_retained_pcm_span_t;

#define SND_RETAINED_PCM_FILL_SPANS 16u
#define SND_RETAINED_PCM_GENERATIONS 16u

/* Called synchronously by start/poll on the sole ordinary owner thread. Fill
   exactly the requested interleaved PCM16 frames and describe them in order.
   Return 1..capacity spans or -1 with errno. Audio writes zeros for all silence
   spans (generation/first_frame both zero). Source generations are nonzero and
   introduced in strictly increasing order, with at most GENERATIONS distinct
   generations per lease. First_frame is the source offset, which may rewind
   for a loop. Split at source/offset/kind boundaries. Audio reserves capacity
   before calling. Pointers are valid only during the call. No reentrant calls. */
typedef int (*snd_retained_pcm_fill_t)(void *context, int16_t *pcm,
    uint32_t frames, snd_retained_pcm_span_t *spans, unsigned capacity);

typedef struct snd_retained_pcm_snapshot {
    uint64_t epoch;
    uint64_t output_frames;
    uint64_t generation;
    uint64_t source_frames;
    uint64_t next_source_frame;
    uint64_t observation_start_ms, observation_end_ms;
    uint32_t session;
    int clock_error;
    int first_error;
    int stop_error;
    unsigned valid;
    unsigned source_output_end;
    unsigned software_stopped;
} snd_retained_pcm_snapshot_t;

/* Open a single fixed-format stream lease. Ring_frames is per channel, a
   multiple of 32, 1024..32768. Rate is 8000..48000 Hz requested AICA pitch.
   Requires an initialized stream runtime and observation-capable firmware.
   Caller serializes all calls and excludes direct channel/stream access and
   global sound teardown while the lease exists. Return 0 or -1 with errno.
   Failed open leaves *out unchanged. */
int snd_retained_pcm_open(snd_retained_pcm_t **out, unsigned channels,
    uint32_t rate, uint32_t ring_frames, snd_retained_pcm_fill_t fill, void *context);

/* One attempt per lease. Prefill synchronously, then start. Even failed start
   may retain hardware/DMA dependencies. Call stop after any start attempt.
   Poll must service at sub-ring cadence and may block in sound operations.
   Poll returns 0 on service (including no refill), -1 on failure. EAGAIN can
   report backpressure before invoking fill. Fatal errors latch and need stop. */
int snd_retained_pcm_start(snd_retained_pcm_t *pcm);
int snd_retained_pcm_poll(snd_retained_pcm_t *pcm);

/* Call outside fill after the last source span was accepted by a successful
   start/poll, or before/after any successful fill for an empty source. Total
   counts delivered-source frames including repeats. Once-only, ordered seal of
   that generation. Further source spans for it are invalid. */
int snd_retained_pcm_seal(snd_retained_pcm_t *pcm, uint64_t generation,
    uint64_t total_source_frames);

/* Snapshot only, with no I/O. Poll advances the qualified cursor. Generation
   zero requests output-only; an unknown nonzero generation gives ENOENT.
   valid==0 exposes the last qualified counters and a positive errno reason.
   End means source reached the qualified output cursor on every channel.
   Audible end, device pause/resume, strong discard and physical retirement
   are unsupported. Negative returns leave *out unchanged. */
int snd_retained_pcm_query(const snd_retained_pcm_t *pcm, uint64_t generation,
    snd_retained_pcm_snapshot_t *out);

/* Stop closes admission and never invokes fill again. Success retires software
   dependencies only. Timeout/failure preserves the lease for stop retry.
   Never use global reset or restart to recover one of these leases. Close can
   reclaim a lease only before any start attempt. After start, close returns
   EBUSY, including after successful stop. Context can be released after stop. */
int snd_retained_pcm_stop(snd_retained_pcm_t *pcm, uint32_t timeout_ms);
int snd_retained_pcm_close(snd_retained_pcm_t *pcm);

#ifdef __cplusplus
}
#endif
#endif
