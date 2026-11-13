/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Experimental current-playback end observation, not sample retirement. */
#ifndef __DC_SOUND_AICA_COMPLETION_H
#define __DC_SOUND_AICA_COMPLETION_H
#include <stdint.h>
#include <stddef.h>
#include <dc/sound/aica_observation.h>
#define AICA_DRIVER_FEATURE_COMPLETION 0x00000020u
#define AICA_COMPLETION_LAYOUT_VERSION 1u
#define AICA_MEM_COMPLETION_HEADER 0x026900u
#define AICA_MEM_COMPLETION_CHANNELS 0x026a00u
#define AICA_MEM_COMPLETION_END 0x027a00u
#define AICA_COMPLETION_EMPTY 0u
#define AICA_COMPLETION_ACTIVE 1u
#define AICA_COMPLETION_STOPPED 2u
#define AICA_COMPLETION_INTERFERED 3u
#define AICA_COMPLETION_ELIGIBLE 1u
#define AICA_COMPLETION_END_SEEN 2u
typedef struct aica_completion_header {
    uint32_t sequence, layout_version, ready, active_session, reserved[12];
} aica_completion_header_t;
typedef struct aica_completion_channel {
    uint32_t sequence, session, start_token, command_token;
    uint32_t attribution_kind, state, flags, first_end_tick;
    uint32_t position, key_on, challenge_cookie, observation_tick;
    uint32_t format, loop_enabled, reserved[2];
} aica_completion_channel_t;
#ifdef __cplusplus
#define AICA_C_ASSERT static_assert
#else
#define AICA_C_ASSERT _Static_assert
#endif
AICA_C_ASSERT(sizeof(aica_completion_header_t) == 64, "completion header");
AICA_C_ASSERT(sizeof(aica_completion_channel_t) == 64, "completion channel");
AICA_C_ASSERT(offsetof(aica_completion_channel_t, start_token) == 8, "start identity");
AICA_C_ASSERT(offsetof(aica_completion_channel_t, flags) == 24, "completion flags");
AICA_C_ASSERT(offsetof(aica_completion_channel_t, challenge_cookie) == 40, "challenge");
AICA_C_ASSERT(offsetof(aica_completion_channel_t, reserved) == 56, "reserved");
AICA_C_ASSERT(AICA_MEM_COMPLETION_HEADER == AICA_MEM_OBSERVATION_END, "after P1a");
AICA_C_ASSERT(AICA_MEM_COMPLETION_HEADER + 64 + 0xc0 == AICA_MEM_COMPLETION_CHANNELS,
              "explicit header padding");
AICA_C_ASSERT(AICA_MEM_COMPLETION_CHANNELS + 64 * 64 == AICA_MEM_COMPLETION_END,
              "completion extent");
AICA_C_ASSERT(AICA_MEM_COMPLETION_END <= 0x030000u, "below sample RAM");
#undef AICA_C_ASSERT
#endif
