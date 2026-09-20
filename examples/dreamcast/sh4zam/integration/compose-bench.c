/* KallistiOS ##version##
   Correctness gates and target-only composition timing harness.
   Copyright (C) 2026 Joseph Black
*/
#ifdef __DREAMCAST__
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);
#endif
#include "compose-candidate.h"
#include <dc/sh4zam.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BATCH 32u
#define REPEATS 7u
#define ITERATIONS 128u
typedef int (*compose_fn)(matrix_t *, const matrix_t *, const matrix_t *);
static const compose_fn implementations[] = { mat_compose, compose_candidate };
static const char *const names[] = { "kos-compose", "saved-xmtrx" };
static const char *const workloads[] = { "independent", "parent-chain", "in-place" };
static matrix_t left[BATCH], right[BATCH], output[BATCH];
static shz_mat4x4_t sentinel;

static bool state_unchanged(void) {
    shz_mat4x4_t observed;
    shz_xmtrx_store_4x4(&observed);
    return !memcmp(&observed, &sentinel, sizeof(observed));
}

static uint32_t control_modes(void) {
#ifdef __DREAMCAST__
    /* FR, SZ, PR, DN and rounding. Arithmetic sticky flags are not compared. */
    return __builtin_sh_get_fpscr() & ((15u << 18) | 3u);
#else
    return 0;
#endif
}

static void reference(double out[4][4], const double a[4][4], const double b[4][4]) {
    double result[4][4];
    for(size_t c = 0; c < 4; ++c) for(size_t r = 0; r < 4; ++r) {
        result[c][r] = 0;
        for(size_t k = 0; k < 4; ++k) result[c][r] += a[k][r] * b[c][k];
    }
    memcpy(out, result, sizeof(result));
}

static void to_double(double out[4][4], const matrix_t *matrix) {
    for(size_t c = 0; c < 4; ++c) for(size_t r = 0; r < 4; ++r)
        out[c][r] = (*matrix)[c][r];
}

static bool matches(const matrix_t *actual, const double expected[4][4]) {
#ifdef __DREAMCAST__
    const double tolerance = 0.0003;
#else
    const double tolerance = 0.00002;
#endif
    for(size_t c = 0; c < 4; ++c) for(size_t r = 0; r < 4; ++r) {
        double value = (*actual)[c][r];
        if(!isfinite(value) || fabs(value - expected[c][r]) >
           tolerance * (1 + fabs(expected[c][r]))) return false;
    }
    return true;
}

static int contracts(void) {
    const uint32_t modes = control_modes();
    for(size_t implementation = 0; implementation < 2; ++implementation) {
        uint32_t random = UINT32_C(0x31415926);
        for(unsigned sample = 0; sample < 32; ++sample)
        for(unsigned alias = 0; alias < 5; ++alias) {
            matrix_t a, b;
            struct { uint64_t before; matrix_t matrix; uint64_t after; } guarded;
            guarded.before = UINT64_C(0x123456789abcdef0);
            guarded.after = UINT64_C(0xfedcba9876543210);
            for(size_t c = 0; c < 4; ++c) for(size_t r = 0; r < 4; ++r) {
                random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                a[c][r] = ((int)(random >> 24) - 128) * 0.03125f;
                random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                b[c][r] = ((int)(random >> 24) - 128) * 0.03125f;
            }
            double da[4][4], db[4][4], expected[4][4];
            to_double(da, &a);
            to_double(db, alias >= 3 ? &a : &b);
            reference(expected, da, db);
            matrix_t *out = alias == 1 || alias == 3 ? &a :
                            alias == 2 ? &b : &guarded.matrix;
            if(implementations[implementation](out, &a, alias >= 3 ? &a : &b) ||
               !matches(out, expected) || !state_unchanged() || control_modes() != modes ||
               guarded.before != UINT64_C(0x123456789abcdef0) ||
               guarded.after != UINT64_C(0xfedcba9876543210)) {
                printf("COMPOSE contract failed impl=%s sample=%u alias=%u\n",
                       names[implementation], sample, alias);
                return -1;
            }
        }
    }
    for(size_t implementation = 0; implementation < 2; ++implementation)
    for(unsigned failure = 0; failure < 6; ++failure) {
        matrix_t a = { { 0 } }, b = { { 0 } }, out, before;
        memset(out, 0x5a, sizeof(out));
        memcpy(before, out, sizeof(before));
        errno = 0;
        int rc = implementations[implementation](
            failure == 0 ? NULL : failure == 3 ? (void *)((uint8_t *)&out + 1) : &out,
            failure == 1 ? NULL : failure == 4 ? (void *)((uint8_t *)&a + 1) : &a,
            failure == 2 ? NULL : failure == 5 ? (void *)((uint8_t *)&b + 1) : &b);
        if(rc != -1 || errno != EINVAL || memcmp(before, out, sizeof(out)) ||
           !state_unchanged() || control_modes() != modes) return -1;
    }
    puts("COMPOSE aliases, full matrices, rejection, XMTRX/control modes: PASS");
    return 0;
}

static void init_workloads(void) {
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    for(size_t i = 0; i < BATCH; ++i) {
        float angle = (float)(i + 1) * 0.015625f;
        float c = cosf(angle), s = sinf(angle);
        left[i][0][0] = c; left[i][0][1] = s;
        left[i][1][0] = -s; left[i][1][1] = c;
        left[i][2][2] = left[i][3][3] = 1;
        left[i][3][0] = (float)i * 0.125f;
        right[i][0][0] = right[i][1][1] = right[i][2][2] = right[i][3][3] = 1;
        right[i][0][1] = (float)(i % 3) * 0.00390625f;
        right[i][1][2] = -0.0078125f;
        right[i][3][1] = (float)(i + 1) * 0.03125f;
    }
}

static int run_workload(compose_fn compose, unsigned workload, unsigned iterations) {
    for(unsigned iteration = 0; iteration < iterations; ++iteration) {
        /* This copy is part of the in-place workload, common to both paths. */
        if(workload == 2) memcpy(output, left, sizeof(output));
        for(size_t i = 0; i < BATCH; ++i) {
            const matrix_t *a = workload == 2 ? &output[i] :
                workload == 1 ? (i ? &output[i - 1] : &left[0]) : &left[i];
            if(compose(&output[i], a, &right[i])) return -1;
        }
    }
    return 0;
}

static bool workload_matches(unsigned workload) {
    double parent[4][4];
    to_double(parent, &left[0]);
    for(size_t i = 0; i < BATCH; ++i) {
        double a[4][4], b[4][4], expected[4][4];
        if(workload == 1) memcpy(a, parent, sizeof(a));
        else to_double(a, &left[i]);
        to_double(b, &right[i]);
        reference(expected, a, b);
        if(!matches(&output[i], expected)) return false;
        memcpy(parent, expected, sizeof(parent));
    }
    return true;
}

#ifdef __DREAMCAST__
static int timings(void) {
    const uint32_t modes = control_modes();
    puts("COMPOSE timings require physical hardware; emulator results are not speed evidence.");
    printf("COMPOSE compiler=%s batch=%u iterations=%u repeats=%u interrupt_policy=enabled\n",
           __VERSION__, BATCH, ITERATIONS, REPEATS);
    for(unsigned workload = 0; workload < 3; ++workload) {
        uint64_t times[2][REPEATS];
        /* Warm both implementations. All printing and result checks stay
           outside the measured intervals; no empty-loop subtraction. */
        for(unsigned implementation = 0; implementation < 2; ++implementation)
            if(run_workload(implementations[implementation], workload, 4)) return -1;
        for(unsigned trial = 0; trial < REPEATS; ++trial)
        for(unsigned order = 0; order < 2; ++order) {
            unsigned implementation = (trial + order) & 1u;
            shz_xmtrx_load_4x4(&sentinel);
            uint64_t start = timer_us_gettime64();
            int rc = run_workload(implementations[implementation], workload, ITERATIONS);
            times[implementation][trial] = timer_us_gettime64() - start;
            if(rc || !state_unchanged() || control_modes() != modes ||
               !workload_matches(workload)) return -1;
            printf("COMPOSE sample workload=%s impl=%s trial=%u us=%" PRIu64 "\n",
                   workloads[workload], names[implementation], trial, times[implementation][trial]);
        }
        for(unsigned implementation = 0; implementation < 2; ++implementation) {
            for(size_t i = 1; i < REPEATS; ++i) {
                uint64_t value = times[implementation][i];
                size_t j = i;
                while(j && times[implementation][j - 1] > value) {
                    times[implementation][j] = times[implementation][j - 1]; --j;
                }
                times[implementation][j] = value;
            }
            uint64_t median = times[implementation][REPEATS / 2];
            printf("COMPOSE summary workload=%s impl=%s min_us=%" PRIu64
                   " median_us=%" PRIu64 " max_us=%" PRIu64 " compositions=%u\n",
                   workloads[workload], names[implementation], times[implementation][0],
                   median, times[implementation][REPEATS - 1], BATCH * ITERATIONS);
        }
    }
    return 0;
}
#endif

int main(void) {
    shz_mat4x4_t original;
    shz_xmtrx_store_4x4(&original);
    for(size_t i = 0; i < 16; ++i) sentinel.elem[i] = (float)(3 * i + 1) * 0.25f;
    shz_xmtrx_load_4x4(&sentinel);
    int rc = contracts();
    init_workloads();
    for(unsigned workload = 0; !rc && workload < 3; ++workload)
    for(unsigned implementation = 0; !rc && implementation < 2; ++implementation) {
        shz_xmtrx_load_4x4(&sentinel);
        rc = run_workload(implementations[implementation], workload, 2);
        if(!workload_matches(workload) || !state_unchanged()) rc = -1;
        printf("COMPOSE workload=%s impl=%s validation=%s\n", workloads[workload],
               names[implementation], rc ? "FAIL" : "PASS");
    }
#ifdef __DREAMCAST__
    if(!rc) rc = timings();
#endif
    shz_xmtrx_load_4x4(&original);
    printf("RESULT: %s (matrix composition comparison; production unchanged)\n", rc ? "FAIL" : "PASS");
    return rc ? 1 : 0;
}
