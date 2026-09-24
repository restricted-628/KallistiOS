/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Strict validator for a separately compiled fast-math SH4ZAM call. */
#include <kos.h>
#include <sh4zam/shz_sh4zam.h>
#include <math.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);
shz_sincos_t release_fast_trig(uint16_t angle);

int main(void) {
    static const uint16_t angles[] = { 0, 16384, 32768, 49152 };
    static const float sine[] = { 0, 1, 0, -1 };
    static const float cosine[] = { 1, 0, -1, 0 };
    int failed = 0;

    dbglog_set_level(DBG_INFO);
    dbgio_dev_select("scif");
    for(size_t i = 0; i < 4; ++i) {
        shz_sincos_t actual = release_fast_trig(angles[i]);
        int valid = isfinite(actual.sin) && isfinite(actual.cos) &&
                    fabsf(actual.sin - sine[i]) <= 3e-4f &&
                    fabsf(actual.cos - cosine[i]) <= 3e-4f;
        printf("u16=%u sin=%.8f cos=%.8f expected=(%.1f,%.1f): %s\n",
               angles[i], (double)actual.sin, (double)actual.cos,
               (double)sine[i], (double)cosine[i], valid ? "PASS" : "FAIL");
        failed |= !valid;
    }
    printf("RESULT: %s (SH4ZAM fast-math u16 trig)\n", failed ? "FAIL" : "PASS");
    fflush(stdout);
    thd_sleep(15000);
    return failed;
}
