/* All certified decision boundaries plus domain/scaling checks. */
#include "adx_math.h"
#include <kosdec/adx.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/adx_setup_table.h"
#include "setup_specials.h"
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define CHECK(x) do {if(!(x)){printf("SETUP_FAIL line=%u\n",(unsigned)__LINE__);return 1;}}while(0)

static int rounded_setup(void) {
    static const struct {
        unsigned rate, cutoff;
        int16_t first, second;
    } cases[] = {
        {8000, 1980, 1241, -93},
        {11025, 1, 8184, -4088},
        {16000, 1, 8187, -4091},
        {22050, 500, 6569, -2634},
        {24000, 1, 8187, -4091},
        {32000, 1, 8192, -4096},
        {44100, 0, 8192, -4096},
        {44100, 1, 8192, -4096},
        {44100, 2, 8187, -4091},
        {44100, 3, 8185, -4089},
        {44100, 500, 7334, -3283},
    };
    uint8_t header[26] = {
        0x80, 0, 0, 22, 3, 18, 4, 1, 0, 0, 0, 0, 0, 0, 0, 32,
        0, 0, 3, 0, '(', 'c', ')', 'C', 'R', 'I'
    };
    struct kosdec_adx_decoder d;
    for(unsigned version = 3; version <= 5; version += 2) {
        header[18] = (uint8_t)version;
        for(unsigned i = 0; i < COUNT(cases); ++i) {
            header[10] = (uint8_t)(cases[i].rate >> 8);
            header[11] = (uint8_t)cases[i].rate;
            header[16] = (uint8_t)(cases[i].cutoff >> 8);
            header[17] = (uint8_t)cases[i].cutoff;
            kosdec_adx_init(&d);
            struct kosdec_adx_result r = kosdec_adx_decode(&d, header, sizeof(header), NULL, 0, false);
            CHECK(r.status == KOSDEC_ADX_MORE && r.consumed == sizeof(header) && d.header_ready);
            CHECK(d.coefficient[0] == cases[i].first && d.coefficient[1] == cases[i].second);
        }
        header[16] = 0;
        header[17] = 1;
        kosdec_adx_init(&d);
        CHECK(kosdec_adx_decode(&d, header, sizeof(header), NULL, 0, false).status == KOSDEC_ADX_MORE);
        uint8_t group[18] = {0, 0, 0x10};
        int16_t pcm[32];
        struct kosdec_adx_result r = kosdec_adx_decode(&d, group, sizeof(group), pcm, 32, false);
        CHECK(r.status == KOSDEC_ADX_MORE && r.frames == 32);
        for(unsigned i = 0; i < 32; ++i)
            CHECK(pcm[i] == (int16_t)(i + 1));

        header[10] = (uint8_t)(48000 >> 8);
        header[11] = (uint8_t)48000;
        kosdec_adx_init(&d);
        CHECK(kosdec_adx_decode(&d, header, sizeof(header), NULL, 0, false).status == KOSDEC_ADX_MORE);
        int16_t extension[2];
        CHECK(!media_adx_coefficients(48000, 1, extension));
        CHECK(!memcmp(d.coefficient, extension, sizeof(extension)));
        CHECK(media_adx_rounded_coefficients(48000, 1, extension) == -1);
    }
    printf("ADX_ROUNDED_SETUP profiles=%u pcm_frames=64 extensions=2 failures=0\n",
           (unsigned)COUNT(cases) * 2);
    return 0;
}

int main(void) {
    if(rounded_setup()) return 1;
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
