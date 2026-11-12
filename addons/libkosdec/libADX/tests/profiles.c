/* Original profile/ownership tests with independent rational PCM vectors. */
#include <kosdec/adx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile_vectors.h"
static unsigned failures, cases;
#define CHECK(x) do { if(!(x)) { ++failures; printf("PROFILE_FAIL line=%u\n", (unsigned)__LINE__); } } while(0)
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (24 - i * 8));
}
static uint8_t *stream(unsigned hdr, unsigned ch, unsigned ver, unsigned frames, size_t *size) {
    *size = hdr + ((frames + 31u) / 32u) * ch * 18u + 18u;
    uint8_t *p = calloc(*size + 1, 1);
    if(!p) exit(2);
    p[0] = 0x80; p[2] = (uint8_t)((hdr - 4) >> 8); p[3] = (uint8_t)(hdr - 4);
    p[4] = 3; p[5] = 18; p[6] = 4; p[7] = (uint8_t)ch;
    put32(p + 8, 44100); put32(p + 12, frames);
    p[16] = 1; p[17] = 0xf4; p[18] = (uint8_t)ver;
    memcpy(p + hdr - 6, "(c)CRI", 6);
    for(unsigned g = 0; g < (frames + 31u) / 32u; ++g)
        for(unsigned c = 0; c < ch; ++c) {
            uint8_t *b = p + hdr + (g * ch + c) * 18;
            b[1] = (uint8_t)(c + 1);
            for(unsigned i = 2; i < 18; ++i) b[i] = (uint8_t)(i * 7 + g * 3 + c * 19);
        }
    p[*size - 18] = 0x80; p[*size - 17] = 1; p[*size - 15] = 14;
    return p;
}
static enum kosdec_adx_status decode(const uint8_t *p, size_t n, size_t fragment,
                                    struct kosdec_adx_decoder *d, int16_t *out) {
    kosdec_adx_init(d);
    size_t at = 0;
    for(size_t calls = 0; calls < n + 256; ++calls) {
        size_t bytes = n - at;
        if(bytes > fragment) bytes = fragment;
        bool ready = d->header_ready;
        struct kosdec_adx_result r = kosdec_adx_decode(d, p + at, bytes,
            out + d->frames_done * d->channels, 65 - d->frames_done, at + bytes == n);
        CHECK(r.consumed <= bytes && r.frames <= 32);
        if(!ready) CHECK(r.consumed <= KOSDEC_ADX_HEADER_BUDGET);
        at += r.consumed;
        if(r.status >= KOSDEC_ADX_DONE) return r.status;
        if(!r.consumed && !r.frames) { CHECK(0); return r.status; }
    }
    CHECK(0); return KOSDEC_ADX_INVALID;
}
int main(void) {
    const unsigned headers[] = {26,36,288,2048,65539};
    const unsigned counts[] = {0,1,31,32,33,65};
    const unsigned fragments[] = {1,7,37,65536};
    struct kosdec_adx_decoder d;
    int16_t out[132];
    for(unsigned h = 0; h < 5; ++h) for(unsigned ch = 1; ch <= 2; ++ch)
    for(unsigned ver = 3; ver <= 5; ver += 2) for(unsigned k = 0; k < 6; ++k) {
        size_t n;
        uint8_t *p = stream(headers[h], ch, ver, counts[k], &n);
        for(unsigned f = 0; f < 4; ++f) {
            ++cases; memset(out, 0x5a, sizeof(out));
            CHECK(decode(p, n, fragments[f], &d, out) == KOSDEC_ADX_DONE);
            CHECK(d.header_ready && d.channels == ch && d.version == ver);
            CHECK(d.header_bytes == headers[h] && d.sample_rate == 44100 && d.cutoff == 500);
            CHECK(d.frames_done == counts[k] && !d.clips);
            for(unsigned i = 0; i < counts[k]; ++i)
                for(unsigned c = 0; c < ch; ++c) CHECK(out[i * ch + c] == golden[c][i]);
            for(unsigned i = counts[k] * ch; i < 132; ++i) CHECK(out[i] == 0x5a5a);
        }
        free(p);
    }
    /* Every truncated prefix of an ordinary mono file, including a partial group. */
    size_t n; uint8_t *p = stream(36, 1, 3, 65, &n);
    for(size_t end = 0; end < n; ++end)
        CHECK(decode(p, end, 7, &d, out) == KOSDEC_ADX_TRUNCATED);
    p[20] = 1; /* Nonzero extension: do not silently ignore a loop or other feature. */
    CHECK(decode(p, n, 7, &d, out) == KOSDEC_ADX_UNSUPPORTED && !d.frames_done);
    p[20] = 0;
    const uint32_t lengths[] = {529201,11890839,KOSDEC_ADX_MAX_FRAMES};
    for(unsigned i = 0; i < 3; ++i) {
        put32(p + 12, lengths[i]); kosdec_adx_init(&d);
        struct kosdec_adx_result r = kosdec_adx_decode(&d, p, 36, NULL, 0, false);
        CHECK(r.status == KOSDEC_ADX_MORE && d.header_ready && d.total_frames == lengths[i]);
    }
    put32(p + 12, KOSDEC_ADX_MAX_FRAMES + 1u);
    CHECK(decode(p, n, 7, &d, out) == KOSDEC_ADX_UNSUPPORTED);
    p[2] = 0; p[3] = 21;
    CHECK(decode(p, n, 7, &d, out) == KOSDEC_ADX_INVALID);
    free(p);
    /* Budget exhaustion with final input still leaves unread input for the caller. */
    p = stream(65539, 1, 3, 0, &n); kosdec_adx_init(&d);
    struct kosdec_adx_result r = kosdec_adx_decode(&d, p, n, NULL, 0, true);
    CHECK(r.status == KOSDEC_ADX_MORE && r.consumed == 512 && !d.header_ready);
    kosdec_adx_cancel(&d);
    r = kosdec_adx_decode(&d, p + 512, n - 512, NULL, 0, true);
    CHECK(r.status == KOSDEC_ADX_CANCELLED && !r.consumed);
    free(p);
    printf("ADX_PROFILES cases=%u failures=%u\n", cases, failures);
    return failures ? 1 : 0;
}
