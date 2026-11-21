/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Internal mailbox for the opted-in Flycast PCM16 profile.
*/
#ifndef FLYCAST_PCM16_CUT_H
#define FLYCAST_PCM16_CUT_H
#include <stdint.h>

#define FLYCAST_CUT_ADDR 0x021500u
#define FLYCAST_CUT_MAGIC 0x46504336u
#define FLYCAST_CUT_LEGACY_RING 16384u
#define FLYCAST_CUT_WIDE_RING 32768u
#define FLYCAST_CUT_MAX_RING 65504u
#define FLYCAST_CUT_LEGACY_MIN_RATE 22050u
#define FLYCAST_CUT_CLOCK_HZ 44100u
#define FLYCAST_CUT_PITCH_MASK 0x7bffu
#define FLYCAST_CUT_PHASE_ONE 1024u
#define FLYCAST_CUT_WIDE_MIN_STEP 43u
#define FLYCAST_CUT_MAX_STEP 4336u
#define FLYCAST_CUT_LEAD 32u
#define FLYCAST_CUT_NATURAL 1u
#define FLYCAST_CUT_FAILED 2u

/* Pinned Flycast PCM interpolation uses a truncated Q10 step. */
static inline uint32_t flycast_cut_phase_step(uint32_t pitch) {
    uint32_t octave = (pitch >> 11) & 15u;
    uint32_t step = 1024u | (pitch & 1023u);
    return octave & 8u ? step >> (16u - octave) : step << octave;
}

typedef struct flycast_cut_slot {
    uint32_t channel, start, command, address;
} flycast_cut_slot_t;

typedef struct flycast_cut_mailbox {
    uint32_t ready, request, session, cut, channels, pitch, ring;
    flycast_cut_slot_t slot[2];
    uint32_t position[2], quoted, authorize;
    uint32_t done, result;
} flycast_cut_mailbox_t;
#endif
