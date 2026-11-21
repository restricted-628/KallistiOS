/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#ifndef __SND_CHANNEL_H
#define __SND_CHANNEL_H
#include <stdint.h>
/* Internal shared reservation accounting. Mutations require IRQ exclusion;
   hold it across reads too when a coherent snapshot is needed.
   Count must be 1 or 2; channels must hold two ints.
   Group allocation leaves output and accounting untouched on failure. */
typedef enum snd_channel_class {
    SND_CHANNEL_CLASS_MIDI,
    SND_CHANNEL_CLASS_SHOT,
    SND_CHANNEL_CLASS_PCM,
    SND_CHANNEL_CLASS_COUNT
} snd_channel_class_t;

uint64_t snd_channel_reserved(void);
uint64_t snd_channel_shot_unavailable(void);
int snd_channel_reserve(snd_channel_class_t source, unsigned count, int channels[2]);
void snd_channel_release(unsigned count, const int channels[2]);
#endif
