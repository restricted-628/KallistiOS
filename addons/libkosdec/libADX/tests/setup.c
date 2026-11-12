/* All certified decision boundaries plus domain/scaling checks. */
#include "adx_math.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/adx_setup_table.h"
#include "setup_specials.h"
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define CHECK(x) do {if(!(x)){printf("SETUP_FAIL line=%u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void) {
    unsigned cases=0;
    const unsigned rates[]={8000,47999,48000};
    for(unsigned axis=0;axis<2;++axis) {
        const uint32_t *table=axis?setup_second:setup_first;
        unsigned count=axis?COUNT(setup_second):COUNT(setup_first),maximum=axis?4095:8191;
        for(unsigned i=0;i<count;++i)for(unsigned r=0;r<COUNT(rates);++r) {
            unsigned below=(unsigned)(((uint64_t)table[i]*rates[r]-1)>>32);
            for(unsigned side=0;side<2;++side) {
                int16_t q[2];unsigned cutoff=below+side;
                CHECK(!media_adx_coefficients(rates[r],cutoff,q));
                unsigned value=axis?(unsigned)-q[1]:(unsigned)q[0];
                CHECK(side?value<maximum-i:value>=maximum-i);
                ++cases;
            }
        }
    }
    for(unsigned i=0;i<COUNT(setup_specials);++i) {
        int16_t q[2];CHECK(!media_adx_coefficients(setup_specials[i].rate,setup_specials[i].cutoff,q));
        CHECK((unsigned)(setup_specials[i].axis?-q[1]:q[0])==setup_specials[i].expected);
    }
    for(unsigned rate=1;rate<=48000;++rate) {
        int16_t q[2],scaled[2];CHECK(!media_adx_coefficients(rate,0,q));
        CHECK(q[0]==8192 && q[1]==-4096);
        CHECK(!media_adx_coefficients(rate,rate/2,q));
        CHECK(q[0]>=708 && q[0]<=8192 && q[1]>=-4096 && q[1]<=-30);
        if(!(rate%2))CHECK(q[0]==708 && q[1]==-30);
        if(rate<=24000) {
            CHECK(!media_adx_coefficients(rate*2,(rate/2)*2,scaled));
            CHECK(!memcmp(q,scaled,sizeof(q)));
        }
        q[0]=123;q[1]=-456;
        CHECK(media_adx_coefficients(rate,rate/2+1,q)==-1);
        CHECK(q[0]==123 && q[1]==-456);
    }
    int16_t q[2]={123,-456};
    CHECK(media_adx_coefficients(0,0,q)==-1);
    CHECK(media_adx_coefficients(48001,0,q)==-1);
    CHECK(media_adx_coefficients(UINT32_MAX,0,q)==-1);
    CHECK(media_adx_coefficients(44100,UINT32_MAX,q)==-1);
    CHECK(media_adx_coefficients(44100,500,NULL)==-1);
    CHECK(q[0]==123 && q[1]==-456);
    printf("ADX_SETUP boundaries=%u specials=%u endpoint_rates=48000 failures=0\n",cases,(unsigned)COUNT(setup_specials));
    return 0;
}
