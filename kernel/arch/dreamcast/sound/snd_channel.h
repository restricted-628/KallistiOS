/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#ifndef __SND_CHANNEL_H
#define __SND_CHANNEL_H
#include <stdint.h>
/* Internal shared reservation accounting. Mutations require IRQ exclusion;
   hold it across reads too when a coherent snapshot is needed.
   Count must be 1 or 2; channels must hold two ints.
   Group allocation leaves output and accounting untouched on failure. */
uint64_t snd_channel_reserved(void);
int snd_channel_reserve(unsigned count, int channels[2]);
void snd_channel_release(unsigned count, const int channels[2]);
#endif
