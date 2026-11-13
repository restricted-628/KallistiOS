/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Checked command processing and fresh observation; no sample retirement. */
#ifndef __DC_SOUND_OBSERVATION_H
#define __DC_SOUND_OBSERVATION_H
#include <stdint.h>
#include <sys/cdefs.h>
#include <dc/sound/sound.h>
__BEGIN_DECLS

#define SND_OBSERVATION_SUBMIT_TIMEOUT_MS 100u
#define SND_DRIVER_FEATURE_OBSERVATION 0x00000010u
#define SND_OBSERVATION_START  1u
#define SND_OBSERVATION_STOP   2u
#define SND_OBSERVATION_UPDATE 3u
#define SND_OBSERVATION_APPLIED       1u
#define SND_OBSERVATION_BAD_OPERATION 2u
#define SND_OBSERVATION_BAD_CONFIG    3u
#define SND_OBSERVATION_BAD_FLAGS     4u
#define SND_OBSERVATION_UNCONFIGURED  5u
#define SND_OBSERVATION_STALE_TOKEN   6u
#define SND_OBSERVATION_ATTR_NONE     0u
#define SND_OBSERVATION_ATTR_MANAGED  1u
#define SND_OBSERVATION_ATTR_LEGACY   2u

typedef struct snd_channel_receipt {
    uint32_t session, token, channel;
} snd_channel_receipt_t;

typedef struct snd_channel_observation {
    uint32_t session, token, channel, operation, result;
    uint32_t attribution_token, attribution_kind;
    uint32_t position, key_on, processing_tick, observation_tick;
    uint64_t host_challenge_started_ms, host_observed_ms;
} snd_channel_observation_t;

/* Thread context; cooperative exclusive snd_init/shutdown lifecycle.
   Submit uses one finite 100ms software-wait budget, including lazy session
   binding and all mutex waits. This is not a hard hardware/G2 latency bound.
   One outstanding receipt per channel. START/UPDATE use existing configuration
   rules; STOP requires NULL config and zero fields/flags. Output changes only
   after successful admission. Success is queue admission, not ARM execution.
   Errors: EINVAL, ENODEV, ENOTSUP, EBUSY, EOVERFLOW, or queue admission errno.
   Generation/cookie exhaustion never wraps; pending receipts remain owned. */
int snd_channel_observed_submit(unsigned channel, uint32_t operation,
    const snd_channel_config_t *config, uint32_t fields, uint32_t start_flags,
    snd_channel_receipt_t *receipt);

/* Nonzero timeout_ms is required (zero gives EINVAL). Its one software-wait
   deadline includes all mutex waits and queue admission. Bounded challenge and
   coherent result read. Success returns the ARM result
   (including rejection) plus an observation made after this call's challenge.
   Host start/end bound its age; ARM ticks are informational, not a source clock.
   Timeout does not cancel/consume the command. Output unchanged on error.
   Also returns EAGAIN for unstable publication, ENODEV for lost session,
   EOVERFLOW for exhausted identity/publication, ETIMEDOUT for no fresh reply.
   Poll while ACK is pending gives EBUSY; retry consume to resolve it.
   Attribution is subsequent cooperative ownership metadata: legacy invalidation
   preserves the sampled position/key/challenge/tick tuple. It does not claim
   that sample was taken after the legacy mutation. A fresh challenge still
   reads hardware, and its host interval bounds the returned sample age.
   Neither APPLIED nor key_on==0 proves final sample fetch or safe reuse. */
int snd_channel_observed_poll(const snd_channel_receipt_t *receipt,
    uint32_t timeout_ms, snd_channel_observation_t *observation);

/* Nonzero timeout_ms is required (zero gives EINVAL). Explicitly consume the retained result; succeed only after matching ARM ACK.
   This releases result bookkeeping only. It neither stops sound nor releases
   channel/sample ownership. Timeout retains local receipt and may be retried.
   Receipt must first have a successfully polled result; otherwise EBUSY.
   Output receipt is not modified; later calls reject consumed/stale receipts. */
int snd_channel_observed_consume(const snd_channel_receipt_t *receipt,
    uint32_t timeout_ms);
__END_DECLS
#endif
