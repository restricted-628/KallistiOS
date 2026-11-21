#ifndef MEDIA_ADX_PITCH_H
#define MEDIA_ADX_PITCH_H

#include <stdint.h>

/* Resolve an ordinary startup representative Hz, or zero if unsupported. */
uint32_t media_adx_initial_pitch_rate(uint32_t source_rate, int units);

#endif
