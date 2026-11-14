#include "dsp_effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static int64_t div_floor(int64_t n,int64_t d) { return n>=0?n/d:-((-n+d-1)/d); }
static int32_t clip(int64_t n) { return (int32_t)(n>8388607?8388607:(n < -8388608?-8388608:n)); }
static int32_t multiply(int32_t n,int coefficient) { return (int32_t)div_floor((int64_t)n*coefficient,4096); }
typedef struct {
    int16_t memory[65536];int32_t temporary[128],input_memory[32],output[16];
    unsigned counter;
} model_t;
/* Original selected-subset scheduling model. NOT a hardware emulator. Decode
   fields independently and reject operations outside the emitted subset. */
static void sample(model_t *m,const snd_dsp_program_t *p,const int32_t *inputs) {
    int64_t accumulator=0;
    int32_t yregister=0,pending[4]={0};
    unsigned step,mask=(8192u<<(unsigned)p->ring_size)-1;
    for(step=0;step<128;step++) {
        const uint16_t *w=p->steps[step].word;
        unsigned ira=(w[1]>>7)&63,tra=(w[0]>>9)&127;
        unsigned ysel=(w[1]>>13)&3;
        int32_t input=ira<32?m->input_memory[ira]:inputs[ira];
        int32_t temporary=m->temporary[(m->counter+tra)&127];
        int64_t b=(w[2]&2)?0:((w[2]&1)?accumulator:temporary);
        int coefficient=p->coefficients[step];
        int32_t shifted=clip(accumulator),x=(w[1]&0x8000)?input:temporary;
        CHECK(ira<=49 && (ysel==1 || ysel==2) && !(w[2]&0x80f0));
        CHECK(!(w[3]&0x01ff) && !(w[0]&1) && !(w[1]&1));
        if(coefficient>=32768) coefficient-=65536;
        coefficient=ysel==1?coefficient/8:(int)div_floor(yregister,2048);
        if(w[1]&0x40) m->input_memory[(w[1]>>1)&31]=pending[step&3];
        if(w[2]&8) yregister=input;
        if(w[2]&4) b=-b;
        accumulator=div_floor((int64_t)x*coefficient,4096)+b;
        if(w[0]&0x100) m->temporary[(m->counter+((w[0]>>1)&127))&127]=shifted;
        if(w[2]&0x6000) {
            unsigned address=(p->addresses[(w[3]>>9)&63]+m->counter)&mask;
            CHECK((step&1) && (w[3]&0x8000));
            if(w[2]&0x2000) pending[(step+2)&3]=(int32_t)m->memory[address]*256;
            if(w[2]&0x4000) m->memory[address]=(int16_t)div_floor(shifted,256);
        }
        if(w[2]&0x1000) m->output[(w[2]>>8)&15]=(int32_t)div_floor(shifted,256);
    }
    m->counter=(m->counter-1)&mask;
}
static uint32_t rng=0x9188abcd;
static uint32_t random_word(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void delay_case(aica_dsp_delay_t *parameters,unsigned frames) {
    snd_dsp_program_t image;
    model_t *model=calloc(1,sizeof(*model));
    int16_t *history[4]={0};unsigned position[4]={0},i,frame;
    int32_t inputs[50]={0};size_t work;
    CHECK(model!=NULL && aica_dsp_build_delay(&image,parameters)==0);
    CHECK(snd_dsp_program_validate(&image,&work)==0 && work==(16384u<<(unsigned)parameters->ring));
    for(i=0;i<parameters->count;i++) { history[i]=calloc(parameters->line[i].delay_samples,sizeof(int16_t));CHECK(history[i]!=NULL); }
    for(frame=0;frame<frames;frame++) {
        int32_t wet=0,expected,input;
        /* Impulses, silence, full scale clipping and signed deterministic noise. */
        if(frame<16) input=frame==0?4000000:0;
        else if(frame%997==0) input=-8388608;
        else if(frame%991==0) input=8388352;
        else input=((int32_t)(random_word()&0x7fff)-16384)*256;
        inputs[parameters->input]=input;
        for(i=0;i<parameters->count;i++) {
            const aica_dsp_delay_line_t *line=parameters->line+i;
            int32_t tap=(int32_t)history[i][position[i]]*256;
            int32_t next=clip((int64_t)input+multiply(tap,line->feedback_q12));
            wet=clip((int64_t)wet+multiply(tap,line->wet_q12));
            history[i][position[i]]=(int16_t)div_floor(next,256);
            if(++position[i]==line->delay_samples) position[i]=0;
        }
        expected=(int32_t)div_floor(clip((int64_t)wet+multiply(input,parameters->dry_q12)),256);
        sample(model,&image,inputs);
        for(i=0;i<16;i++) CHECK(model->output[i]==(i==parameters->output?expected:0));
    }
    for(i=0;i<parameters->count;i++) free(history[i]);free(model);
}
static void delays(void) {
    unsigned ring,count,i;
    aica_dsp_delay_t p={32,7,SND_DSP_RING_16K,1024,1,{{1,0,2048}}};
    snd_dsp_program_t image,old;
    for(ring=0;ring<4;ring++) for(count=1;count<=4;count++) {
        p.ring=(snd_dsp_ring_size_t)ring;p.count=count;
        p.input=32+(ring*4+count)%18;p.output=(ring*4+count)%16;
        for(i=0;i<count;i++) p.line[i]=(aica_dsp_delay_line_t){i*13+1,i%2?-3500:3072,i%2?-1024:512};
        delay_case(&p,256);
    }
    /* Exercise moving-segment interference and every ring wrap boundary. */
    for(ring=0;ring<4;ring++) {
        p.ring=(snd_dsp_ring_size_t)ring;p.count=4;
        for(i=0;i<4;i++) p.line[i]=(aica_dsp_delay_line_t){((8192u<<ring)/4)-1,2048,512};
        delay_case(&p,(8192u<<ring)+8);
    }
    p.count=1;p.line[0]=(aica_dsp_delay_line_t){65535,-4095,4095};delay_case(&p,65540);
    CHECK(aica_dsp_build_reverb(&image,32,15)==0);
    CHECK(image.addresses[0]==1493 && image.addresses[2]==4096+2111 &&
          image.addresses[4]==8192+2633 && image.addresses[6]==12288+3251);
    {
        snd_dsp_program_t equivalent;
        aica_dsp_delay_t reverb={32,15,SND_DSP_RING_32K,0,4,
            {{1493,2750,512},{2111,2680,512},{2633,2620,512},{3251,2550,512}}};
        CHECK(aica_dsp_build_delay(&equivalent,&reverb)==0);
        CHECK(memcmp(&image,&equivalent,sizeof(image))==0);
        delay_case(&reverb,20000);
    }
    old=image;
    p.line[0].delay_samples=65536;
    CHECK(aica_dsp_build_delay(&image,&p)<0 && memcmp(&image,&old,sizeof(image))==0);
    p.line[0].delay_samples=0;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.line[0].delay_samples=1;p.line[0].feedback_q12=4096;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.line[0].feedback_q12=-4096;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.line[0].feedback_q12=0;p.line[0].wet_q12=4096;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.line[0].wet_q12=0;p.ring=(snd_dsp_ring_size_t)-1;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.ring=SND_DSP_RING_16K;p.count=0;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.count=5;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.count=1;p.input=31;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.input=50;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.input=32;p.output=16;CHECK(aica_dsp_build_delay(&image,&p)<0);
    p.output=0;p.dry_q12=4096;CHECK(aica_dsp_build_delay(&image,&p)<0);
    CHECK(aica_dsp_build_delay(NULL,&p)<0 && aica_dsp_build_delay(&image,NULL)<0);
    CHECK(memcmp(&image,&old,sizeof(image))==0);
}
static void modulation(void) {
    snd_dsp_program_t p,old;model_t *m=calloc(1,sizeof(*m));int32_t inputs[50]={0};unsigned i,j;
    CHECK(m!=NULL);
    for(i=0;i<2048;i++) {
        unsigned carrier=i%50,modulator=(i+1)%50,output=i%16;
        CHECK(aica_dsp_build_modulation(&p,carrier,modulator,output)==0);
        inputs[carrier]=((int32_t)(random_word()&65535)-32768)*256;
        inputs[modulator]=((int32_t)(random_word()&65535)-32768)*256;
        if(carrier<32) m->input_memory[carrier]=inputs[carrier];
        if(modulator<32) m->input_memory[modulator]=inputs[modulator];
        sample(m,&p,inputs);
        int32_t expected=(int32_t)div_floor(clip(multiply(inputs[carrier],
            (int)div_floor(inputs[modulator],2048))),256);
        for(j=0;j<16;j++) CHECK(m->output[j]==(j==output?expected:0));
    }
    old=p;CHECK(aica_dsp_build_modulation(&p,50,0,0)<0);
    CHECK(aica_dsp_build_modulation(&p,0,50,0)<0);
    CHECK(aica_dsp_build_modulation(&p,0,0,16)<0);
    CHECK(aica_dsp_build_modulation(NULL,0,0,0)<0);
    CHECK(memcmp(&p,&old,sizeof(p))==0);free(m);
}
int main(void) { delays();modulation();printf("dsp effect checks: %lu\n",checks);return 0; }
