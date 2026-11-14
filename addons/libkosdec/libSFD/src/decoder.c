/* Original bounded fixture adapter; upstream changes are listed in PROVENANCE. */
#include <kosdec/sfd_mpeg.h>
#include "idct.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <sh4zam/shz_scalar.h>

/* Syntax levels/scales enter numeric processing once; bounded products are
   exactly representable in binary32. Keep negative floor and oddification. */
static float media_mpeg_dequantize(float level, unsigned scale, unsigned weight, int intra) {
    float value = level * 2.0f;
    if(!intra) value += value < 0.0f ? -1.0f : 1.0f;
    value = shz_floorf(value * (float)scale * (float)weight * 0.0625f);
    if(value == 2.0f * shz_floorf(value * 0.5f))
        value -= value > 0.0f ? 1.0f : -1.0f;
    /* Syntax bounds guarantee finite values; avoid strict-mode libm min/max. */
    if(value < -2048.0f) value = -2048.0f;
    if(value > 2047.0f) value = 2047.0f;
    return value;
}
static float media_mpeg_average2(float a, float b) {
    return shz_floorf((a + b + 1.0f) * 0.5f);
}
static float media_mpeg_average4(float a, float b, float c, float d) {
    return shz_floorf((a + b + c + d + 2.0f) * 0.25f);
}
static int media_mpeg_dc_coefficient(float predictor, int differential, float *out) {
    float value = (predictor + (float)differential) * 8.0f;
    if(!(value >= -2048.0f && value <= 2047.0f)) return -1;
    *out = value;
    return 0;
}
#ifdef MEDIA_MPEG_NUMERIC_TEST
float kosdec_sfd_mpeg_test_dequant(float level, unsigned scale, unsigned weight, int intra) {
    return media_mpeg_dequantize(level, scale, weight, intra);
}
float kosdec_sfd_mpeg_test_average2(float a, float b) { return media_mpeg_average2(a, b); }
float kosdec_sfd_mpeg_test_average4(float a, float b, float c, float d) { return media_mpeg_average4(a, b, c, d); }
int kosdec_sfd_mpeg_test_dc(float p, int d, float *out) { return media_mpeg_dc_coefficient(p, d, out); }
#endif

struct context {
    jmp_buf escape;
    struct kosdec_sfd_mpeg_stats stats;
    kosdec_sfd_mpeg_block_fn block;
    kosdec_sfd_mpeg_frame_fn frame;
    void *user;
    size_t frame_limit, work_limit;
    alignas(max_align_t) unsigned char arena[512 * 1024];
    uint8_t pixels[KOSDEC_SFD_MPEG_FRAME_BYTES];
};
static struct context *active;
static void media_mpeg_fail(enum kosdec_sfd_mpeg_status status) {
    active->stats.status = status;
    longjmp(active->escape, 1);
}
static void media_mpeg_tick(void) {
    if(++active->stats.work > active->work_limit)
        media_mpeg_fail(KOSDEC_SFD_MPEG_LIMIT);
}
static void *arena_alloc(size_t n) {
    const size_t a = alignof(max_align_t);
    size_t start = (active->stats.allocated + a - 1) & ~(a - 1);
    if(start > sizeof(active->arena) || n > sizeof(active->arena) - start)
        media_mpeg_fail(KOSDEC_SFD_MPEG_LIMIT);
    active->stats.allocated = start + n;
    return active->arena + start;
}
static void *arena_realloc(void *p, size_t n) {
    (void)p; (void)n;
    media_mpeg_fail(KOSDEC_SFD_MPEG_LIMIT);
    return NULL;
}
static void media_mpeg_transform(float *values) {
    float residual[64];
    unsigned nonzero = 0, ac = 0;
    if(++active->stats.blocks > active->frame_limit * 1800u)
        media_mpeg_fail(KOSDEC_SFD_MPEG_LIMIT);
    for(unsigned i = 0; i < 64; ++i) {
        if(!(values[i] >= -2048.0f && values[i] <= 2047.0f))
            media_mpeg_fail(KOSDEC_SFD_MPEG_COEFFICIENT);
        if(values[i]) { ++nonzero; if(i) ++ac; }
    }
    if(!nonzero) ++active->stats.zero_blocks;
    else if(!ac) ++active->stats.dc_blocks;
    else ++active->stats.general_blocks;
    if(kosdec_sfd_private_idct_batch_f32(values, residual, 1)) media_mpeg_fail(KOSDEC_SFD_MPEG_COEFFICIENT);
    if(active->block) {
        /* Legacy diagnostic callback only. Normal video never creates this
           integer copy; reconstruction consumes the float residuals below. */
        int16_t diagnostic[64];
        for(unsigned i = 0; i < 64; ++i) diagnostic[i] = (int16_t)residual[i];
        if(active->block(values, diagnostic, active->user)) media_mpeg_fail(KOSDEC_SFD_MPEG_CALLBACK);
    }
    memcpy(values, residual, sizeof(residual));
}
#define PLM_MALLOC(n) arena_alloc(n)
#define PLM_REALLOC(p,n) arena_realloc(p,n)
#define PLM_FREE(p) ((void)(p))
#define PLM_NO_STDIO
#define PL_MPEG_IMPLEMENTATION
#include "reference/pl_mpeg.h"

/* Header admission happens before ANY upstream allocation. This is a fixed
   fixture profile, not a general MPEG validator. Repeated identical sequence
   headers are allowed; altered headers and MPEG-2 extensions are rejected. */
static enum kosdec_sfd_mpeg_status admit(const uint8_t *p, size_t n, size_t expected,
                              struct kosdec_sfd_mpeg_stats *s, size_t byte_limit,
                              size_t frame_limit) {
    if(!p || !n || !expected) return KOSDEC_SFD_MPEG_INVALID;
    if(n > byte_limit || expected > frame_limit) return KOSDEC_SFD_MPEG_LIMIT;
    if(n < 16 || memcmp(p, "\0\0\1\xb3", 4)) return KOSDEC_SFD_MPEG_INVALID;
    if(memcmp(p + n - 4, "\0\0\1\xb7", 4)) return KOSDEC_SFD_MPEG_TRUNCATED;
    uint8_t sequence[136];
    size_t sequence_len = 0, pictures = 0;
    for(size_t i = 0; i + 4 <= n; ++i) {
        if(p[i] || p[i + 1] || p[i + 2] != 1) continue;
        unsigned code = p[i + 3];
        if(code == 0xb3) {
            if(i + 12 > n) return KOSDEC_SFD_MPEG_TRUNCATED;
            const uint8_t *h = p + i + 4;
            unsigned width = h[0] * 16u + (h[1] >> 4);
            unsigned height = (h[1] & 15u) * 256u + h[2];
            if(width != 320 || height != 240 || (h[3] & 15) != 5)
                return KOSDEC_SFD_MPEG_INVALID; /* Exact 30fps fixture profile. */
            /* Fixed default matrices, bitrate/VBV may repeat identically. */
            if((h[7] & 3u) != 0) return KOSDEC_SFD_MPEG_INVALID;
            if(!sequence_len) { memcpy(sequence, h, 8); sequence_len = 8; }
            else if(memcmp(sequence, h, sequence_len)) return KOSDEC_SFD_MPEG_INVALID;
        }
        else if(code == 0) {
            if(i + 6 > n) return KOSDEC_SFD_MPEG_TRUNCATED;
            unsigned type = (p[i + 5] >> 3) & 7;
            if(type < 1 || type > 3) return KOSDEC_SFD_MPEG_INVALID;
            ++s->pictures[type];
            if(++pictures > frame_limit) return KOSDEC_SFD_MPEG_LIMIT;
        }
        else if(code == 0xb5) return KOSDEC_SFD_MPEG_INVALID;
        else if(code == 0xb7 && i + 4 != n) return KOSDEC_SFD_MPEG_INVALID;
    }
    if(pictures != expected) return KOSDEC_SFD_MPEG_TRUNCATED;
    return KOSDEC_SFD_MPEG_OK;
}

static enum kosdec_sfd_mpeg_status decode_profile(const uint8_t *input, size_t bytes,
                            size_t expected_frames, kosdec_sfd_mpeg_block_fn block,
                            kosdec_sfd_mpeg_frame_fn frame, void *user,
                            struct kosdec_sfd_mpeg_stats *stats, size_t byte_limit,
                            size_t frame_limit, size_t work_limit, void *workspace) {
    if(!stats) return KOSDEC_SFD_MPEG_INVALID;
    memset(stats, 0, sizeof(*stats));
    if(active) return stats->status = KOSDEC_SFD_MPEG_BUSY;
    enum kosdec_sfd_mpeg_status status = admit(input, bytes, expected_frames, stats,
                                    byte_limit, frame_limit);
    if(status != KOSDEC_SFD_MPEG_OK) return stats->status = status;
    struct context *ctx = workspace ? workspace : calloc(1, sizeof(*ctx));
    if(!ctx) return stats->status = KOSDEC_SFD_MPEG_LIMIT;
    if(workspace) memset(ctx, 0, sizeof(*ctx));
    ctx->stats = *stats; ctx->block = block; ctx->frame = frame; ctx->user = user;
    ctx->frame_limit = frame_limit; ctx->work_limit = work_limit;
    active = ctx;
    if(!setjmp(ctx->escape)) {
        kosdec_sfd_private_plm_buffer_t *buffer = kosdec_sfd_private_plm_buffer_create_with_memory((uint8_t *)input, bytes, 0);
        kosdec_sfd_private_plm_video_t *video = kosdec_sfd_private_plm_video_create_with_buffer(buffer, 0);
        if(!video->has_sequence_header) media_mpeg_fail(KOSDEC_SFD_MPEG_TRUNCATED);
        kosdec_sfd_private_plm_frame_t *f;
        while((f = kosdec_sfd_private_plm_video_decode(video))) {
            if(ctx->stats.frames >= expected_frames) media_mpeg_fail(KOSDEC_SFD_MPEG_INVALID);
            if(f->width != 320 || f->height != 240) media_mpeg_fail(KOSDEC_SFD_MPEG_INVALID);
            memcpy(ctx->pixels, f->y.data, 320 * 240);
            memcpy(ctx->pixels + 320 * 240, f->cb.data, 160 * 120);
            memcpy(ctx->pixels + 320 * 240 + 160 * 120, f->cr.data, 160 * 120);
            if(frame && frame(ctx->pixels, ctx->stats.frames, user))
                media_mpeg_fail(KOSDEC_SFD_MPEG_CALLBACK);
            ++ctx->stats.frames;
        }
        if(ctx->stats.frames != expected_frames || !kosdec_sfd_private_plm_video_has_ended(video))
            media_mpeg_fail(KOSDEC_SFD_MPEG_TRUNCATED);
    }
    *stats = ctx->stats;
    active = NULL;
    if(!workspace) free(ctx);
    return stats->status;
}

enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode(const uint8_t *input, size_t bytes,
                            size_t frames, kosdec_sfd_mpeg_block_fn block,
                            kosdec_sfd_mpeg_frame_fn frame, void *user,
                            struct kosdec_sfd_mpeg_stats *stats) {
    return decode_profile(input, bytes, frames, block, frame, user, stats,
                          KOSDEC_SFD_MPEG_MAX_BYTES, KOSDEC_SFD_MPEG_MAX_FRAMES, 32u * KOSDEC_SFD_MPEG_MAX_BYTES, NULL);
}

enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode_movie(const uint8_t *input, size_t bytes,
                                  size_t frames, kosdec_sfd_mpeg_block_fn block,
                                  kosdec_sfd_mpeg_frame_fn frame, void *user,
                                  struct kosdec_sfd_mpeg_stats *stats) {
    return decode_profile(input, bytes, frames, block, frame, user, stats,
                          4u * 1024u * 1024u, 360, 384u * 1024u * 1024u, NULL);
}

size_t kosdec_sfd_mpeg_movie_workspace_size(void) { return sizeof(struct context); }
size_t kosdec_sfd_mpeg_movie_workspace_alignment(void) { return alignof(struct context); }
enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode_movie_workspace(const uint8_t *input, size_t bytes,
    size_t frames, void *workspace, size_t workspace_bytes,
    kosdec_sfd_mpeg_block_fn block, kosdec_sfd_mpeg_frame_fn frame, void *user, struct kosdec_sfd_mpeg_stats *stats) {
    if(!stats) return KOSDEC_SFD_MPEG_INVALID;
    if(!workspace || workspace_bytes < sizeof(struct context) ||
       (uintptr_t)workspace % alignof(struct context)) {
        memset(stats, 0, sizeof(*stats));
        return stats->status = KOSDEC_SFD_MPEG_INVALID;
    }
    return decode_profile(input, bytes, frames, block, frame, user, stats,
                          4u * 1024u * 1024u, 360, 384u * 1024u * 1024u, workspace);
}
