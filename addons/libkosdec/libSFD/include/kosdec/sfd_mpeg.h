/* Experimental bounded MPEG-1 video decoder; see libSFD/README.md. */
#ifndef KOSDEC_SFD_MPEG_H
#define KOSDEC_SFD_MPEG_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_SFD_MPEG_MAX_BYTES (2u * 1024u * 1024u)
#define KOSDEC_SFD_MPEG_MAX_FRAMES 12u
#define KOSDEC_SFD_MPEG_FRAME_BYTES (320u * 240u * 3u / 2u)
enum kosdec_sfd_mpeg_status { KOSDEC_SFD_MPEG_OK, KOSDEC_SFD_MPEG_INVALID, KOSDEC_SFD_MPEG_TRUNCATED, KOSDEC_SFD_MPEG_LIMIT,
                   KOSDEC_SFD_MPEG_COEFFICIENT, KOSDEC_SFD_MPEG_CALLBACK, KOSDEC_SFD_MPEG_BUSY };
struct kosdec_sfd_mpeg_stats {
    size_t frames, pictures[4], blocks, zero_blocks, dc_blocks, general_blocks;
    size_t allocated, work;
    enum kosdec_sfd_mpeg_status status;
};
typedef int (*kosdec_sfd_mpeg_block_fn)(const float *, const int16_t *, void *);
typedef int (*kosdec_sfd_mpeg_frame_fn)(const uint8_t *, size_t, void *);
/* Complete immutable input, synchronous callbacks, no reentry/thread sharing.
   expected_frames must equal the elementary stream's picture count. */
enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode(const uint8_t *input, size_t bytes,
                            size_t expected_frames, kosdec_sfd_mpeg_block_fn block,
                            kosdec_sfd_mpeg_frame_fn frame, void *user,
                            struct kosdec_sfd_mpeg_stats *stats);
/* Explicit video-only example profile: <=4MiB, <=360 pictures. Same format,
   synchronous ownership and numerical contract; does not change kosdec_sfd_mpeg_decode. */
enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode_movie(const uint8_t *input, size_t bytes,
                                  size_t expected_frames, kosdec_sfd_mpeg_block_fn block,
                                  kosdec_sfd_mpeg_frame_fn frame, void *user,
                                  struct kosdec_sfd_mpeg_stats *stats);
size_t kosdec_sfd_mpeg_movie_workspace_size(void);
size_t kosdec_sfd_mpeg_movie_workspace_alignment(void);
#ifdef MEDIA_MPEG_NUMERIC_TEST
/* Test access to the actual producer helpers, not separate model code. */
float kosdec_sfd_mpeg_test_dequant(float level, unsigned scale, unsigned weight, int intra);
float kosdec_sfd_mpeg_test_average2(float a, float b);
float kosdec_sfd_mpeg_test_average4(float a, float b, float c, float d);
int kosdec_sfd_mpeg_test_dc(float p, int d, float *out);
#endif
/* Caller owns the whole aligned writable span through return. The decoder
   initializes it, never frees/remaps it, and retains no pointer after ordinary
   return (including callback failure). Input, stats and callback-owned objects
   must be disjoint from it: opaque callback spans are a caller obligation,
   not runtime overlap validation. No foreign longjmp or concurrent callers.
   Platform mapping, OIX and context lifetime remain entirely caller-owned. */
enum kosdec_sfd_mpeg_status kosdec_sfd_mpeg_decode_movie_workspace(const uint8_t *input, size_t bytes,
    size_t expected_frames, void *workspace, size_t workspace_bytes,
    kosdec_sfd_mpeg_block_fn block, kosdec_sfd_mpeg_frame_fn frame, void *user, struct kosdec_sfd_mpeg_stats *stats);
#ifdef __cplusplus
}
#endif
#endif
