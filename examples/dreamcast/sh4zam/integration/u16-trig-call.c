/* KallistiOS ##version##
   Compile separately in strict, fast-math, Ofast, and C++ fast-math modes. */
#include <kos/sh4zam.h>

#ifdef __cplusplus
extern "C" {
#endif

shz_sincos_t TRIG_CALL(uint16_t angle) {
    return kos_shz_sincosu16(angle);
}

shz_sincos_t TRIG_CONSTANT_CALL(void) {
    return kos_shz_sincosu16(16384);
}

#ifdef __cplusplus
}
#endif
