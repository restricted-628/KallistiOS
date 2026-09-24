/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Compile only this translation unit with -ffast-math. */
#include <sh4zam/shz_sh4zam.h>

shz_sincos_t release_fast_trig(uint16_t angle) {
    return shz_sincosu16(angle);
}
