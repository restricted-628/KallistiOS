/* Original coefficient setup. The two profiles use different rounding contracts. */
#include "adx_math.h"
#include <stddef.h>
#include "adx_setup_table.h"

struct rounded_checkpoint {
    uint32_t angle;
    uint16_t data_offset, first, second;
    uint8_t deltas;
};

#include "adx_rounded_setup_table.h"

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

/* Encode round32(round32(cutoff * round32(2*pi)) / rate). Cutoff is positive.
   The integer construction leaves the caller's FPU controls and flags alone. */
static uint32_t rounded_angle(unsigned rate, unsigned cutoff) {
    uint64_t product = (uint64_t)cutoff * UINT32_C(0xc90fdb);
    unsigned shift = 40u - (unsigned)__builtin_clzll(product);
    uint32_t mantissa = (uint32_t)(product >> shift);
    if(shift) {
        uint32_t halfway = UINT32_C(1) << (shift - 1);
        uint32_t remainder = (uint32_t)product & (2 * halfway - 1);
        mantissa += remainder > halfway || (remainder == halfway && (mantissa & 1));
    }
    unsigned scale = 31u - (unsigned)__builtin_clz(rate);
    uint64_t scaled = (uint64_t)mantissa << scale;
    if(scaled < (uint64_t)rate << 23) {
        scaled <<= 1;
        ++scale;
    }
    uint32_t normalized = (uint32_t)(scaled / rate);
    uint32_t twice_remainder = 2u * (uint32_t)(scaled % rate);
    normalized += twice_remainder > rate || (twice_remainder == rate && (normalized & 1));
    /* The significand includes the implicit bit. Addition also takes care of
       the carry when the rounded quotient crosses into the next exponent. */
    unsigned biased_without_hidden_bit = 128u + shift - scale;
    return (biased_without_hidden_bit << 23) + normalized;
}

static void rounded_pair(uint32_t angle, int16_t q12[2]) {
    unsigned lo = 0;
    unsigned hi = sizeof(rounded_checkpoints) / sizeof(*rounded_checkpoints);
    while(hi - lo > 1) {
        unsigned mid = lo + (hi - lo) / 2;
        if(rounded_checkpoints[mid].angle <= angle)
            lo = mid;
        else
            hi = mid;
    }
    const struct rounded_checkpoint *entry = &rounded_checkpoints[lo];
    const uint8_t *data = rounded_deltas + entry->data_offset;
    uint32_t word = entry->angle;
    int first = entry->first;
    int second = entry->second;
    for(unsigned i = 0; i < entry->deltas; ++i) {
        unsigned shift = 0;
        unsigned byte;
        do {
            byte = *data++;
            word += (uint32_t)(byte & 127u) << shift;
            shift += 7;
        } while(byte & 128u);
        if(word > angle)
            break;
        int delta = *data++;
        first += (delta >> 4) - 8;
        second += (delta & 15) - 8;
    }
    q12[0] = (int16_t)first;
    q12[1] = (int16_t)-second;
}

int media_adx_rounded_coefficients(unsigned rate, unsigned cutoff, int16_t q12[2]) {
    if(!q12 || !rate || rate > MEDIA_ADX_ROUNDED_MAX_RATE || cutoff > rate / 2)
        return -1;
    rounded_pair(cutoff ? rounded_angle(rate, cutoff) : 0, q12);
    return 0;
}
