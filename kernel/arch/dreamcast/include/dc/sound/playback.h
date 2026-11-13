/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
/** \file dc/sound/playback.h
    \brief Experimental cooperative mono playback with caller-owned samples.

    Samples must already reside in sound RAM. Their external owner must retain
    every possibly referenced region before START and after accepted replacement,
    STOP, ACK, detach and close. This API never frees samples or establishes when
    AICA has finished fetching them. Known refusal/rejection removes only that
    attempt's prospective reference, not any prior or other user's references.

    EXPERIMENTAL: capable firmware latches a finite PCM16 LP end observation;
    a fresh matching observation may reset software priority. Physical attribution
    across replacement and selector timing remain unqualified. This is not a
    sample-reuse guarantee. Other formats, loops and delayed starts are ineligible.
    A lost firmware session pins the attachment/channel capacity until runtime restart; no abandon
    or automatic recovery is supplied. Normally observe STOP, consume its result,
    and detach before exclusive sound shutdown. This releases software ownership
    only, not sample retention. These limitations are not full Manatee parity.

    Normal thread context and cooperative ownership are required. Raw/P1a/SFX
    writers must not independently use an attached channel. Interference is known
    only when freshly observed; detach uses the last observed STOP and does not
    continuously monitor hardware. Lifecycle calls remain exclusive with APIs.
*/
#ifndef __DC_SOUND_PLAYBACK_H
#define __DC_SOUND_PLAYBACK_H
#include <dc/sound/port.h>
#include <dc/sound/observation.h>
__BEGIN_DECLS

#define SND_PLAYBACK_TIMEOUT_MS 100u

typedef struct snd_port_operation {
    uint32_t port, session, token;
} snd_port_operation_t;

/** Attach a live mono reservation atomically relative to close. No hardware
    action or initial-silence guarantee. EBUSY already attached/pending; ENOTSUP
    pair/firmware; EINVAL invalid handle; ENODEV missing/lost firmware; EOVERFLOW
    identity exhausted. Attach/detach/submission each share one finite 100ms
    software lock/queue budget. IRQ context gives EPERM; timeout ETIMEDOUT. */
int snd_port_playback_attach(snd_port_t port);

/** START returns 1 admitted, 0 priority declined, -1 error. Priority 0..15 follows
    inactive/either-zero/equal-or-higher admission. Output changes only after
    queue admission. Admission is not ARM execution or audible playback.
    One outstanding operation per attachment; further commands give EBUSY.
    Retain old and proposed samples on admitted-but-unobserved operations. */
int snd_port_playback_start(snd_port_t port, const snd_channel_config_t *config,
    unsigned priority, snd_port_operation_t *operation);

/** UPDATE/STOP return 0 admitted, -1 error. UPDATE needs a previously applied
    owned configuration; it cannot change sample geometry. STOP may explicitly
    recover observed interference after the previous result is consumed. */
int snd_port_playback_update(snd_port_t port, const snd_channel_config_t *config,
    uint32_t fields, snd_port_operation_t *operation);
int snd_port_playback_stop(snd_port_t port, snd_port_operation_t *operation);

/** Nonzero timeout required (zero EINVAL). One overall deadline includes all
    nested locks/submission. Return 0 with classified ARM outcome and fresh sample,
    or -1 with output unchanged. Commit historical policy exactly once; compare
    attribution on every successful poll. Timeout neither cancels nor rolls back.
    A later legacy mutation can make an APPLIED result historical only. */
int snd_port_playback_poll(const snd_port_operation_t *operation,
    uint32_t timeout_ms, snd_channel_observation_t *observation);

/** Consume an already observed result using explicit ACK. Nonzero timeout.
    Failure retains identity; retry after timeout. Poll while ACK pending gives
    EBUSY. Success releases only result bookkeeping and invalidates this operation
    handle. It does not release samples or the channel attachment. */
int snd_port_playback_consume(const snd_port_operation_t *operation,
    uint32_t timeout_ms);

#define SND_DRIVER_FEATURE_COMPLETION 0x00000020u
#define SND_PLAYBACK_EMPTY 0u
#define SND_PLAYBACK_ACTIVE 1u
#define SND_PLAYBACK_STOPPED 2u
#define SND_PLAYBACK_INTERFERED 3u
#define SND_PLAYBACK_END_ELIGIBLE 1u
#define SND_PLAYBACK_END_SEEN 2u

typedef struct snd_port_playback_state {
    uint32_t port, session, channel, start_token;
    uint32_t command_token, state, flags, first_end_tick;
    uint32_t position, key_on, observation_tick;
    uint64_t host_challenge_started_ms, host_observed_ms;
} snd_port_playback_state_t;

/** Experimental current-generation query, independent of consumed command ACK.
    Returns 0, or -1 with output unchanged. EAGAIN until this attachment owns a
    classified APPLIED START (never adopts a prior port's channel history).
    EBUSY outstanding receipt or foreign playback/command identity; interference
    remains until explicit STOP recovery. Matching START with legacy INTERFERED
    may return that observation, with no priority reset. ENOTSUP old firmware;
    ENODEV lost session; EPROTO malformed records; EOVERFLOW exhausted publication;
    EINVAL invalid port/output/zero timeout; EPERM IRQ; ETIMEDOUT deadline.
    One deadline bounds software waits, not a wedged bus. END_SEEN persists through
    UPDATE/STOP/ACK until next APPLIED START; it is not an event history. Tick0 is
    valid. A matching active end resets priority once, without permitting detach,
    releasing ownership or samples. START checks the same fresh state before
    priority admission on capable firmware. All sample retention rules remain. */
int snd_port_playback_get_state(snd_port_t port, uint32_t timeout_ms,
                                snd_port_playback_state_t *state);

/** Detach only after no applied START, or last-observed APPLIED STOP, and no
    outstanding operation. EBUSY on active/interfered/pending state; ENODEV on
    lost session. Failure preserves ownership. Success permits snd_port_close,
    which returns the software reservation only; sample retention continues. */
int snd_port_playback_detach(snd_port_t port);
__END_DECLS
#endif
