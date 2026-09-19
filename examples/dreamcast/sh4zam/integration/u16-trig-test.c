/* KallistiOS ##version##
   Strict validator for KOS's local SH4ZAM u16 adapter. */
#ifdef __DREAMCAST__
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);
#endif
#include <kos/sh4zam.h>
#include <math.h>
#include <stdio.h>

#define DECLARE_LANE(name) \
    shz_sincos_t name(uint16_t angle); \
    shz_sincos_t name##_constant(void)
DECLARE_LANE(trig_strict);
DECLARE_LANE(trig_fast);
DECLARE_LANE(trig_ofast);
DECLARE_LANE(trig_cpp);

int main(void) {
    static const struct {
        const char *name;
        shz_sincos_t (*runtime)(uint16_t);
        shz_sincos_t (*constant)(void);
    } lanes[] = {
        { "strict", trig_strict, trig_strict_constant },
        { "fast", trig_fast, trig_fast_constant },
        { "Ofast", trig_ofast, trig_ofast_constant },
        { "C++ fast", trig_cpp, trig_cpp_constant }
    };
    unsigned failures[4] = { 0 };
    double max_error[4] = { 0 };
    const double tau = 6.283185307179586476925286766559;
    const double tolerance = 3e-4;
    int failed = 0;

#ifdef __DREAMCAST__
    dbglog_set_level(DBG_INFO);
    dbgio_dev_select("scif");
#endif
    /* Independent double-precision reference; no SH4ZAM u16 implementation
       or fast-math code is used to decide what the expected result is. */
    for(unsigned angle = 0; angle < 65536; ++angle) {
        double radians = (double)angle * (tau / 65536.0);
        double expected_sin = sin(radians), expected_cos = cos(radians);

        for(unsigned lane = 0; lane < 4; ++lane) {
            shz_sincos_t actual = lanes[lane].runtime((uint16_t)angle);
            double error = fmax(fabs(actual.sin - expected_sin),
                                fabs(actual.cos - expected_cos));
            if(!isfinite(actual.sin) || !isfinite(actual.cos) || error > tolerance) {
                if(failures[lane]++ == 0)
                    printf("%s first failure: angle=%u sin=%.9g cos=%.9g\n",
                           lanes[lane].name, angle,
                           (double)actual.sin, (double)actual.cos);
            }
            if(error > max_error[lane])
                max_error[lane] = error;
        }
    }
    for(unsigned lane = 0; lane < 4; ++lane) {
        shz_sincos_t constant = lanes[lane].constant();
        int constant_ok = isfinite(constant.sin) && isfinite(constant.cos) &&
                          fabs(constant.sin - 1.0) <= tolerance &&
                          fabs(constant.cos) <= tolerance;
        failed |= failures[lane] != 0 || !constant_ok;
        printf("%s: 65536 angles, failures=%u max_error=%.9g constant=%s\n",
               lanes[lane].name, failures[lane], max_error[lane],
               constant_ok ? "PASS" : "FAIL");
    }
    printf("RESULT: %s (KOS SH4ZAM u16 adapter)\n", failed ? "FAIL" : "PASS");
    fflush(stdout);
#ifdef __DREAMCAST__
    thd_sleep(15000);
#endif
    return failed;
}
