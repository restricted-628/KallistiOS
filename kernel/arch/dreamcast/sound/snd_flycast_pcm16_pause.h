/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Private, build-gated Flycast stereo pause profile.
*/
#ifndef SND_FLYCAST_PCM16_PAUSE_H
#define SND_FLYCAST_PCM16_PAUSE_H
#include "snd_ordinary_pcm16.h"

#define SND_FLYCAST_PCM16_PAUSE_RING 16384u
#define SND_FLYCAST_PCM16_PAUSE_PREROLL 64u

/* One explicitly opted-in 44.1 kHz stereo PCM16 lease. Enable before
   start. The paired cut is supported during the current epoch's first
   traversal. The caller serializes service, pause, resume and stop. */
int snd_flycast_pcm16_pause_enable(snd_ordinary_pcm16_t *pcm);

/* Success freezes the pair at the same exclusive local output boundary.
   Status and cut use the current bank's local frames, including preroll
   after a resume. EAGAIN leaves playback running. ETIMEDOUT after a
   submission holds the request and ownership; retry pause or stop. */
int snd_flycast_pcm16_pause(snd_ordinary_pcm16_t *pcm, uint32_t timeout,
                           uint64_t *exclusive_cut);

/* Caller stages exactly RING-PREROLL interleaved frames, starting with the
   first survivor after discard. Both uploads and fresh START identities
   must qualify before this succeeds. The backend inserts PREROLL silence
   and does not call the producer during re-prime. The usual fill callback
   supplies the next frames after the accepted prefill. A hard failure
   keeps the reservation and requires checked stop. */
int snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *interleaved,
                            uint32_t frames);

#endif
