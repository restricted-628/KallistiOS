/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Private application-polled stereo PCM16 with independently checked refills.
*/
#ifndef SND_ORDINARY_PCM16_H
#define SND_ORDINARY_PCM16_H

#include <dc/sound/stream.h>

typedef struct snd_ordinary_pcm16 snd_ordinary_pcm16_t;

/* Interleaved PCM; return the number of frames filled. A short fill is padded
   with silence. The callback must not reenter this stream. */
typedef int (*snd_ordinary_pcm16_fill_t)(void *context, int16_t *pcm,
                                        uint32_t frames);

typedef struct snd_ordinary_pcm16_status {
    /* Slot 0 implements the sample-based title clock. Divide by nominal
       source Hz at the caller. Slot 1 is an independent renderer cursor. */
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

   Config must be stereo PCM16 at 8–48 kHz with pitch LFO off. Ring frames
   per channel must be 8192–32768 and divisible by 32. */
int snd_ordinary_pcm16_open(snd_ordinary_pcm16_t **out,
    const snd_stream_config_t *config, uint32_t ring,
    snd_ordinary_pcm16_fill_t fill, void *context);
int snd_ordinary_pcm16_start(snd_ordinary_pcm16_t *pcm);
int snd_ordinary_pcm16_poll(snd_ordinary_pcm16_t *pcm);
/* Live native rate admission for this ring geometry. Above 48 kHz,
   the full 32768-frame ring is required and the maximum is 88.2 kHz.
   Cadence and producer capacity still govern refill and deadline success. */
bool snd_ordinary_pcm16_rate_supported(uint32_t rate, uint32_t ring);
int snd_ordinary_pcm16_set_rate(snd_ordinary_pcm16_t *pcm, uint32_t rate);
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
