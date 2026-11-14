/* Original private effects; load through the existing KOS DSP owner. */
#ifndef KOS_PRIVATE_DSP_EFFECTS_H
#define KOS_PRIVATE_DSP_EFFECTS_H
#include "dsp_build.h"
typedef struct {
    unsigned delay_samples;
    int feedback_q12; /* -4095..4095: absolute feedback strictly below unity. */
    int wet_q12; /* -4096..4095. */
} aica_dsp_delay_line_t;
typedef struct {
    unsigned input,output; /* INPUTS32..49 (MIXS/EXTS), return0..15. */
    snd_dsp_ring_size_t ring;
    int dry_q12; /* -4096..4095;0 for a wet-only return. */
    unsigned count; /*1..4 parallel lines; each has its own ring segment. */
    aica_dsp_delay_line_t line[4];
} aica_dsp_delay_t;
/* PCM16 linear delay memory (NOFL=1). Each line delay is1..segment_words-1;
   segment_words=(8192<<ring)/count. Needs zeroed work RAM on initial load.
   Uses TEMP0 and MEMS0..count-1, all reserved by the existing complete DSP image.
   Pure preparation: output unchanged on rejection, no live work/parameter edits.
   Mathematical feedback bound does not eliminate quantization limit cycles. */
int aica_dsp_build_delay(snd_dsp_program_t *,const aica_dsp_delay_t *);
/* Original fixed four-comb wet-only preset; not a vendor preset match.
   Ring32KiB;1493/2111/2633/3251 sample delays, mild positive feedback. */
int aica_dsp_build_reverb(snd_dsp_program_t *,unsigned input,unsigned output);
/* Multiply carrier by signed modulation input scaled to13-bit Q12 via Y_REG.
   Latch -> multiply -> write on successive steps. A caller-provided unipolar
   control gives tremolo; bipolar control gives amplitude/ring modulation.
   Both inputs0..49; no autonomous oscillator or pitch/chorus behavior. */
int aica_dsp_build_modulation(snd_dsp_program_t *,unsigned carrier,
                              unsigned modulation,unsigned output);
#endif
