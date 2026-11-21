/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Internal mailbox for the opted-in Flycast PCM16 profile.
*/
#ifndef FLYCAST_PCM16_CUT_H
#define FLYCAST_PCM16_CUT_H
#include <stdint.h>

#define FLYCAST_CUT_ADDR 0x021500u
#define FLYCAST_CUT_MAGIC 0x46504332u
#define FLYCAST_CUT_RING 16384u
#define FLYCAST_CUT_LEAD 32u
#define FLYCAST_CUT_NATURAL 1u
#define FLYCAST_CUT_FAILED 2u

typedef struct flycast_cut_slot {
    uint32_t channel, start, command, address;
} flycast_cut_slot_t;

typedef struct flycast_cut_mailbox {
    uint32_t ready, request, session, cut;
    flycast_cut_slot_t slot[2];
    uint32_t position[2], quoted, authorize;
    uint32_t done, result;
} flycast_cut_mailbox_t;
#endif
