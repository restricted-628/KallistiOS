/* Private original DSP authoring; existing KOS owns hardware loading. */
#ifndef KOS_PRIVATE_DSP_BUILD_H
#define KOS_PRIVATE_DSP_BUILD_H
#include <dc/sound/dsp.h>
typedef struct {
    unsigned tra,twt,twa,xsel,ysel,ira,iwt,iwa;
    unsigned table,mwt,mrd,ewt,ewa,adrl,frcl,shift,yrl,negb,zero,bsel;
    unsigned nofl,masa,adreb,nxadr;
} aica_dsp_fields_t;
/* Fields are full-width, checked before packing. Invalid input leaves output
   unchanged. Memory controls (including TABLE/MASA/NOFL) require an odd step.
   This is structural encoding, not dataflow/hazard validation. */
int aica_dsp_encode(unsigned step,const aica_dsp_fields_t *,snd_dsp_instruction_t *);
/* Signed13-bit Q12 coefficient -> raw register; -4096..4095. Positive exact
   unity is not representable. No rounding, saturation or floating-point math. */
int aica_dsp_coefficient(int q12,uint16_t *result);
typedef struct {
    unsigned input; /* Native INPUTS address: MEMS0..31,MIXS32..47,EXTS48..49. */
    unsigned output; /* Effect return0..15, unique across routes. */
    int gain_q12;
} aica_dsp_gain_route_t;
/* Build complete native image,0..16 routes. Zero routes creates silence.
   Clears every unused output on every sample. Image never touches TEMP/MEMS
   or sound memory; existing loader still reserves its minimum16KiB work area.
   Builder is pure preparation, does not mutate live coefficients or load.
   Returns0 success,-1 invalid; result unchanged on rejection. */
int aica_dsp_build_gain(snd_dsp_program_t *,const aica_dsp_gain_route_t *,unsigned count);
#endif
