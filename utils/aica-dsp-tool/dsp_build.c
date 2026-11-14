#include "dsp_build.h"
int aica_dsp_encode(unsigned step,const aica_dsp_fields_t *f,snd_dsp_instruction_t *out) {
    snd_dsp_instruction_t result;
    if(!f || !out || step>=SND_DSP_STEP_COUNT) return -1;
#define RANGE(field,maximum) if(f->field>(maximum)) return -1
    RANGE(tra,127);RANGE(twt,1);RANGE(twa,127);RANGE(xsel,1);
    RANGE(ysel,3);RANGE(ira,49);RANGE(iwt,1);RANGE(iwa,31);
    RANGE(table,1);RANGE(mwt,1);RANGE(mrd,1);RANGE(ewt,1);RANGE(ewa,15);
    RANGE(adrl,1);RANGE(frcl,1);RANGE(shift,3);RANGE(yrl,1);
    RANGE(negb,1);RANGE(zero,1);RANGE(bsel,1);RANGE(nofl,1);
    RANGE(masa,63);RANGE(adreb,1);RANGE(nxadr,1);
#undef RANGE
    if(!(step&1u) && (f->table || f->mwt || f->mrd || f->nofl || f->masa ||
                      f->adreb || f->nxadr)) return -1;
    result.word[0]=(uint16_t)((f->tra<<9)|(f->twt<<8)|(f->twa<<1));
    result.word[1]=(uint16_t)((f->xsel<<15)|(f->ysel<<13)|(f->ira<<7)|(f->iwt<<6)|(f->iwa<<1));
    result.word[2]=(uint16_t)((f->table<<15)|(f->mwt<<14)|(f->mrd<<13)|(f->ewt<<12)|
        (f->ewa<<8)|(f->adrl<<7)|(f->frcl<<6)|(f->shift<<4)|(f->yrl<<3)|
        (f->negb<<2)|(f->zero<<1)|f->bsel);
    result.word[3]=(uint16_t)((f->nofl<<15)|(f->masa<<9)|(f->adreb<<8)|(f->nxadr<<7));
    *out=result;return 0;
}
int aica_dsp_coefficient(int q12,uint16_t *result) {
    if(!result || q12 < -4096 || q12>4095) return -1;
    *result=(uint16_t)(q12*8);return 0;
}
int aica_dsp_build_gain(snd_dsp_program_t *out,const aica_dsp_gain_route_t *routes,unsigned count) {
    snd_dsp_program_t image;
    aica_dsp_fields_t idle={0};
    unsigned used=0,i,step=0;
    if(!out || count>16 || (count && !routes)) return -1;
    for(i=0;i<count;i++) {
        if(routes[i].input>49 || routes[i].output>15 || routes[i].gain_q12 < -4096 ||
           routes[i].gain_q12>4095 || (used & (1u<<routes[i].output))) return -1;
        used|=1u<<routes[i].output;
    }
    snd_dsp_program_init(&image);
    /* Explicit zero coefficient and ZERO input avoid relying on TEMP/FRC state.
       An all-zero instruction is arithmetic, not a side-effect-free NOP. */
    idle.ysel=1;idle.zero=1;
    for(i=0;i<SND_DSP_STEP_COUNT;i++) aica_dsp_encode(i,&idle,&image.steps[i]);
    for(i=0;i<count;i++) {
        aica_dsp_fields_t multiply=idle,write=idle;
        multiply.xsel=1;multiply.ira=routes[i].input;
        aica_dsp_encode(step,&multiply,&image.steps[step]);
        aica_dsp_coefficient(routes[i].gain_q12,&image.coefficients[step]);++step;
        write.ewt=1;write.ewa=routes[i].output;
        aica_dsp_encode(step,&write,&image.steps[step]);++step;
    }
    /* The preceding write computes zero for the next step. With no routes,
       explicitly prime ACC before the first output write. */
    if(!step) ++step;
    for(i=0;i<16;i++) if(!(used&(1u<<i))) {
        aica_dsp_fields_t write=idle;write.ewt=1;write.ewa=i;
        aica_dsp_encode(step,&write,&image.steps[step]);++step;
    }
    if(snd_dsp_program_validate(&image,NULL)<0) return -1;
    *out=image;return 0;
}
