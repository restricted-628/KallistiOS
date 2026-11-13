/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Optional checked-operation observation wire ABI. */
#ifndef __DC_SOUND_AICA_OBSERVATION_H
#define __DC_SOUND_AICA_OBSERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <dc/sound/aica_comm.h>

#define AICA_OBSERVATION_LAYOUT_VERSION 1u
#define AICA_DRIVER_FEATURE_OBSERVATION 0x00000010u
#define AICA_CMD_OBSERVATION_BIND       0x00000007u
#define AICA_CMD_OBSERVATION_CONTROL    0x00000008u
#define AICA_CMD_OBSERVATION_SAMPLE     0x00000009u
#define AICA_CMD_OBSERVATION_CONSUME    0x0000000au

#define AICA_MEM_OBSERVATION_HEADER     0x025000u
#define AICA_MEM_OBSERVATION_RESULTS    0x025100u
#define AICA_MEM_OBSERVATION_CHANNELS   0x026100u
#define AICA_MEM_OBSERVATION_END        0x026900u
#define AICA_OBSERVATION_SLOTS          64u

#define AICA_OBSERVATION_BIND_OK        1u
#define AICA_OBSERVATION_BIND_CONFLICT  2u
#define AICA_OBSERVATION_EMPTY         0u
#define AICA_OBSERVATION_APPLIED       1u
#define AICA_OBSERVATION_BAD_OPERATION 2u
#define AICA_OBSERVATION_BAD_CONFIG    3u
#define AICA_OBSERVATION_BAD_FLAGS     4u
#define AICA_OBSERVATION_UNCONFIGURED  5u
#define AICA_OBSERVATION_STALE_TOKEN   6u
#define AICA_OBSERVATION_ACK_OK        1u
#define AICA_OBSERVATION_ACK_MISMATCH  2u
#define AICA_OBSERVATION_ATTR_NONE    0u
#define AICA_OBSERVATION_ATTR_MANAGED 1u
#define AICA_OBSERVATION_ATTR_LEGACY  2u

/* Diagnostic codes are not a reliable per-request completion queue. */
#define AICA_OBSERVATION_DIAG_NONE        0u
#define AICA_OBSERVATION_DIAG_PACKET      1u
#define AICA_OBSERVATION_DIAG_SESSION     2u
#define AICA_OBSERVATION_DIAG_SLOT        3u
#define AICA_OBSERVATION_DIAG_BUSY        4u
#define AICA_OBSERVATION_DIAG_EXHAUSTED   5u
#define AICA_OBSERVATION_DIAG_CHANNEL     6u

/* All fields are little-endian 32-bit words. ARM is sole shared-record writer.
   sequence is odd during publication; UINT32_MAX means permanently exhausted.
   Reserved fields are zero. Existing layouts/regions are not modified. */
typedef struct aica_observation_header {
    uint32_t sequence, layout_version, ready, active_session;
    uint32_t bind_session, bind_cookie, bind_result, observation_tick;
    uint32_t last_protocol_error, protocol_error_count, flags, reserved[5];
} aica_observation_header_t;

typedef struct aica_observation_result {
    uint32_t sequence, session, token, channel;
    uint32_t operation, result, processing_tick, high_water_token;
    uint32_t ack_cookie, ack_token, ack_result, reserved[5];
} aica_observation_result_t;

typedef struct aica_observation_channel {
    uint32_t sequence, session, attribution_token, attribution_kind;
    uint32_t position, key_on, challenge_cookie, observation_tick;
} aica_observation_channel_t;

/* These payloads follow an unchanged 32-byte aica_cmd_t header.
   timestamp/misc are zero. cmd_id is the fresh cookie, or the CONTROL token. */
typedef struct aica_observation_bind { uint32_t session; }
    aica_observation_bind_t;
typedef struct aica_observation_control {
    uint32_t session, token, result_slot, channel;
    uint32_t operation, fields, start_flags;
    aica_channel_config_t config;
} aica_observation_control_t;
typedef struct aica_observation_sample { uint32_t session, channel; }
    aica_observation_sample_t;
typedef struct aica_observation_consume { uint32_t session, result_slot, token; }
    aica_observation_consume_t;

#ifdef __cplusplus
#define AICA_OBS_ASSERT static_assert
#define AICA_OBS_ALIGNOF alignof
#else
#define AICA_OBS_ASSERT _Static_assert
#define AICA_OBS_ALIGNOF _Alignof
#endif
AICA_OBS_ASSERT(sizeof(uint32_t) == 4, "observation word");
AICA_OBS_ASSERT(sizeof(aica_cmd_t) == 32, "legacy command header");
AICA_OBS_ASSERT(sizeof(aica_channel_config_t) == 148, "legacy config layout");
AICA_OBS_ASSERT(sizeof(aica_observation_header_t) == 64, "header stride");
AICA_OBS_ASSERT(sizeof(aica_observation_result_t) == 64, "result stride");
AICA_OBS_ASSERT(sizeof(aica_observation_channel_t) == 32, "channel stride");
AICA_OBS_ASSERT(sizeof(aica_observation_bind_t) == 4, "bind payload");
AICA_OBS_ASSERT(sizeof(aica_observation_control_t) == 176, "control payload");
AICA_OBS_ASSERT(sizeof(aica_observation_sample_t) == 8, "sample payload");
AICA_OBS_ASSERT(sizeof(aica_observation_consume_t) == 12, "consume payload");
AICA_OBS_ASSERT(offsetof(aica_observation_control_t, config) == 28,
                "control config offset");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, ack_cookie) == 32,
                "ack offset");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, challenge_cookie) == 24,
                "challenge offset");
AICA_OBS_ASSERT(AICA_MEM_OBSERVATION_RESULTS + 64u * 64u ==
                AICA_MEM_OBSERVATION_CHANNELS, "result array extent");
AICA_OBS_ASSERT(AICA_MEM_OBSERVATION_CHANNELS + 64u * 32u ==
                AICA_MEM_OBSERVATION_END, "channel array extent");
AICA_OBS_ASSERT(AICA_MEM_OBSERVATION_END <= 0x030000u, "reserved space fit");
AICA_OBS_ASSERT(AICA_OBS_ALIGNOF(aica_observation_header_t) == 4, "header alignment");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, sequence) == 0, "header.sequence");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, layout_version) == 4, "header.layout_version");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, ready) == 8, "header.ready");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, active_session) == 12, "header.active_session");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, bind_session) == 16, "header.bind_session");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, bind_cookie) == 20, "header.bind_cookie");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, bind_result) == 24, "header.bind_result");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, observation_tick) == 28, "header.observation_tick");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, last_protocol_error) == 32, "header.last_protocol_error");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, protocol_error_count) == 36, "header.protocol_error_count");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, flags) == 40, "header.flags");
AICA_OBS_ASSERT(offsetof(aica_observation_header_t, reserved) == 44, "header.reserved");
AICA_OBS_ASSERT(AICA_OBS_ALIGNOF(aica_observation_result_t) == 4, "result alignment");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, sequence) == 0, "result.sequence");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, session) == 4, "result.session");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, token) == 8, "result.token");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, channel) == 12, "result.channel");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, operation) == 16, "result.operation");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, result) == 20, "result.result");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, processing_tick) == 24, "result.processing_tick");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, high_water_token) == 28, "result.high_water_token");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, ack_cookie) == 32, "result.ack_cookie");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, ack_token) == 36, "result.ack_token");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, ack_result) == 40, "result.ack_result");
AICA_OBS_ASSERT(offsetof(aica_observation_result_t, reserved) == 44, "result.reserved");
AICA_OBS_ASSERT(AICA_OBS_ALIGNOF(aica_observation_channel_t) == 4, "channel alignment");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, sequence) == 0, "channel.sequence");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, session) == 4, "channel.session");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, attribution_token) == 8, "channel.attribution_token");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, attribution_kind) == 12, "channel.attribution_kind");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, position) == 16, "channel.position");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, key_on) == 20, "channel.key_on");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, challenge_cookie) == 24, "channel.challenge_cookie");
AICA_OBS_ASSERT(offsetof(aica_observation_channel_t, observation_tick) == 28, "channel.observation_tick");
#undef AICA_OBS_ALIGNOF
#undef AICA_OBS_ASSERT
#endif
