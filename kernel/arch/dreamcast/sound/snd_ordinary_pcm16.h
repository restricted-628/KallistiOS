/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Private application-polled PCM16 with independently checked refills.
*/
#ifndef SND_ORDINARY_PCM16_H
#define SND_ORDINARY_PCM16_H

#include <dc/sound/stream.h>

typedef struct snd_ordinary_pcm16 snd_ordinary_pcm16_t;

/* Native PCM16, one sample per mono frame or interleaved left/right for
   stereo. Return frames filled. A short fill is padded with silence.
   The callback must not reenter this stream. */
typedef int (*snd_ordinary_pcm16_fill_t)(void *context, int16_t *pcm,
                                        uint32_t frames);

typedef struct snd_ordinary_pcm16_status {
    /* Slot 0 implements the sample-based title clock. Divide by nominal
       source Hz at the caller. Slot 1 is an independent renderer cursor
       for stereo and is zero for mono. Cursors include all output, even
       padding. The caller accounts for source and terminal drain. */
    uint64_t frames[2];
    uint64_t committed_frames;
    uint32_t rate;
    int error;
} snd_ordinary_pcm16_status_t;

/* Caller serializes all operations in thread context and coordinates the
   sound and G2 lifetimes. Let admitted SPU DMA legs complete normally.
   External suspension or cancellation of those legs is outside this
   contract. Join the producer and complete stop before resetting or
   shutting down sound or G2 DMA.

   Config must be mono or stereo PCM16 with pitch LFO off. The initial
   playback rate and ring must pass snd_ordinary_pcm16_rate_supported.
   The caller provides enough PCM and service cadence for the chosen rate. */
int snd_ordinary_pcm16_open(snd_ordinary_pcm16_t **out,
    const snd_stream_config_t *config, uint32_t ring,
    snd_ordinary_pcm16_fill_t fill, void *context);
int snd_ordinary_pcm16_start(snd_ordinary_pcm16_t *pcm);
int snd_ordinary_pcm16_poll(snd_ordinary_pcm16_t *pcm);
/* Initial and live native rate admission for mono or stereo. The range
   is 1889–186736 Hz. Above 48 kHz use at least 32768 frames per channel.
   Above 88.2 kHz use 65504. Cadence and producer capacity still govern
   refill and deadline success. */
bool snd_ordinary_pcm16_rate_supported(uint32_t rate, uint32_t ring);
/* When entering or leaving the extended range below 8 kHz or above
   88.2 kHz, EAGAIN means each active voice is still running at its previous
   rate and the deadline window needs more headroom. It submits no pitch
   writes or refills. Service the stream before retrying. Observation or
   transport failure is terminal even if its internal result was EAGAIN. */
int snd_ordinary_pcm16_set_rate(snd_ordinary_pcm16_t *pcm, uint32_t rate);
/* Set exact AICA TL bytes. Use left for mono and pass equal bytes for a
   title-wide level. Requires the negotiated observed TL feature.

   Before START, after a healthy replayable STOP, or at a confirmed Flycast
   pause cut, success stages the bytes for the next key-on. A live success
   confirms every active slot and its continuing cursor. Stereo writes are
   ordered per slot. EAGAIN defers before any update or refill; service the
   running stream before retrying. A failed live observation or transport
   transaction after admission leaves the lease owned for checked STOP. */
int snd_ordinary_pcm16_set_tl(snd_ordinary_pcm16_t *pcm,
    unsigned left_tl, unsigned right_tl);
int snd_ordinary_pcm16_status(const snd_ordinary_pcm16_t *pcm,
    snd_ordinary_pcm16_status_t *out);
/* Stop can be retried after a timeout while the owner keeps the runtimes
   live. Each DMA leg retires through its own completion callback. External
   cancellation can suppress that callback indefinitely. In that case stop
   remains incomplete and keeps the staging and reservation owned.

   A successful stop retires the producer and host staging. Attempted start
   pins channels and sound RAM. Close is available only before start. */
int snd_ordinary_pcm16_stop(snd_ordinary_pcm16_t *pcm, uint32_t timeout);
int snd_ordinary_pcm16_close(snd_ordinary_pcm16_t *pcm);

#endif
