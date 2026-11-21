#ifndef KOS_SOUND_RETAINED_MEMORY_H
#define KOS_SOUND_RETAINED_MEMORY_H

#include <stdbool.h>
#include "snd_retained_pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SND_RETAINED_MEMORY_GENERATION UINT64_C(1)

typedef struct snd_retained_memory_position {
    uint64_t submitted_frames; /* Committed source frames including repeats. */
    uint32_t next_frame;       /* Next source offset or size at finite EOF. */
} snd_retained_memory_position_t;

/* Open a fixed-format lease whose source will be bound before start. Use the
   ordinary retained start, poll, query, stop and close calls. The source
   generation is SND_RETAINED_MEMORY_GENERATION and finite sources are sealed
   when their last submitted frame commits. */
int snd_retained_pcm_open_memory(snd_retained_pcm_t **out, unsigned channels,
    uint32_t rate, uint32_t ring_frames);

/* Borrow immutable interleaved PCM16 until checked stop or unstarted close.
   Bind before any start attempt. Repeating sources must cover at least half
   the frame capacity of the sound ring. Failed bind retains the prior source. */
int snd_retained_pcm_memory_bind(snd_retained_pcm_t *pcm, const int16_t *data,
    uint32_t frames, bool repeat);

/* Snapshot the committed transfer position under the sole-owner contract.
   The position remains available after software stop. This query performs no
   I/O. For a live or bound source the owner can use next_frame to locate
   the next interleaved data. Failure leaves the output unchanged. */
int snd_retained_pcm_memory_position(const snd_retained_pcm_t *pcm,
    snd_retained_memory_position_t *position);

#ifdef __cplusplus
}
#endif
#endif
