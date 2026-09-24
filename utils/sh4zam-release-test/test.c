/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Reuse the target release regressions against the portable backend. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sh4zam/shz_sh4zam.h>

static bool close_enough(float a, float b) {
    return isfinite(a) && isfinite(b) && fabsf(a - b) <= 3e-4f;
}

#include "../../examples/dreamcast/sh4zam/integration/release-fixtures.h"

int main(void) {
    bool passed = verify_release_memory();
    passed &= verify_release_math();
    passed &= verify_release_3x4();
    puts(passed ? "SH4ZAM release fixtures: PASS" : "SH4ZAM release fixtures: FAIL");
    return !passed;
}
