#include "dsp_build.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
typedef struct { size_t offset;unsigned width,word,shift,maximum,memory; } field_t;
#define F(n,w,word,shift,max,mem) {offsetof(aica_dsp_fields_t,n),w,word,shift,max,mem}
static const field_t fields[]={
 F(tra,7,0,9,127,0),F(twt,1,0,8,1,0),F(twa,7,0,1,127,0),
 F(xsel,1,1,15,1,0),F(ysel,2,1,13,3,0),F(ira,6,1,7,49,0),F(iwt,1,1,6,1,0),F(iwa,5,1,1,31,0),
 F(table,1,2,15,1,1),F(mwt,1,2,14,1,1),F(mrd,1,2,13,1,1),F(ewt,1,2,12,1,0),
 F(ewa,4,2,8,15,0),F(adrl,1,2,7,1,0),F(frcl,1,2,6,1,0),F(shift,2,2,4,3,0),
 F(yrl,1,2,3,1,0),F(negb,1,2,2,1,0),F(zero,1,2,1,1,0),F(bsel,1,2,0,1,0),
 F(nofl,1,3,15,1,1),F(masa,6,3,9,63,1),F(adreb,1,3,8,1,1),F(nxadr,1,3,7,1,1)};
#undef F
static void encoding(void) {
    unsigned i,value,step;uint16_t coefficient=1234;
    snd_dsp_instruction_t instruction,saved;
    aica_dsp_fields_t f={0};
    memset(&instruction,0xa5,sizeof(instruction));saved=instruction;
    CHECK(aica_dsp_encode(128,&f,&instruction)<0 && memcmp(&instruction,&saved,sizeof(saved))==0);
    CHECK(aica_dsp_encode(0,NULL,&instruction)<0 && aica_dsp_encode(0,&f,NULL)<0);
    for(i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        const field_t *field=&fields[i];
        for(value=0;value<=field->maximum+1;value++) {
            memset(&f,0,sizeof(f));*(unsigned *)((unsigned char *)&f+field->offset)=value;
            for(step=0;step<128;step++) {
                int valid=value<=field->maximum && (!(field->memory && value) || (step&1));
                instruction=saved;
                CHECK((aica_dsp_encode(step,&f,&instruction)==0)==valid);
                if(valid) {
                    unsigned word;
                    for(word=0;word<4;word++) CHECK(instruction.word[word]==
                        (word==field->word?(uint16_t)(value<<field->shift):0));
                } else CHECK(memcmp(&instruction,&saved,sizeof(saved))==0);
            }
        }
        *(unsigned *)((unsigned char *)&f+field->offset)=UINT_MAX;
        CHECK(aica_dsp_encode(127,&f,&instruction)<0);
    }
    for(int value_q12=-4096;value_q12<=4095;value_q12++) {
        CHECK(aica_dsp_coefficient(value_q12,&coefficient)==0);
        CHECK(coefficient==(uint16_t)(value_q12*8) && !(coefficient&7));
    }
    coefficient=1234;CHECK(aica_dsp_coefficient(-4097,&coefficient)<0 && coefficient==1234);
    CHECK(aica_dsp_coefficient(4096,&coefficient)<0 && coefficient==1234);
    CHECK(aica_dsp_coefficient(0,NULL)<0);
}
/* Deliberately limited original arithmetic oracle for generated gain images.
   It rejects memory/TEMP/latch operations instead of pretending to emulate them.
   Signed shifts use floor division explicitly, without implementation-defined
   right shifts or undefined negative left shifts. Inputs already use24-bit units. */
static int64_t floor_div(int64_t n,int64_t d) { return n>=0?n/d:-((-n+d-1)/d); }
static int32_t clip24(int64_t n) { return (int32_t)(n>8388607?8388607:(n < -8388608?-8388608:n)); }
static void execute(const snd_dsp_program_t *p,const int32_t *inputs,int32_t *outputs) {
    int64_t accumulator=0;unsigned step;
    for(step=0;step<128;step++) {
        const uint16_t *w=p->steps[step].word;
        unsigned input=(w[1]>>7)&63;
        int coefficient=p->coefficients[step];
        int32_t old=clip24(accumulator);
        if(coefficient>=32768) coefficient-=65536;
        coefficient/=8;
        CHECK(w[0]==0 && w[3]==0 && (w[1]&0x607f)==0x2000);
        CHECK((w[2]&0xe0fd)==0 && (w[2]&2)==2 && input<=49);
        accumulator=floor_div((int64_t)((w[1]&0x8000)?inputs[input]:123456)*coefficient,4096);
        if(w[2]&0x1000) outputs[(w[2]>>8)&15]=(int32_t)floor_div(old,256);
    }
}
static uint32_t random_state=0xaba357d9;
static uint32_t next(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static void gains(void) {
    snd_dsp_program_t p,old;
    aica_dsp_gain_route_t routes[16];
    int32_t inputs[50],outputs[16];unsigned round,i,count;
    size_t work;
    for(round=0;round<1024;round++) {
        count=round%17;
        for(i=0;i<50;i++) inputs[i]=(int32_t)(next()&0xffffff)-8388608;
        for(i=0;i<16;i++) {
            outputs[i]=0x7fff;
            routes[i].input=next()%50;routes[i].output=(i+round)%16;
            routes[i].gain_q12=(int)(next()%8192)-4096;
        }
        CHECK(aica_dsp_build_gain(&p,routes,count)==0);
        CHECK(snd_dsp_program_validate(&p,&work)==0 && work==16384);
        execute(&p,inputs,outputs);
        for(i=0;i<16;i++) {
            int32_t expected=0;unsigned j;
            for(j=0;j<count;j++) if(routes[j].output==i) expected=(int32_t)floor_div(
                clip24(floor_div((int64_t)inputs[routes[j].input]*routes[j].gain_q12,4096)),256);
            CHECK(outputs[i]==expected);
        }
    }
    /* Deterministic signed endpoints and per-return silence reset. */
    for(i=0;i<50;i++) inputs[i]=-8388608;
    for(i=0;i<16;i++) routes[i]=(aica_dsp_gain_route_t){i+32,i,-4096};
    CHECK(aica_dsp_build_gain(&p,routes,16)==0);execute(&p,inputs,outputs);
    for(i=0;i<16;i++) CHECK(outputs[i]==32767);
    CHECK(aica_dsp_build_gain(&p,NULL,0)==0);execute(&p,inputs,outputs);
    for(i=0;i<16;i++) CHECK(outputs[i]==0);
    old=p;routes[0]=(aica_dsp_gain_route_t){50,0,0};
    CHECK(aica_dsp_build_gain(&p,routes,1)<0 && memcmp(&p,&old,sizeof(p))==0);
    routes[0]=(aica_dsp_gain_route_t){0,16,0};
    CHECK(aica_dsp_build_gain(&p,routes,1)<0 && memcmp(&p,&old,sizeof(p))==0);
    routes[0]=(aica_dsp_gain_route_t){0,0,4096};
    CHECK(aica_dsp_build_gain(&p,routes,1)<0 && memcmp(&p,&old,sizeof(p))==0);
    routes[0]=(aica_dsp_gain_route_t){0,1,0};routes[1]=routes[0];
    CHECK(aica_dsp_build_gain(&p,routes,2)<0 && memcmp(&p,&old,sizeof(p))==0);
    CHECK(aica_dsp_build_gain(&p,NULL,1)<0 && memcmp(&p,&old,sizeof(p))==0);
    CHECK(aica_dsp_build_gain(&p,routes,17)<0 && memcmp(&p,&old,sizeof(p))==0);
    CHECK(aica_dsp_build_gain(NULL,routes,0)<0);
}
int main(void) { encoding();gains();printf("dsp build checks: %u\n",checks);return 0; }
