/* Original exact quantized setup; generated mathematical bounds, no SDK table. */
#include "adx_math.h"
#include <stddef.h>
#include "adx_setup_table.h"

static unsigned crossed(const uint32_t *table, unsigned count,
                        unsigned rate, uint64_t cutoff_q32) {
    unsigned lo = 0, hi = count;
    while(lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if(cutoff_q32 >= (uint64_t)table[mid] * rate) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int media_adx_coefficients(unsigned rate, unsigned cutoff, int16_t q12[2]) {
    if(!q12 || !rate || rate > 48000 || cutoff > rate / 2) return -1;
    if(!cutoff) {
        q12[0] = 8192;
        q12[1] = -4096;
        return 0;
    }
    /* Each rounded boundary separates every rational cutoff/rate in this domain
       exactly. See tests/generate_setup.py and COEFFICIENTS.md. The 32x32 products
       and shifted cutoff fit uint64_t; no target transcendental rounding occurs. */
    uint64_t ratio = (uint64_t)cutoff << 32;
    unsigned first = crossed(setup_first, sizeof(setup_first)/sizeof(*setup_first), rate, ratio);
    unsigned second = crossed(setup_second, sizeof(setup_second)/sizeof(*setup_second), rate, ratio);
    q12[0] = (int16_t)(8191u - first);
    q12[1] = (int16_t)-(int)(4095u - second);
    return 0;
}
