#include "dsp_effects.h"
static aica_dsp_fields_t idle_fields(void) {
    aica_dsp_fields_t f={0};f.ysel=1;f.zero=1;return f;
}
static void initialize(snd_dsp_program_t *image) {
    unsigned step;aica_dsp_fields_t idle=idle_fields();
    snd_dsp_program_init(image);
    for(step=0;step<128;step++) aica_dsp_encode(step,&idle,&image->steps[step]);
}
static void put(snd_dsp_program_t *image,unsigned step,aica_dsp_fields_t f,int coefficient) {
    aica_dsp_encode(step,&f,&image->steps[step]);
    aica_dsp_coefficient(coefficient,&image->coefficients[step]);
}
static void clear_other_outputs(snd_dsp_program_t *image,unsigned step,unsigned output) {
    unsigned i;
    for(i=0;i<16;i++) if(i!=output) {
        aica_dsp_fields_t f=idle_fields();f.ewt=1;f.ewa=i;put(image,step++,f,0);
    }
}
int aica_dsp_build_delay(snd_dsp_program_t *out,const aica_dsp_delay_t *p) {
    snd_dsp_program_t image;
    unsigned i,segment,step;
    aica_dsp_fields_t f;
    if(!out || !p || p->input<32 || p->input>49 || p->output>15 ||
       (unsigned)p->ring>3 || !p->count || p->count>4 ||
       p->dry_q12 < -4096 || p->dry_q12>4095) return -1;
    segment=(8192u<<(unsigned)p->ring)/p->count;
    for(i=0;i<p->count;i++) if(!p->line[i].delay_samples ||
       p->line[i].delay_samples>=segment || p->line[i].feedback_q12 < -4095 ||
       p->line[i].feedback_q12>4095 || p->line[i].wet_q12 < -4096 ||
       p->line[i].wet_q12>4095) return -1;
    initialize(&image);image.ring_size=p->ring;
    for(i=0;i<p->count;i++) {
        unsigned base=i*10;
        image.addresses[2*i]=(uint16_t)(i*segment+p->line[i].delay_samples);
        image.addresses[2*i+1]=(uint16_t)(i*segment);
        f=idle_fields();f.mrd=1;f.nofl=1;f.masa=2*i;put(&image,base+1,f,0);
        f=idle_fields();f.iwt=1;f.iwa=i;put(&image,base+3,f,0);
        /* Exact -input followed by subtract-ACC makes +input without inventing
           an unrepresentable +1 coefficient. ACC keeps its26-bit headroom. */
        f=idle_fields();f.xsel=1;f.ira=p->input;put(&image,base+4,f,-4096);
        f=idle_fields();f.xsel=1;f.ira=i;f.zero=0;f.bsel=1;f.negb=1;
        put(&image,base+5,f,p->line[i].feedback_q12);
        f=idle_fields();f.zero=0;f.bsel=1;put(&image,base+6,f,0);
        /* The write consumes the preceding sum. In parallel form the wet tap
           plus prior lines, then latch the saturated sum one step later. */
        f=idle_fields();f.mwt=1;f.nofl=1;f.masa=2*i+1;
        f.xsel=1;f.ira=i;f.zero=i?0u:1u;f.tra=0;
        put(&image,base+7,f,p->line[i].wet_q12);
        f=idle_fields();f.twt=1;f.twa=0;put(&image,base+8,f,0);
    }
    step=p->count*10;f=idle_fields();f.xsel=1;f.ira=p->input;f.zero=0;f.tra=0;
    put(&image,step++,f,p->dry_q12);
    f=idle_fields();f.ewt=1;f.ewa=p->output;put(&image,step++,f,0);
    clear_other_outputs(&image,step,p->output);
    if(snd_dsp_program_validate(&image,NULL)<0) return -1;
    *out=image;return 0;
}
int aica_dsp_build_reverb(snd_dsp_program_t *out,unsigned input,unsigned output) {
    aica_dsp_delay_t preset={input,output,SND_DSP_RING_32K,0,4,
        {{1493,2750,512},{2111,2680,512},{2633,2620,512},{3251,2550,512}}};
    return aica_dsp_build_delay(out,&preset);
}
int aica_dsp_build_modulation(snd_dsp_program_t *out,unsigned carrier,
                              unsigned modulation,unsigned output) {
    snd_dsp_program_t image;aica_dsp_fields_t f;
    if(!out || carrier>49 || modulation>49 || output>15) return -1;
    initialize(&image);
    f=idle_fields();f.xsel=1;f.ira=modulation;f.yrl=1;put(&image,0,f,0);
    f=idle_fields();f.xsel=1;f.ira=carrier;f.ysel=2;put(&image,1,f,0);
    f=idle_fields();f.ewt=1;f.ewa=output;put(&image,2,f,0);
    clear_other_outputs(&image,3,output);
    if(snd_dsp_program_validate(&image,NULL)<0) return -1;
    *out=image;return 0;
}
