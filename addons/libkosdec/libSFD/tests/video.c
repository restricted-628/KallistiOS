#include <kosdec/sfd.h>
#include <kosdec/sfd_video.h>
#include <kosdec/sfd_mpeg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sh4zam/shz_matrix.h>
#ifdef __DREAMCAST__
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

static unsigned failures;
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while(0)
#ifndef MEDIA_COLOR_ONLY
static uint8_t *load(const char *dir, const char *name, size_t *size) {
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return NULL;
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    if(fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if(n <= 0 || n > KOSDEC_SFD_MAX_BYTES || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *size = (size_t)n; return p;
}
#endif
static unsigned component(double x) {
    x = floor(x + 0.5);
    return x < 0 ? 0 : x > 255 ? 255 : (unsigned)x;
}
static uint16_t color_oracle(unsigned y, unsigned u, unsigned v) {
    double l = 298.0 / 256 * ((int)y - 16);
    double cb = (int)u - 128, cr = (int)v - 128;
    unsigned r = component(l + 409.0 / 256 * cr);
    unsigned g = component(l - 100.0 / 256 * cb - 208.0 / 256 * cr);
    unsigned b = component(l + 516.0 / 256 * cb);
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
static void check_conversion(void) {
    uint8_t *yuv = malloc(KOSDEC_SFD_MPEG_FRAME_BYTES);
    uint16_t *rgb = malloc(320 * 240 * sizeof(*rgb));
    if(!yuv || !rgb) { ++failures; free(yuv); free(rgb); return; }
    for(unsigned i = 0; i < KOSDEC_SFD_MPEG_FRAME_BYTES; ++i) yuv[i] = (uint8_t)(i * 73 + i / 127);
    shz_mat4x4_t seed, after;
    for(unsigned i = 0; i < 16; ++i) seed.elem[i] = (float)i * 0.125f;
    seed.elem[2] = -0.0f;
    shz_xmtrx_load_4x4(&seed);
    kosdec_sfd_rgb565(yuv, rgb);
    shz_xmtrx_store_4x4(&after);
    REQUIRE(!memcmp(&seed, &after, sizeof(seed)));
    for(unsigned y = 0; y < 240; ++y) for(unsigned x = 0; x < 320; ++x) {
        unsigned c = (y / 2) * 160 + x / 2;
        REQUIRE(rgb[y * 320 + x] == color_oracle(yuv[y * 320 + x], yuv[76800 + c], yuv[96000 + c]));
    }
    memset(yuv, 128, KOSDEC_SFD_MPEG_FRAME_BYTES);
    memset(yuv, 16, 76800); kosdec_sfd_rgb565(yuv, rgb);
    REQUIRE(rgb[0] == 0 && rgb[76799] == 0);
    memset(yuv, 235, 76800); kosdec_sfd_rgb565(yuv, rgb);
    REQUIRE(rgb[0] == 65535 && rgb[76799] == 65535);
    /* HUD must stay in its declared rows, including unavailable/large values. */
    for(unsigned pass = 0; pass < 2; ++pass) {
        for(unsigned i = 0; i < 76800; ++i) rgb[i] = 0x1234;
        kosdec_sfd_overlay(rgb, pass ? 360 : 0, pass ? 299 : 0, pass ? 10023 : 0);
        REQUIRE(rgb[0] == 0);
        for(unsigned i = 18 * 320; i < 76800; ++i) REQUIRE(rgb[i] == 0x1234);
    }
    free(yuv); free(rgb);
}
/* Integer arithmetic is solely an independent color test oracle, never a
   runtime conversion alternative. Explicit negative floor avoids signed shift. */
static unsigned oracle_channel(int x) {
    x += 128;
    x = x >= 0 ? x / 256 : -((-x + 255) / 256);
    return x < 0 ? 0 : x > 255 ? 255 : (unsigned)x;
}
static void exhaustive_color(void) {
    uint8_t *yuv = malloc(KOSDEC_SFD_MPEG_FRAME_BYTES);
    uint16_t *rgb = malloc(320 * 240 * sizeof(*rgb));
    if(!yuv || !rgb) { ++failures; free(yuv); free(rgb); return; }
    unsigned mismatches = 0, first = 0, got = 0, expected = 0;
    const unsigned total = 1u << 24;
    for(unsigned base = 0; base < total; base += 19200) {
        for(unsigned c = 0; c < 19200; ++c) {
            unsigned n = base + c;
            unsigned pixel = (c / 160) * 640 + (c % 160) * 2;
            yuv[pixel] = yuv[pixel + 1] = yuv[pixel + 320] = yuv[pixel + 321] = (uint8_t)n;
            yuv[76800 + c] = (uint8_t)(n >> 8);
            yuv[96000 + c] = (uint8_t)(n >> 16);
        }
        kosdec_sfd_rgb565(yuv, rgb);
        for(unsigned c = 0; c < 19200 && base + c < total; ++c) {
            unsigned n = base + c;
            int y = (int)(n & 255) - 16, u = (int)((n >> 8) & 255) - 128, v = (int)(n >> 16) - 128;
            unsigned r = oracle_channel(298 * y + 409 * v);
            unsigned g = oracle_channel(298 * y - 100 * u - 208 * v);
            unsigned b = oracle_channel(298 * y + 516 * u);
            unsigned want = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
            unsigned pixel = (c / 160) * 640 + (c % 160) * 2;
            if(rgb[pixel] != want) {
                if(!mismatches) { first = n; got = rgb[pixel]; expected = want; }
                ++mismatches;
            }
        }
    }
    printf("KOSDEC_SFD_COLOR tuples=%u mismatches=%u first=%06x got=%04x expected=%04x\n",
           total, mismatches, first, got, expected);
    REQUIRE(!mismatches);
    free(yuv); free(rgb);
}
#ifdef MEDIA_COLOR_ONLY
int main(void) {
#ifdef __DREAMCAST__
    dbgio_dev_select("scif");
#endif
    check_conversion();
    exhaustive_color();
    printf("KOSDEC_SFD_COLOR_COMPLETE failures=%u result=%s\n", failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
#else
static void numeric_tests(void) {
    static const unsigned weights[] = {8,16,19,22,24,26,27,29,32,34,35,37,38,40,46,48,56,58,69,83};
    unsigned cases = 0;
    for(int level = -256; level <= 255; ++level)
        for(unsigned q = 0; q <= 31; ++q)
            for(unsigned w = 0; w < sizeof(weights) / sizeof(weights[0]); ++w)
                for(int intra = 0; intra < 2; ++intra) {
                    int value = level * 2;
                    if(!intra) value += value < 0 ? -1 : 1;
                    value *= (int)(q * weights[w]);
                    value = value >= 0 ? value / 16 : -((-value + 15) / 16);
                    if(value % 2 == 0) value -= value > 0 ? 1 : -1;
                    if(value < -2048) value = -2048;
                    if(value > 2047) value = 2047;
                    REQUIRE(kosdec_sfd_mpeg_test_dequant((float)level, q, weights[w], intra) == (float)value);
                    ++cases;
                }
    for(int p = -256; p <= 255; ++p) for(int d = -255; d <= 255; ++d) {
        int want = (p + d) * 8;
        float out = 12345.0f;
        int result = kosdec_sfd_mpeg_test_dc((float)p, d, &out);
        if(want < -2048 || want > 2047) REQUIRE(result == -1 && out == 12345.0f);
        else REQUIRE(result == 0 && out == (float)want);
    }
    for(unsigned a = 0; a < 256; ++a) for(unsigned b = 0; b < 256; ++b) {
        unsigned inner = (a + b + 1) / 2;
        float pred = kosdec_sfd_mpeg_test_average2((float)a, (float)b);
        REQUIRE(pred == (float)inner);
        for(unsigned d = 0; d < 256; ++d)
            REQUIRE(kosdec_sfd_mpeg_test_average2((float)d, pred) == (float)((d + inner + 1) / 2));
    }
    for(unsigned sum = 0; sum <= 1020; ++sum) {
        unsigned rest = sum, v[4];
        for(unsigned i = 0; i < 4; ++i) { v[i] = rest > 255 ? 255 : rest; rest -= v[i]; }
        float pred = kosdec_sfd_mpeg_test_average4((float)v[0], (float)v[1], (float)v[2], (float)v[3]);
        REQUIRE(pred == (float)((sum + 2) / 4));
        for(unsigned d = 0; d < 256; ++d)
            REQUIRE(kosdec_sfd_mpeg_test_average2((float)d, pred) == (float)((d + (sum + 2) / 4 + 1) / 2));
    }
    printf("KOSDEC_SFD_NUMERIC dequant_cases=%u DC=261632 pair=65536 nested=16777216 four_sum=1021 failures=%u\n", cases, failures);
}
struct frames { FILE *reference; const char *dir; size_t count; double sum; unsigned max; uint64_t hash; };
static int stop_frame(const uint8_t *p, size_t i, void *u) {
    (void)p; (void)i; (void)u; return 1;
}
static int save_rgb(const char *dir, const char *name, size_t index, const uint8_t *yuv) {
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/%s-%03lu.rgb565", dir, name, (unsigned long)index) >= (int)sizeof(path)) return 1;
    uint16_t rgb[320 * 240];
    kosdec_sfd_rgb565(yuv, rgb);
    FILE *f = fopen(path, "wb");
    if(!f) return 1;
    int error = fwrite(rgb, sizeof(rgb), 1, f) != 1;
    return fclose(f) || error;
}
static int compare_frame(const uint8_t *pixels, size_t index, void *opaque) {
    struct frames *f = opaque;
    uint8_t reference[KOSDEC_SFD_MPEG_FRAME_BYTES];
    if(fread(reference, 1, sizeof(reference), f->reference) != sizeof(reference)) return 1;
    if(index != f->count++) return 1;
    unsigned max = 0;
    double sum = 0;
    for(size_t i = 0; i < sizeof(reference); ++i) {
        f->hash = (f->hash ^ pixels[i]) * UINT64_C(1099511628211);
        unsigned e = (unsigned)abs(pixels[i] - reference[i]);
        if(e > max) max = e;
        sum += e;
    }
    if(max > f->max) f->max = max;
    f->sum += sum;
    if(index == 0 || index == 179 || index == 359) {
        REQUIRE(!save_rgb(f->dir, "decoded", index, pixels));
        REQUIRE(!save_rgb(f->dir, "reference", index, reference));
    }
    printf("KOSDEC_SFD_FRAME index=%lu max=%u mae=%.9f\n", (unsigned long)index, max, sum / sizeof(reference));
    return 0;
}
int main(int argc, char **argv) {
    if(argc != 2) { fprintf(stderr, "usage: sfd-test fixture-directory\n"); return 2; }
    check_conversion();
    exhaustive_color();
    numeric_tests();
    size_t n = 0, oracle_size = 0;
    uint8_t *input = load(argv[1], "movie.sfd", &n);
    uint8_t *oracle = load(argv[1], "oracle.m1v", &oracle_size);
    if(!input || !oracle) { free(input); free(oracle); return 2; }
    struct kosdec_sfd_info info;
    REQUIRE(kosdec_sfd_video_extract(input, n, NULL, 0, &info) == KOSDEC_SFD_OK);
    size_t bytes = info.video_bytes;
    uint8_t *video = malloc(bytes);
    if(!video) return 2;
    REQUIRE(kosdec_sfd_video_extract(input, n, video, bytes, &info) == KOSDEC_SFD_OK);
    REQUIRE(bytes == oracle_size && !memcmp(video, oracle, bytes));
    REQUIRE(info.video_packets > 0 && info.audio_packets > 0 && info.timestamps > 0);
    REQUIRE(kosdec_sfd_video_extract(NULL, n, NULL, 0, &info) == KOSDEC_SFD_INVALID);
    REQUIRE(kosdec_sfd_video_extract(input, KOSDEC_SFD_MAX_BYTES + 1u, NULL, 0, &info) == KOSDEC_SFD_LIMIT);
    REQUIRE(kosdec_sfd_video_extract(input, 11, NULL, 0, &info) == KOSDEC_SFD_TRUNCATED);
    REQUIRE(kosdec_sfd_video_extract(input, n - 1, NULL, 0, &info) == KOSDEC_SFD_TRUNCATED);
    REQUIRE(kosdec_sfd_video_extract(input, n, video, bytes - 1, &info) == KOSDEC_SFD_LIMIT);
    input[n - 1] = 0;
    REQUIRE(kosdec_sfd_video_extract(input, n, NULL, 0, &info) == KOSDEC_SFD_INVALID);
    input[n - 1] = 255;
    uint8_t *extra = malloc(n + 2048);
    if(!extra) return 2;
    memcpy(extra, input, n); memset(extra + n, 255, 2048);
    REQUIRE(kosdec_sfd_video_extract(extra, n + 2048, NULL, 0, &info) == KOSDEC_SFD_INVALID);
    free(extra);
    input[4] ^= 1;
    REQUIRE(kosdec_sfd_video_extract(input, n, NULL, 0, &info) == KOSDEC_SFD_UNSUPPORTED);
    input[4] ^= 1;
    /* Truncation within an actual packet, not just missing final padding. */
    REQUIRE(kosdec_sfd_video_extract(input, 30, NULL, 0, &info) == KOSDEC_SFD_TRUNCATED);
    struct kosdec_sfd_mpeg_stats stats;
    REQUIRE(kosdec_sfd_mpeg_decode(video, bytes, 360, NULL, NULL, NULL, &stats) == KOSDEC_SFD_MPEG_LIMIT);
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/reference.yuv", argv[1]) >= (int)sizeof(path)) return 2;
    struct frames frames = {0};
    frames.dir = argv[1];
    frames.reference = fopen(path, "rb");
    if(!frames.reference) return 2;
    enum kosdec_sfd_mpeg_status status = kosdec_sfd_mpeg_decode_movie(video, bytes, 360, NULL, compare_frame, &frames, &stats);
    REQUIRE(status == KOSDEC_SFD_MPEG_OK && stats.frames == 360 && frames.count == 360);
    REQUIRE(fgetc(frames.reference) == EOF);
    fclose(frames.reference);
    size_t needed = kosdec_sfd_mpeg_movie_workspace_size(), alignment = kosdec_sfd_mpeg_movie_workspace_alignment();
    size_t allocated = (needed + alignment - 1) / alignment * alignment + 2 * alignment;
    uint8_t *backing = aligned_alloc(alignment, allocated);
    if(!backing) return 2;
    void *workspace = backing + alignment;
    memset(backing, 0xa5, allocated);
    REQUIRE(kosdec_sfd_mpeg_decode_movie_workspace(video, bytes, 360, workspace, needed - 1,
            NULL, NULL, NULL, &stats) == KOSDEC_SFD_MPEG_INVALID);
    REQUIRE(kosdec_sfd_mpeg_decode_movie_workspace(video, bytes, 360, (uint8_t *)workspace + 1, needed,
            NULL, NULL, NULL, &stats) == KOSDEC_SFD_MPEG_INVALID);
    for(size_t i = 0; i < allocated; ++i) REQUIRE(backing[i] == 0xa5);
    REQUIRE(kosdec_sfd_mpeg_decode_movie_workspace(video, bytes, 360, workspace, needed,
            NULL, stop_frame, NULL, &stats) == KOSDEC_SFD_MPEG_CALLBACK);
    REQUIRE(stats.frames == 0);
    struct frames reused = {.dir = argv[1]};
    reused.reference = fopen(path, "rb");
    if(!reused.reference) return 2;
    REQUIRE(kosdec_sfd_mpeg_decode_movie_workspace(video, bytes, 360, workspace, needed,
            NULL, compare_frame, &reused, &stats) == KOSDEC_SFD_MPEG_OK);
    REQUIRE(reused.count == 360 && reused.hash == frames.hash);
    REQUIRE(fgetc(reused.reference) == EOF);
    fclose(reused.reference);
    for(size_t i = 0; i < alignment; ++i) REQUIRE(backing[i] == 0xa5);
    for(size_t i = alignment + needed; i < allocated; ++i) REQUIRE(backing[i] == 0xa5);
    printf("KOSDEC_SFD_WORKSPACE size=%lu alignment=%lu parity_hash=%016llx reuse=PASS\n",
           (unsigned long)needed, (unsigned long)alignment, (unsigned long long)reused.hash);
    free(backing);
    printf("KOSDEC_SFD_COMPLETE video_bytes=%lu frames=%lu blocks=%lu work=%lu arena=%lu "
           "max_pixel=%u mae=%.9f failures=%u result=%s VIDEO_ONLY\n", (unsigned long)bytes,
           (unsigned long)stats.frames, (unsigned long)stats.blocks, (unsigned long)stats.work,
           (unsigned long)stats.allocated, frames.max, frames.sum / (360.0 * KOSDEC_SFD_MPEG_FRAME_BYTES),
           failures, failures ? "FAIL" : "PASS");
    free(video); free(input); free(oracle);
    return failures ? 1 : 0;
}
#endif
