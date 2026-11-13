/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#ifndef __AICA_OBSERVER_H
#define __AICA_OBSERVER_H
#include <dc/sound/aica_observation.h>
#include <dc/sound/aica_completion.h>

typedef struct aica_observer {
    volatile aica_observation_header_t *header;
    volatile aica_observation_result_t *results;
    volatile aica_observation_channel_t *channels;
    uint32_t (*apply)(uint32_t channel, const aica_channel_control_t *control);
    void (*read)(uint32_t channel, uint32_t *position, uint32_t *key_on);
    volatile aica_completion_header_t *completion_header;
    volatile aica_completion_channel_t *completions;
    uint32_t (*loop_end)(void); /* Selected channel; destructive LP read. */
} aica_observer_t;

void aica_observer_init(aica_observer_t *observer);
void aica_observer_ready(aica_observer_t *observer);
/* Packet size has been bounded by the queue parser; exact payload size and
   routing are checked here. Returns 0 for a rejected protocol/operation. */
int aica_observer_command(aica_observer_t *observer, const aica_cmd_t *packet,
                          uint32_t tick);
void aica_observer_refresh(aica_observer_t *observer, uint32_t channel,
                          uint32_t position, uint32_t key_on, uint32_t tick, uint32_t lp);
int aica_observer_needs_lp(const aica_observer_t *observer, uint32_t channel);
void aica_observer_legacy(aica_observer_t *observer, uint32_t channel);
#endif
