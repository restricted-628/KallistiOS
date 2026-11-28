/* KallistiOS ##version## */
#ifndef PVR_2D_ROTATION_H
#define PVR_2D_ROTATION_H

#include <dc/sh4zam.h>
#include <math.h>

/* Callers admit any finite binary32 radian value. In this ordinary-angle
   range the FSCA phase is below 2^20 ticks, so the scale step retains
   fractional-tick precision before FTRC. Beyond it use libm's full
   argument reduction: a float approximation of 2*pi accumulates error
   with every turn, and a direct signed FTRC eventually saturates. */
static inline shz_sincos_t pvr_2d_sincos(float radians) {
    if(fabsf(radians) <= 64.0f)
        return shz_sincosf(radians);

    return (shz_sincos_t){ .sin = sinf(radians), .cos = cosf(radians) };
}

#endif
