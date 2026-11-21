/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Private, build-gated Flycast PCM16 pause profile.
*/
#ifndef SND_FLYCAST_PCM16_PAUSE_H
#define SND_FLYCAST_PCM16_PAUSE_H
#include "snd_ordinary_pcm16.h"

#define SND_FLYCAST_PCM16_PAUSE_RING 16384u
#define SND_FLYCAST_PCM16_PAUSE_PREROLL 64u

/* Native start and live rates supported by the checked pause profile. */
bool snd_flycast_pcm16_pause_rate_supported(uint32_t rate);

/* One explicitly opted-in 22.05–44.1 kHz mono or stereo PCM16 lease.
   Enable before start. Rate changes use snd_ordinary_pcm16_set_rate while
   running. EAGAIN from that call leaves the rate intact; poll and retry.
   The caller serializes service, rate changes, pause, resume and stop. */
int snd_flycast_pcm16_pause_enable(snd_ordinary_pcm16_t *pcm);

/* Success freezes the active voices at one exclusive local output boundary.
   Status and cut count frames since the current bank's START, including
   preroll and every traversal. EAGAIN leaves playback running. ETIMEDOUT
   holds any selected cut and ownership; retry pause or stop. */
int snd_flycast_pcm16_pause(snd_ordinary_pcm16_t *pcm, uint32_t timeout,
                           uint64_t *exclusive_cut);

/* Caller stages exactly RING-PREROLL PCM16 frames, starting with the
   first survivor after discard. Mono is contiguous and stereo is interleaved.
   Both half-ring uploads and all active START identities must qualify. The backend inserts PREROLL silence
   and does not call the producer during re-prime. The usual fill callback
   supplies the next frames after the accepted prefill. START uses the last
   admitted pitch and begins a new interpolator phase. A hard failure
   keeps the reservation and requires checked stop. */
int snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *samples,
                            uint32_t frames);

#endif
