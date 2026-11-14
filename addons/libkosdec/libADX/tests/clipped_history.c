/* Exact, hand-derived float-feedback cases; no proprietary sample vectors. */
#include "adx_math.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"CLIPPED_HISTORY_FAIL line=%u\n",(unsigned)__LINE__); \
    exit(1); } } while(0)
static unsigned checks;
int main(void) {
    /* c0=1, c1=0, scale=32768. Every intermediate is exactly binary32.
       +7,-7 => +229376,0; emitted PCM is +32767,0.
       Feeding clipped +32767 back would make the second output -32768.
       -8,+7 => -262144,-32768; emitted PCM is -32768,-32768.
       Feeding clipped -32768 back would make the second output +32767. */
    const int16_t coefficients[2]={4096,0};
    for(unsigned negative=0;negative<2;++negative) {
        uint8_t block[18]={0x7f,0xff,negative?0x87:0x79};
        for(unsigned stride=1;stride<=2;++stride) {
            int16_t history[2]={0,0}, out[64];
            for(unsigned i=0;i<64;++i) out[i]=123;
            CHECK(media_adx_channel_group(block,2,coefficients,history,out,stride)==1);
            CHECK(out[0]==(negative?INT16_MIN:INT16_MAX));
            CHECK(out[stride]==(negative?INT16_MIN:0));
            CHECK(history[0]==out[stride] && history[1]==out[0]);
            for(unsigned i=0;i<64;++i)
                if(i!=0 && i!=stride) CHECK(out[i]==123);
        }
    }
    /* Stored history is PCM16 at the call boundary: after one clipped sample,
       a new zero-residual group starts with that stored value, not +229376. */
    uint8_t block[18]={0x7f,0xff,0x70}, zero[18]={0};
    int16_t history[2]={0,0}, out[2];
    CHECK(media_adx_channel_group(block,1,coefficients,history,out,1)==1);
    CHECK(history[0]==INT16_MAX && history[1]==0);
    CHECK(media_adx_channel_group(zero,1,coefficients,history,out,1)==0);
    CHECK(out[0]==INT16_MAX && history[0]==INT16_MAX && history[1]==INT16_MAX);
    printf("ADX_CLIPPED_HISTORY checks=%u failures=0\n",checks);
    return 0;
}
