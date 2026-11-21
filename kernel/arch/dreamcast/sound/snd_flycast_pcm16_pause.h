/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Private, build-gated Flycast PCM16 pause profile.
*/
#ifndef SND_FLYCAST_PCM16_PAUSE_H
#define SND_FLYCAST_PCM16_PAUSE_H
#include "snd_ordinary_pcm16.h"

#define SND_FLYCAST_PCM16_PAUSE_RING 16384u
#define SND_FLYCAST_PCM16_PAUSE_PREROLL 64u

/* Native rates supported by the legacy 16,384-frame profile. */
bool snd_flycast_pcm16_pause_rate_supported(uint32_t rate);

/* The 32,768- and 65,504-frame profiles use ordinary rate admission.
   The start rate also needs to qualify for ordinary PCM16 startup. */
bool snd_flycast_pcm16_pause_geometry_supported(uint32_t rate, uint32_t ring);

/* Silent input frames for a fresh START at rate, or zero for invalid rate.
   Above unity this aligns the first survivor to an exact mixer phase. */
uint32_t snd_flycast_pcm16_resume_preroll(uint32_t rate);

/* One explicitly opted-in mono or stereo PCM16 lease.
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

/* Replace the pitch staged for the next restart of a confirmed cut.
   Success performs no device work and leaves the admitted rate unchanged.
   ENOTSUP for an unsupported rate leaves any previous staging intact. */
int snd_flycast_pcm16_stage_resume_rate(snd_ordinary_pcm16_t *pcm, uint32_t rate);

/* Caller stages exactly ring minus the target rate's preroll frames, starting with the
   first survivor after discard. Mono is contiguous and stereo is interleaved.
   Both half-ring uploads and all active START identities must qualify.
   The backend inserts the silent preroll and does not call the producer during
   re-prime. The usual fill callback supplies frames after prefill. START
   uses the staged pitch, or the last admitted pitch if none is staged, and
   begins a new interpolator phase. A hard failure requires checked stop. */
int snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *samples,
                            uint32_t frames);

#endif
