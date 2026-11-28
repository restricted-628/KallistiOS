/* Exact, hand-derived float-feedback cases; no proprietary sample vectors. */
#include "adx_math.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"CLIPPED_HISTORY_FAIL line=%u\n",(unsigned)__LINE__); \
    exit(1); } } while(0)
static unsigned checks;

static void check_jobs(void) {
    const int16_t coefficients[2] = {7500, -3580};
    uint8_t coded[6 * 18] = {0};
    for(unsigned block = 0; block < 6; ++block) {
        coded[18 * block + 1] = 3;
        for(unsigned i = 2; i < 18; ++i)
            coded[18 * block + i] = (uint8_t)(17 * i + 3 * block);
    }
    /* Built independently from the binary32 predictor, then checked against
       the reference mono and stereo decoder for the same synthetic block bytes. */
    const struct {
        unsigned channels, physical_frames, shown;
        uint32_t digest;
    } cases[] = {
        {1, 64, 33, UINT32_C(0x6ed631b0)},
        {2, 64, 33, UINT32_C(0xbe8b0064)},
        {2, 96, 95, UINT32_C(0x4e28e7c2)},
    };
    for(unsigned n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n) {
        unsigned channels = cases[n].channels;
        unsigned frames = cases[n].physical_frames;
        unsigned shown = cases[n].shown;
        uint32_t digest = 0;
        int16_t pcm[96 * 2];
        for(unsigned i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i)
            pcm[i] = 123;
        for(unsigned ch = 0; ch < channels; ++ch) {
            int16_t h[2] = {0, 0}, silent[2] = {0, 0};
            CHECK(media_adx_channel_job(coded + ch * 18, frames, shown,
                                        coefficients, h, pcm + ch, channels) == 0);
            CHECK(media_adx_channel_job(coded + ch * 18, frames, 0,
                                        coefficients, silent, NULL, channels) == 0);
            CHECK(silent[0] == h[0] && silent[1] == h[1]);
            for(unsigned i = 0; i < shown; ++i) {
                uint16_t sample = (uint16_t)pcm[i * channels + ch];
                digest = digest * 31 + (sample & 255);
                digest = digest * 31 + (sample >> 8);
            }
        }
        CHECK(digest == cases[n].digest);
        for(unsigned i = shown * channels; i < sizeof(pcm) / sizeof(pcm[0]); ++i)
            CHECK(pcm[i] == 123);
    }

    /* A control word in a later block rejects the entire channel job. */
    coded[36] = 0x80;
    int16_t h[2] = {1, 2}, pcm[2] = {3, 4};
    CHECK(media_adx_channel_job(coded, 64, 1, coefficients, h, pcm, 2) < 0);
    CHECK(h[0] == 1 && h[1] == 2 && pcm[0] == 3 && pcm[1] == 4);

    uint8_t long_job[MEDIA_ADX_JOB_MAX_FRAMES / 32 * 18];
    memset(long_job, 0x77, sizeof(long_job));
    for(unsigned i = 0; i < sizeof(long_job); i += 18) {
        long_job[i] = 0x1f;
        long_job[i + 1] = 0xff;
    }
    const int16_t unstable[2] = {8192, -4095};
    CHECK(media_adx_channel_job(long_job, 64, 0, unstable, h, NULL, 1) < 0);
    CHECK(h[0] == 1 && h[1] == 2);
    const int16_t double_pole[2] = {8192, -4096};
    CHECK(media_adx_channel_job(long_job, MEDIA_ADX_JOB_MAX_FRAMES, 0,
                                double_pole, h, NULL, 1) == 0);
    CHECK(h[0] == INT16_MAX && h[1] == INT16_MAX);
}

int main(void) {
    /* c0=1, c1=0, scale=8192. Every intermediate is exactly binary32.
       +7,-7 => +57344,0; emitted PCM is +32767,0.
       Feeding clipped +32767 back would make the second output -24577.
       -8,+4 => -65536,-32768; emitted PCM is -32768,-32768.
       Feeding clipped -32768 back would make the second output zero. */
    const int16_t coefficients[2]={4096,0};
    for(unsigned negative=0;negative<2;++negative) {
        uint8_t block[18]={0x7f,0xff,negative?0x84:0x79};
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
    /* At the group boundary the next prediction starts at +32767. */
    uint8_t block[18]={0x7f,0xff,0x70}, zero[18]={0};
    int16_t history[2]={0,0}, out[2];
    CHECK(media_adx_channel_group(block,1,coefficients,history,out,1)==1);
    CHECK(history[0]==INT16_MAX && history[1]==0);
    CHECK(media_adx_channel_group(zero,1,coefficients,history,out,1)==0);
    CHECK(out[0]==INT16_MAX && history[0]==INT16_MAX && history[1]==INT16_MAX);
    /* The two bits between the control and scale fields are not scale. */
    const int16_t no_prediction[2] = {0,0};
    for(unsigned flags = 0; flags < 4; ++flags) {
        uint8_t small[18] = {(uint8_t)(flags << 5), 3, 0x78};
        int16_t initial[2] = {123,-123};
        CHECK(media_adx_channel_group(small,2,no_prediction,initial,out,1) == 0);
        CHECK(out[0] == 28 && out[1] == -32);
        CHECK(initial[0] == -32 && initial[1] == 28);
        small[0] |= 0x80;
        CHECK(media_adx_channel_group(small,2,no_prediction,initial,out,1) < 0);
        CHECK(out[0] == 28 && out[1] == -32);
        CHECK(initial[0] == -32 && initial[1] == 28);
    }
    check_jobs();
    printf("ADX_CLIPPED_HISTORY checks=%u failures=0\n",checks);
    return 0;
}
