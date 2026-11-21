/* Original PCM pitch conversion for the Sega Library 2.00J PCM profile. */
#include "adx_pitch.h"
#include <math.h>
#include <stddef.h>

/* For integer source rates 8000..48000, the base note/fine value in
   1/256-semitone units is the mathematical floor plus one at these
   143 rates. These keys are derived from observed Play helper outputs. */
static const uint16_t rounded_up_rates[] = {
    8200, 8372, 8446, 8808, 8920, 9009, 9062, 10458, 10687, 10840, 11366,
    11628, 11649, 12162, 12206, 12325, 12541, 12575, 12689, 13209, 13383, 13544,
    13593, 13636, 13741, 14166, 14182, 14198, 14259, 14486, 14522, 14840, 15337,
    15389, 15511, 16103, 16227, 16238, 16271, 16400, 16744, 16892, 17509, 17616,
    17840, 18018, 18075, 18124, 18425, 18693, 18731, 18803, 19481, 19503, 19751,
    20138, 20883, 20916, 20968, 21139, 21249, 21297, 21374, 21461, 21597, 21680,
    22477, 22691, 22732, 23235, 23298, 24324, 24423, 24539, 24823, 25378, 25585,
    25637, 25963, 26051, 26305, 26418, 26766, 26881, 27088, 27482, 27625, 27775,
    28179, 28332, 28396, 28473, 28518, 28647, 28972, 29044, 30529, 30674, 30778,
    31022, 31029, 31637, 31687, 32454, 32542, 32800, 33488, 34229, 35018, 35232,
    35680, 35971, 36150, 36248, 36850, 37327, 37386, 37462, 37589, 37759, 38551,
    38673, 38962, 39006, 39333, 40577, 40687, 41832, 42278, 42748, 43194, 43233,
    43360, 43507, 43951, 45382, 45464, 46313, 46470, 46491, 46596, 46849, 47509,
};

static uint32_t pcm_pitch_rate(uint32_t source_rate, int units,
    unsigned min_hz, unsigned max_hz) {
    if(source_rate < 8000 || source_rate > 48000 || units < -6399 || units > 6399)
        return 0;

    int base_units = (int)(3072.0 * log2(1411200.0 / source_rate));
    const size_t count = sizeof(rounded_up_rates) / sizeof(rounded_up_rates[0]);
    size_t lo = 0, hi = count;
    while(lo < hi) {
        size_t middle = lo + (hi - lo) / 2;
        if(rounded_up_rates[middle] < source_rate)
            lo = middle + 1;
        else
            hi = middle;
    }
    if(lo < count && rounded_up_rates[lo] == source_rate)
        ++base_units;

    /* Note 60 is AICA unity. Floor the signed octave, then truncate the
       nonnegative remainder to the stock half-unit steps. */
    int pitch = 60 * 256 - base_units + units;
    int octave = (pitch < 0 ? pitch - 3071 : pitch) / 3072;
    unsigned fraction = (unsigned)(pitch - octave * 3072) / 2;
    unsigned fns = (unsigned)(1024.0 * (exp2((double)fraction / 1536.0) - 1.0));
    unsigned octave_hz = octave < 0 ? 44100u >> -octave : 44100u << octave;

    /* The ordinary KOS writer floors the quotient. Use the regular
       interval and exclude aliases caused by overflow of the FNS field.
       For example, 11024 Hz encodes a word for approximately 5512 Hz. */
    unsigned rate = (octave_hz * (1024 + fns) + 1023) / 1024;
    const unsigned end = (octave_hz * (1025 + fns) + 1023) / 1024;
    if(rate < min_hz)
        rate = min_hz;
    return rate < end && rate <= max_hz ? rate : 0;
}

uint32_t media_adx_initial_pitch_rate(uint32_t source_rate, int units) {
    return pcm_pitch_rate(source_rate,units,8000,88200);
}

uint32_t media_adx_live_pitch_rate(uint32_t source_rate, int units) {
    return pcm_pitch_rate(source_rate,units,1889,186736);
}
