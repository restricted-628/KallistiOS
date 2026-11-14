/* KallistiOS ##version##

   snd_iface_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __SND_IFACE_INTERNAL_H
#define __SND_IFACE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <dc/sound/sound.h>
#include <dc/sound/observation.h>
#include <dc/sound/aica_completion.h>

bool snd_iface_is_initialized(void);
int snd_iface_exclusive_begin(uint32_t timeout_ms);
void snd_iface_exclusive_end(void);

void snd_dsp_system_reset(void);
void snd_dsp_system_shutdown(void);

/* Audio-private observation bridges. Caller observes exclusive sound lifecycle.
   Absolute deadlines bound software waits, not wedged hardware/G2 accesses. */
void snd_observation_load_begin(void);
void snd_observation_reset(void);
uint32_t snd_iface_observation_features(void);
int snd_iface_observation_submit(void *packet, uint32_t words, uint64_t deadline);
int snd_iface_observation_read(uint32_t offset, void *record, uint32_t bytes);
int snd_iface_observation_validate(uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags);
void snd_iface_observation_pack(void *destination,
    const snd_channel_config_t *config);

/* Private wrapper entry points share one caller-owned absolute deadline. */
int snd_observation_session(uint32_t *session);
int snd_observation_bind_until(uint64_t deadline, uint32_t *session);
int snd_observation_submit_until(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt, uint64_t deadline);
/* Only callers that just validated the unchanged config/fields/flags may use
   this entry. Playback validates before priority decline, without repeating
   that same SH-4 pass at submission; ARM still validates its copied packet. */
int snd_observation_submit_validated_until(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t flags,
    snd_channel_receipt_t *receipt, uint64_t deadline);
int snd_observation_poll_until(const snd_channel_receipt_t *receipt,
    uint64_t deadline, snd_channel_observation_t *observation);
int snd_observation_consume_until(const snd_channel_receipt_t *receipt,
    uint64_t deadline);
int snd_observation_completion_until(unsigned channel, uint32_t expected_session,
    uint64_t deadline, aica_completion_channel_t *state,
    uint64_t *started, uint64_t *finished);
void snd_playback_reset(void);
#endif /* __SND_IFACE_INTERNAL_H */
