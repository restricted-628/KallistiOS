/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Hand-authored synthetic codec inputs; no proprietary media.
*/
#include <dc/sound/adx.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (24 - 8 * i));
}

static size_t fixture(uint8_t *p, unsigned version, unsigned channels,
                      uint32_t frames, unsigned offset) {
    size_t length = offset + ((frames + 31u) / 32u) * 18u * channels;
    memset(p, 0, length);
    p[0] = 0x80;
    p[2] = (uint8_t)((offset - 4) >> 8);
    p[3] = (uint8_t)(offset - 4);
    p[4] = 3; p[5] = 18; p[6] = 4; p[7] = (uint8_t)channels;
    put32(p + 8, 44100);
    put32(p + 12, frames);
    p[16] = 1; p[17] = 244; p[18] = (uint8_t)version;
    memcpy(p + offset - 6, "(c)CRI", 6);
    for(size_t pos = offset; pos < length; pos += 18) {
        p[pos + 1] = 3;
        for(unsigned n = 2; n < 18; ++n)
            p[pos + n] = (uint8_t)(pos * 7 + n * 19);
    }
    return length;
}

static size_t run(const uint8_t *input, size_t bytes, size_t chunk,
                  int16_t *output, snd_adx_status_t expected) {
    snd_adx_decoder_t d;
    snd_adx_init(&d);
    size_t pos = 0, frames = 0;
    for(size_t iteration = 0; iteration < bytes + 1024; ++iteration) {
        size_t available = bytes - pos;
        if(available > chunk)
            available = chunk;
        snd_adx_result_t r = snd_adx_decode(&d, input + pos, available,
                         output + frames * (d.header_ready ? d.info.channels : 1),
                         32, pos + available == bytes);
        assert(r.consumed <= available && r.consumed <= 256);
        assert(r.frames <= 32);
        pos += r.consumed;
        frames += r.frames;
        if(r.status >= SND_ADX_DONE) {
            assert(r.status == expected);
            return frames;
        }
        assert(r.consumed || r.frames || r.status == SND_ADX_MORE);
    }
    assert(!"decoder did not terminate");
    return 0;
}

static void test_chunks(void) {
    uint8_t input[2400];
    int16_t baseline[256], fragmented[256];
    const unsigned offsets[] = {26, 36, 288, 2048};
    const uint32_t counts[] = {0, 1, 31, 32, 33, 97};
    for(unsigned version = 3; version <= 5; version += 2)
    for(unsigned channels = 1; channels <= 2; ++channels)
    for(size_t o = 0; o < sizeof(offsets) / sizeof(*offsets); ++o)
    for(size_t n = 0; n < sizeof(counts) / sizeof(*counts); ++n) {
        size_t bytes = fixture(input, version, channels, counts[n], offsets[o]);
        assert(run(input, bytes, bytes, baseline, SND_ADX_DONE) == counts[n]);
        for(size_t chunk = 1; chunk <= 39; ++chunk) {
            memset(fragmented, 0x6b, sizeof(fragmented));
            assert(run(input, bytes, chunk, fragmented, SND_ADX_DONE) == counts[n]);
            assert(!memcmp(baseline, fragmented, counts[n] * channels * 2));
            assert(fragmented[counts[n] * channels] == 0x6b6b);
        }
    }
}

static void test_truncation(void) {
    uint8_t input[128];
    int16_t out[128];
    size_t bytes = fixture(input, 3, 2, 33, 36);
    for(size_t end = 0; end < bytes; ++end)
        run(input, end, 7, out, SND_ADX_TRUNCATED);
}

static void test_admission(void) {
    uint8_t input[128];
    int16_t out[128];
    static const struct { unsigned at, value; snd_adx_status_t status; } bad[] = {
        {0, 0, SND_ADX_INVALID}, {3, 0, SND_ADX_INVALID},
        {4, 4, SND_ADX_UNSUPPORTED}, {5, 19, SND_ADX_UNSUPPORTED},
        {6, 8, SND_ADX_UNSUPPORTED}, {7, 0, SND_ADX_UNSUPPORTED},
        {7, 3, SND_ADX_UNSUPPORTED}, {18, 4, SND_ADX_UNSUPPORTED},
        {19, 8, SND_ADX_UNSUPPORTED}, {30, 0, SND_ADX_INVALID},
        {8, 1, SND_ADX_UNSUPPORTED}, {12, 128, SND_ADX_INVALID},
        {16, 255, SND_ADX_INVALID}, {36, 128, SND_ADX_INVALID},
        {54, 128, SND_ADX_INVALID}
    };
    for(size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        size_t bytes = fixture(input, 3, 2, 32, 36);
        input[bad[i].at] = (uint8_t)bad[i].value;
        run(input, bytes, 1, out, bad[i].status);
    }
    size_t bytes = fixture(input, 5, 1, 32, 36);
    memset(input + 8, 0, 4);
    run(input, bytes, bytes, out, SND_ADX_INVALID);
    fixture(input, 5, 1, 32, 36);
    input[16] = input[17] = 0;
    run(input, bytes, bytes, out, SND_ADX_INVALID);
}

static void test_numerics(void) {
    uint8_t input[72];
    int16_t output[64];
    for(unsigned version = 3; version <= 5; version += 2) {
        size_t bytes = fixture(input, version, 1, 32, 36);
        memset(input + 36, 0, 18);
        input[38] = 0x1f; /* scale 0 means multiplier 1, not silence */
        run(input, bytes, bytes, output, SND_ADX_DONE);
        assert(output[0] == 1 && output[1] == 0);
        assert(output[2] == -1);
        assert(output[3] == -2);
        input[36] = 0x7f; input[37] = 0xff;
        memset(input + 38, 0x77, 16);
        run(input, bytes, bytes, output, SND_ADX_DONE);
        for(unsigned i = 0; i < 32; ++i)
            assert(output[i] == INT16_MAX);
        memset(input + 38, 0x88, 16);
        run(input, bytes, bytes, output, SND_ADX_DONE);
        for(unsigned i = 0; i < 32; ++i)
            assert(output[i] == INT16_MIN);
    }
}

static void test_state(void) {
    uint8_t input[128];
    int16_t out[128];
    snd_adx_decoder_t a, b, snapshot;
    size_t bytes = fixture(input, 5, 2, 33, 36);
    snd_adx_init(&a); snd_adx_init(&b);
    snd_adx_result_t r = snd_adx_decode(&a, input, bytes, out, 32, true);
    assert(r.status == SND_ADX_MORE && r.consumed == 36 && !r.frames);
    assert(a.header_ready && sizeof(a) <= 160);
    assert(a.coefficient[0] == 7334 && a.coefficient[1] == -3283);
    snapshot = a;
    r = snd_adx_decode(&a, input + 36, bytes - 36, out, 31, true);
    assert(r.status == SND_ADX_NEED_OUTPUT && !r.consumed && !r.frames);
    assert(!memcmp(&a, &snapshot, sizeof(a)));
    r = snd_adx_decode(&a, NULL, 1, out, 32, false);
    assert(r.status == SND_ADX_BAD_ARGUMENT);
    assert(!memcmp(&a, &snapshot, sizeof(a)));
    snd_adx_decode(&b, input, bytes, out, 32, true);
    r = snd_adx_decode(&a, input + 36, 36, out, 32, false);
    assert(r.frames == 32 && r.status == SND_ADX_MORE);
    snd_adx_result_t other = snd_adx_decode(&b, input + 36, 36, out + 64, 32, false);
    assert(other.frames == 32 && !memcmp(out, out + 64, 128));
    r = snd_adx_decode(&a, input + 72, 36, out, 1, true);
    assert(r.frames == 1 && r.status == SND_ADX_DONE);
    r = snd_adx_decode(&a, input, bytes, out, 32, true);
    assert(!r.frames && !r.consumed && r.status == SND_ADX_DONE);
    snd_adx_cancel(&b);
    r = snd_adx_decode(&b, input, bytes, out, 32, true);
    assert(r.status == SND_ADX_CANCELLED && !r.consumed);
    snd_adx_init(&b);
    assert(!b.frames_done && !b.header_ready);
    snd_adx_init(NULL); snd_adx_cancel(NULL);
    assert(snd_adx_decode(NULL, NULL, 0, NULL, 0, false).status == SND_ADX_BAD_ARGUMENT);
}

static void test_limits_and_atomicity(void) {
    uint8_t *input = malloc(65539 + 72);
    int16_t out[64];
    assert(input);
    size_t bytes = fixture(input, 5, 2, 32, 65539);
    assert(run(input, bytes, 37, out, SND_ADX_DONE) == 32);
    free(input);

    uint8_t small[128];
    fixture(small, 3, 2, 32, 36);
    snd_adx_decoder_t d;
    snd_adx_init(&d);
    snd_adx_result_t r = snd_adx_decode(&d, NULL, 0, NULL, 0, false);
    assert(r.status == SND_ADX_NEED_INPUT && !r.consumed);
    snd_adx_decode(&d, small, 36, out, 32, false);
    for(unsigned i = 0; i < 64; ++i)
        out[i] = 12345;
    small[54] = 0x80; /* Second channel fails: first must not be published. */
    r = snd_adx_decode(&d, small + 36, 36, out, 32, false);
    assert(r.status == SND_ADX_INVALID && !r.frames);
    for(unsigned i = 0; i < 64; ++i)
        assert(out[i] == 12345);
    assert(!d.frames_done && !d.history[0][0] && !d.history[1][0]);
    r = snd_adx_decode(&d, small, 36, out, 32, false);
    assert(r.status == SND_ADX_INVALID && !r.consumed);

    snd_adx_init(&d);
    fixture(small, 5, 1, 1, 36);
    memset(small + 54, 0x80, 16);
    snd_adx_decode(&d, small, 70, out, 32, true);
    r = snd_adx_decode(&d, small + 36, 34, out, 32, true);
    assert(r.status == SND_ADX_DONE && r.consumed == 18 && r.frames == 1);
}

static uint32_t random_word(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_mutations(void) {
    uint32_t seed = 1;
    for(unsigned trial = 0; trial < 10000; ++trial) {
        uint8_t input[128];
        int16_t guarded[66];
        size_t bytes = fixture(input, trial & 1 ? 3 : 5, 2, 33, 36);
        for(unsigned mutation = 0; mutation < trial % 5; ++mutation) {
            size_t at = random_word(&seed) % bytes;
            input[at] ^= (uint8_t)random_word(&seed);
        }
        bytes = random_word(&seed) % (bytes + 1);
        snd_adx_decoder_t d;
        snd_adx_init(&d);
        size_t pos = 0;
        for(unsigned step = 0; step < 200; ++step) {
            size_t count = bytes - pos;
            if(count > 7)
                count = 7;
            guarded[0] = 23456; guarded[65] = -23456;
            snd_adx_result_t r = snd_adx_decode(&d, input + pos, count,
                                       guarded + 1, 32, pos + count == bytes);
            assert(r.consumed <= count && r.frames <= 32);
            assert(guarded[0] == 23456 && guarded[65] == -23456);
            pos += r.consumed;
            if(r.status >= SND_ADX_DONE)
                break;
            assert(step != 199);
        }
    }
}

int main(void) {
    test_chunks(); test_truncation(); test_admission();
    test_numerics(); test_state(); test_limits_and_atomicity(); test_mutations();
    puts("ADX: chunking, bounds, truncation, admission, numerics and state PASS");
    return 0;
}
