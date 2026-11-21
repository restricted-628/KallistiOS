/* Original finite headerless ADX body publisher for a serialized live ring. */
#ifndef KOSDEC_ADX_SFA_H
#define KOSDEC_ADX_SFA_H
#include <kosdec/adx.h>
#include <kosdec/adx_joint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_joint_reader;
struct kosdec_adx_sfa {
    uint32_t magic, groups, published_groups;
    unsigned channels, header_used, footer_used;
    struct kosdec_adx_joint *joint;
    struct kosdec_adx_joint_reader *live_reader;
    uint8_t header[KOSDEC_ADX_SFA_HEADER_BYTES];
    bool done, length_known;
};
/* Initialize before use on an empty, unsealed ring with no borrows and capacity
   >=36. Writer exclusively owns FREE publication; a joint reader may consume
   DATA between calls, all under the same serialized producer (e.g. feed hook).
   State/joint/backing/input are disjoint and remain alive until users join.
   Known total_frames includes zero groups inserted below. Prefix fixed500Hz,
   version3, encoding3/block18/4-bit. No compressed payload or encoder is supplied.
   DONE on success; invalid input leaves writer and joint unchanged. No reinit of
   an active writer; cancellation requires joining readers and resetting joint.
   This finite contract does not admit unknown-length SFD audio or live rewrites. */
enum kosdec_adx_status kosdec_adx_sfa_init(struct kosdec_adx_sfa *,
    struct kosdec_adx_joint *, unsigned channels, uint32_t rate, uint32_t total_frames);
/* Pair a fresh joint reader (before its first read) with a writer on its empty
   ring. Same serialized producer and backing-storage lifetime as finite SFA.
   The reader and writer must stay paired until their users are joined. Do not
   close, reset or reinitialize either side while the producer is active.
   Supported with or without decode jobs on the reader. Configure both
   before publication. The ring must meet the reader's job-mode requirements.
   Publishes an SFA placeholder header. While awaiting length, only whole groups
   may be submitted, up to floor(KOSDEC_ADX_MAX_FRAMES/32). */
enum kosdec_adx_status kosdec_adx_sfa_init_live(struct kosdec_adx_sfa *,
    struct kosdec_adx_joint_reader *, unsigned channels, uint32_t rate);
/* Set the authoritative final frame count once, including inserted groups.
   Call on the paired producer BEFORE submitting a partial final group. Rejects
   counts below the number of already published whole frames and counts above
   the codec limit. May run before the header is consumed. The writer then
   requires the exact remaining body groups before emitting footer and seal.
   DONE on success; invalid arguments leave both objects unchanged. */
enum kosdec_adx_status kosdec_adx_sfa_set_length(struct kosdec_adx_sfa *,
    uint32_t total_frames);
/* Each step publishes <=512 bytes. pump emits remaining prefix, then MORE while
   body groups remain, then the18-byte endcode and checked seal. NEED_OUTPUT is
   backpressure; DONE means validated framing was published, not decoded/played.
   Header/footer may span ring wraps; body groups are published atomically. */
enum kosdec_adx_status kosdec_adx_sfa_pump(struct kosdec_adx_sfa *);
/* Only after prefix publication; otherwise NEED_OUTPUT with *accepted=0.
   groups counts complete18*channels-byte compressed groups. Accept up to the
   free space, remaining declared groups and512-byte budget. Validate admitted
   group scale fields before any mutation; INVALID leaves output unchanged.
   Remaining caller data is not read. Nonzero groups needs nonnull input; no
   retained input pointer. MORE on progress; NEED_OUTPUT on no space. Requesting
   beyond the declared remaining count is BAD_ARGUMENT, never silent truncation.
   Insert supplies whole zero residual groups through the same length accounting;
   predictor history remains live, so it need not produce immediate PCM silence.
   *accepted is unchanged on errors and disjoint from all other objects. */
enum kosdec_adx_status kosdec_adx_sfa_write(struct kosdec_adx_sfa *,
    const uint8_t *body, uint32_t groups, uint32_t *accepted);
enum kosdec_adx_status kosdec_adx_sfa_insert(struct kosdec_adx_sfa *,
    uint32_t groups, uint32_t *accepted);
#ifdef __cplusplus
}
#endif
#endif
