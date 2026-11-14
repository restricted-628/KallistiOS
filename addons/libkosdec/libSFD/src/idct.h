/* Original libSFD IDCT. Private implementation interface. */
#ifndef KOSDEC_SFD_PRIVATE_IDCT_H
#define KOSDEC_SFD_PRIVATE_IDCT_H
#include <stddef.h>
#include <stdint.h>

/* Natural integral coefficients [-2048,2047], at most six 8x8 blocks.
   Disjoint element-aligned buffers. No scaling, level shift or pixel clamp.
   Normal KOS FPU profile only. Synchronous; preserves caller XMTRX. */
int kosdec_sfd_private_idct_batch(const float *coeff, int16_t *residual, size_t count);
/* Same transform/domain/state contract; both buffers need float alignment.
   Integral-valued float output, legacy binary32 signed-half bias followed by
   truncation, zero canonicalized positive. No int16 intermediate buffer. */
int kosdec_sfd_private_idct_batch_f32(const float *coeff, float *residual, size_t count);
#endif
