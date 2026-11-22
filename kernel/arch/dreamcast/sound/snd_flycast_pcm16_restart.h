/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Private ordinary PCM16 replay for the opted-in Flycast mixer.
*/
#ifndef SND_FLYCAST_PCM16_RESTART_H
#define SND_FLYCAST_PCM16_RESTART_H
#include "snd_ordinary_pcm16.h"

/* Build the SH-4 sound code with AICA_FLYCAST_PCM16_RESTART, and use only
   on the qualified Flycast mixer with Timer B unmodified. The caller
   serializes the lease and joins its old producer before replay.

   After checked STOP of a healthy, played ordinary lease, replay on its
   existing PCM16 channels and ring. Both configurations need RR=31.
   The pause profile has its own re-prime operation. timeout_ms bounds
   release qualification; prefill and START keep their usual deadlines.

   A preflight failure leaves *attempted false and binds no fill callback.
   Once *attempted is true, failure requires checked STOP again and
   leaves the reservation owned. The previous rate and cursor remain
   reported on failure. Success adopts the new configuration and
   fresh START anchors. The API never releases played channels or ARAM. */
int snd_flycast_pcm16_restart(snd_ordinary_pcm16_t *pcm,
    const snd_stream_config_t *config, snd_ordinary_pcm16_fill_t fill,
    void *context, uint32_t timeout_ms, bool *attempted);

#endif
