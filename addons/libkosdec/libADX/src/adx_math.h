/* Original bounded ADX arithmetic candidate. Private ABI, not a parser. */
#ifndef MEDIA_ADX_MATH_H
#define MEDIA_ADX_MATH_H
#include <stdint.h>

/* Integer rate 1..48000 Hz; cutoff 0..floor(rate/2). Invalid/null: -1,
   no write. Success: 0. Certified Q32 boundaries select exact mathematical Q12
   truncations; see COEFFICIENTS.md and tests/generate_setup.py. */
int media_adx_coefficients(unsigned rate, unsigned cutoff, int16_t q12[2]);

/* One channel of one fixed logical group, frames 1..32, stride 1 or 2.
   q12[0] in [0,8192], q12[1] in [-4096,0]; block scale high bit clear.
   history[0] is latest PCM16; history[1] is preceding. Float Q16 histories
   persist inside this call, quantized only on return. Process no padding.
   Returns output clip count >=0, or -1 before writes for invalid arguments.
   Required buffers are disjoint, element-aligned, and fully readable/writable;
   pcm capacity is 1+(frames-1)*stride elements. Caller validates both stereo
   channels before either call and keeps numerical boundaries independent of
   API fragments. Normal KOS FPU profile; XMTRX/controls preserved, FPUL and
   arithmetic flags scratch. No allocation, callback, yield or global state. */
int media_adx_channel_group(const uint8_t block[18], unsigned frames,
                            const int16_t q12[2], int16_t history[2],
                            int16_t *pcm, unsigned stride);

#ifdef MEDIA_ADX_MATH_TEST_INTERNAL
/* Test-only exposure of finite-domain conversion and native instruction. */
int16_t media_adx_test_pcm(float scaled, unsigned *clips);
float media_adx_test_mac(float a, float b, float c);
#endif
#endif
