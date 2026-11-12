/* Original synthetic framing tests, separated from the SFD fixture harness. */
#include <kosdec/adx.h>
#include <stdio.h>
#include <string.h>
#define FIXTURE_HEADER_BYTES 288u
static unsigned failures;
#define CHECK(x) do { if(!(x)) { ++failures; printf("ADX_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
static size_t synthetic(uint8_t *p, unsigned frames) {
    size_t groups = (frames + 31) / 32;
    size_t n = FIXTURE_HEADER_BYTES + groups * 36 + 18;
    memset(p, 0, n);
    const uint8_t h[] = {0x80,0,1,0x1c,3,18,4,2,0,0,0xac,0x44,0,0,0,0,1,0xf4,5,0};
    memcpy(p, h, sizeof(h));
    p[12] = (uint8_t)(frames >> 24); p[13] = (uint8_t)(frames >> 16);
    p[14] = (uint8_t)(frames >> 8); p[15] = (uint8_t)frames;
    memcpy(p + FIXTURE_HEADER_BYTES - 6, "(c)CRI", 6);
    for(size_t g = 0; g < groups; ++g) for(unsigned ch = 0; ch < 2; ++ch) {
        uint8_t *block = p + FIXTURE_HEADER_BYTES + g * 36 + ch * 18;
        block[1] = (uint8_t)(ch + 1);
        for(unsigned i = 2; i < 18; ++i) block[i] = (uint8_t)(i * 7 + g * 3 + ch * 19);
    }
    p[n - 18] = 0x80; p[n - 17] = 1; p[n - 15] = 14;
    return n;
}
static enum kosdec_adx_status small_decode(const uint8_t *p, size_t n, size_t fragment,
                                         struct kosdec_adx_decoder *d, int16_t *out) {
    kosdec_adx_init(d);
    size_t used = 0;
    for(unsigned calls = 0; calls < 1024; ++calls) {
        size_t bytes = n - used;
        if(bytes > fragment) bytes = fragment;
        struct kosdec_adx_result r = kosdec_adx_decode(d, p + used, bytes,
            out + d->frames_done * 2u, 64 - d->frames_done, used + bytes == n);
        CHECK(r.consumed <= bytes && r.frames <= 32);
        used += r.consumed;
        if(r.status >= KOSDEC_ADX_DONE) return r.status;
        CHECK(r.status != KOSDEC_ADX_NEED_OUTPUT);
    }
    CHECK(0); return KOSDEC_ADX_INVALID;
}
static void framing_tests(void) {
    uint8_t data[FIXTURE_HEADER_BYTES + 72 + 19], copy[sizeof(data)];
    int16_t gold[128], actual[128];
    struct kosdec_adx_decoder baseline, d, before;
    const unsigned counts[] = {0,1,16,31,32,33,64};
    for(unsigned k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
        size_t n = synthetic(data, counts[k]);
        CHECK(small_decode(data, n, n, &baseline, gold) == KOSDEC_ADX_DONE);
        for(size_t fragment = 1; fragment <= 39; ++fragment) {
            memset(actual, 0x5a, sizeof(actual));
            CHECK(small_decode(data, n, fragment, &d, actual) == KOSDEC_ADX_DONE);
            CHECK(d.frames_done == counts[k] && d.clips == baseline.clips);
            CHECK(!memcmp(gold, actual, counts[k] * 4u));
            CHECK(!memcmp(d.history, baseline.history, sizeof(d.history)));
            for(unsigned j = counts[k] * 2; j < 128; ++j) CHECK(actual[j] == 0x5a5a);
        }
    }
    size_t n = synthetic(data, 33);
    for(size_t end = 0; end < n; ++end)
        CHECK(small_decode(data, end, 13, &d, actual) == KOSDEC_ADX_TRUNCATED);
    /* Output backpressure changes no state or source cursor. */
    kosdec_adx_init(&d);
    struct kosdec_adx_result r = kosdec_adx_decode(&d, data, n, NULL, 0, true);
    CHECK(r.status == KOSDEC_ADX_MORE && r.consumed == FIXTURE_HEADER_BYTES);
    before = d;
    r = kosdec_adx_decode(&d, data + FIXTURE_HEADER_BYTES, 36, actual, 31, true);
    CHECK(r.status == KOSDEC_ADX_NEED_OUTPUT && !r.consumed && !r.frames);
    CHECK(!memcmp(&d, &before, sizeof(d)));
    r = kosdec_adx_decode(&d, NULL, 1, actual, 32, false);
    CHECK(r.status == KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&d, &before, sizeof(d)));
    r = kosdec_adx_decode(&d, data, 0, NULL, 1, false);
    CHECK(r.status == KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&d, &before, sizeof(d)));
    r = kosdec_adx_decode(&d, data, 0, (int16_t *)((uint8_t *)actual + 1), 1, false);
    CHECK(r.status == KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&d, &before, sizeof(d)));
    kosdec_adx_cancel(&d); before = d;
    r = kosdec_adx_decode(&d, data, n, actual, 32, true);
    CHECK(r.status == KOSDEC_ADX_CANCELLED && !r.consumed && !memcmp(&d, &before, sizeof(d)));
    CHECK(small_decode(data, n, 1, &d, actual) == KOSDEC_ADX_DONE); /* reset */
    before = d; kosdec_adx_cancel(&d);
    CHECK(!memcmp(&d, &before, sizeof(d)));
    /* Both scale headers must validate before the left channel writes. */
    n = synthetic(data, 32); data[FIXTURE_HEADER_BYTES + 18] = 0x80;
    kosdec_adx_init(&d); (void)kosdec_adx_decode(&d, data, FIXTURE_HEADER_BYTES, NULL, 0, false);
    memset(actual, 0x5a, sizeof(actual)); before = d;
    r = kosdec_adx_decode(&d, data + FIXTURE_HEADER_BYTES, 36, actual, 32, false);
    CHECK(r.status == KOSDEC_ADX_INVALID && !r.frames && d.frames_done == 0 && d.clips == 0);
    CHECK(!memcmp(d.history, before.history, sizeof(d.history)));
    for(unsigned i = 0; i < 128; ++i) CHECK(actual[i] == 0x5a5a);
    before = d; r = kosdec_adx_decode(&d, NULL, 0, actual, 32, true);
    CHECK(r.status == KOSDEC_ADX_INVALID && !memcmp(&d, &before, sizeof(d)));
    /* The terminal group's encoded padding cannot advance PCM/history. */
    n = synthetic(data, 33); memcpy(copy, data, n);
    for(unsigned ch = 0; ch < 2; ++ch) {
        uint8_t *block = copy + FIXTURE_HEADER_BYTES + 36 + ch * 18;
        block[2] ^= 15;
        for(unsigned i = 3; i < 18; ++i) block[i] ^= 255;
    }
    CHECK(small_decode(data, n, 7, &baseline, gold) == KOSDEC_ADX_DONE);
    CHECK(small_decode(copy, n, 11, &d, actual) == KOSDEC_ADX_DONE);
    CHECK(!memcmp(gold, actual, 33 * 4) && !memcmp(d.history, baseline.history, sizeof(d.history)));
    CHECK(d.clips == baseline.clips);
    /* Admission and footer policy are deliberately narrow and fail sticky. */
    const unsigned bad_offsets[] = {0,2,4,5,6,7,8,18,19,20,287};
    for(unsigned i = 0; i < sizeof(bad_offsets) / sizeof(bad_offsets[0]); ++i) {
        n = synthetic(data, 1); data[bad_offsets[i]] ^= 1;
        enum kosdec_adx_status s = small_decode(data, n, 17, &d, actual);
        CHECK(s == KOSDEC_ADX_UNSUPPORTED || s == KOSDEC_ADX_INVALID);
        CHECK(!d.frames_done);
    }
    n = synthetic(data, 1);
    const uint32_t too_many = KOSDEC_ADX_MAX_FRAMES + 1;
    data[12] = (uint8_t)(too_many >> 24); data[13] = (uint8_t)(too_many >> 16);
    data[14] = (uint8_t)(too_many >> 8); data[15] = (uint8_t)too_many;
    CHECK(small_decode(data, n, 17, &d, actual) == KOSDEC_ADX_UNSUPPORTED);
    CHECK(!d.frames_done);
    n = synthetic(data, 1); data[n - 1] = 1;
    CHECK(small_decode(data, n, 13, &d, actual) == KOSDEC_ADX_INVALID);
    n = synthetic(data, 1); data[n++] = 0;
    CHECK(small_decode(data, n, 13, &d, actual) == KOSDEC_ADX_INVALID);
    n = synthetic(data, 1); kosdec_adx_init(&d);
    size_t at = 0;
    do {
        r = kosdec_adx_decode(&d, data + at, n - at, actual, 32, false);
        at += r.consumed;
    } while(r.status == KOSDEC_ADX_MORE);
    CHECK(r.status == KOSDEC_ADX_NEED_END && at == n && d.frames_done == 1);
    before = d; r = kosdec_adx_decode(&d, NULL, 0, NULL, 0, false);
    CHECK(r.status == KOSDEC_ADX_NEED_END && !memcmp(&d, &before, sizeof(d)));
    r = kosdec_adx_decode(&d, NULL, 0, NULL, 0, true);
    CHECK(r.status == KOSDEC_ADX_DONE);
    printf("ADX_FRAMING_COMPLETE failures=%u state_bytes=%lu\n", failures, (unsigned long)sizeof(d));
}

int main(void) { framing_tests(); return failures ? 1 : 0; }
