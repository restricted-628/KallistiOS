/* KallistiOS ##version##
   Native AICA OCT/FNS encoder shared by ARM START and the checked host.
*/
#ifndef AICA_PITCH_H
#define AICA_PITCH_H
#include <stdint.h>

static uint32_t calc_aica_pitch(uint32_t freq) {
    uint32_t freq_lo;
    uint32_t freq_base = 5644800;
    int freq_hi = 7;

    while(freq < freq_base && freq_hi > -8) {
        freq_base >>= 1;
        --freq_hi;
    }

    freq_lo = (freq << 10) / freq_base;
    return (((uint32_t)freq_hi & 0x1f) << 11) | (freq_lo & 1023);
}

#endif
