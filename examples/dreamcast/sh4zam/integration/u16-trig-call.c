/* KallistiOS ##version##
   Compile separately in strict, fast-math, Ofast, and C++ fast-math modes. */
#include <kos/sh4zam.h>
#define TRIG_JOIN_(a, b) a##b
#define TRIG_JOIN(a, b) TRIG_JOIN_(a, b)

#ifdef __cplusplus
extern "C" {
#endif

shz_sincos_t TRIG_CALL(uint16_t angle) {
    return kos_shz_sincosu16(angle);
}

shz_sincos_t TRIG_CONSTANT_CALL(void) {
    return kos_shz_sincosu16(16384);
}

shz_sincos_t TRIG_JOIN(TRIG_CALL, _direct)(uint16_t angle) {
    return shz_sincosu16(angle);
}

shz_sincos_t TRIG_JOIN(TRIG_CALL, _direct_constant)(void) {
    return shz_sincosu16(16384);
}

#ifdef __cplusplus
}
#endif
