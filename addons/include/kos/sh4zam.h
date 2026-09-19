/* KallistiOS ##version##
   KOS-side SH4ZAM integration helpers. Not part of upstream SH4ZAM. */

#ifndef __KOS_SH4ZAM_H
#define __KOS_SH4ZAM_H

#include <sh4zam/shz_trig.h>

/** \file kos/sh4zam.h
    \brief KOS-side helpers for the separately maintained SH4ZAM library.
*/

/** \brief Return sine and cosine for an unsigned 16-bit turn angle.

    One turn is 65536 units: 16384 is a quarter-turn, and 65535 is one
    unit short of a full turn. Constant and runtime arguments use the same
    convention on SH-4 and on the portable backend.

    This explicit helper avoids the SH4ZAM 0.8.1 SH-4 fast-math u16 conversion
    discrepancy by using its public radians API. It does not replace or modify
    upstream shz_sincosu16(). Fast-math remains enabled for the caller; SH4ZAM
    and the compiler still select the radians implementation. Results are
    approximate, not promised bit-identical across compiler modes/backends.

    \param angle Angle in unsigned 16-bit turn units.
    \return Approximate sine and cosine of the angle.
*/
static inline shz_sincos_t kos_shz_sincosu16(uint16_t angle) SHZ_NOEXCEPT {
    return shz_sincosf((float)angle * (SHZ_F_TAU / 65536.0f));
}

#endif /* __KOS_SH4ZAM_H */
