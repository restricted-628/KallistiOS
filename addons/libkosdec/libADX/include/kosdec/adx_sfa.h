/* Original finite headerless ADX body publisher for a serialized live ring. */
#ifndef KOSDEC_ADX_SFA_H
#define KOSDEC_ADX_SFA_H
#include <kosdec/adx.h>
#include <kosdec/adx_joint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_sfa {
    uint32_t magic, groups, published_groups;
    unsigned channels, header_used, footer_used;
    struct kosdec_adx_joint *joint;
    uint8_t header[KOSDEC_ADX_SFA_HEADER_BYTES];
    bool done;
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
