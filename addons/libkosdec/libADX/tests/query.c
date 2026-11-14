/* Original metadata/prefix contract tests, using the actual decoder parser. */
#include <kosdec/adx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"QUERY_FAIL line=%u\n",(unsigned)__LINE__); abort(); } } while(0)
static unsigned cases;
static void be32(uint8_t *p, uint32_t x) {
    for(unsigned n=0;n<4;++n) p[n]=(uint8_t)(x>>(24-n*8));
}
static bool same(const struct kosdec_adx_info *a, const struct kosdec_adx_info *b) {
    return a->header_bytes==b->header_bytes && a->total_frames==b->total_frames &&
        a->sample_rate==b->sample_rate && a->cutoff==b->cutoff && a->inserted_frames==b->inserted_frames &&
        a->encoding==b->encoding && a->block_bytes==b->block_bytes &&
        a->sample_bits==b->sample_bits && a->channels==b->channels && a->version==b->version &&
        a->loop.present==b->loop.present && a->loop.start_frame==b->loop.start_frame &&
        a->loop.end_frame==b->loop.end_frame && a->loop.start_byte==b->loop.start_byte &&
        a->loop.end_byte==b->loop.end_byte;
}
static uint8_t *make_header(size_t bytes, unsigned ch, unsigned version, bool loop) {
    uint8_t *p=calloc(bytes+64,1); CHECK(p);
    p[0]=0x80; p[2]=(uint8_t)((bytes-4)>>8); p[3]=(uint8_t)(bytes-4);
    p[4]=3; p[5]=18; p[6]=4; p[7]=(uint8_t)ch;
    be32(p+8,44100); be32(p+12,65); p[16]=1; p[17]=0xf4; p[18]=(uint8_t)version;
    if(loop) {
        be32(p+20,1); be32(p+24,1); be32(p+28,32); be32(p+32,(uint32_t)bytes+18*ch);
        be32(p+36,61); be32(p+40,(uint32_t)bytes+36*ch);
    }
    memcpy(p+bytes-6,"(c)CRI",6); memset(p+bytes,0xff,64); return p;
}
static void headers(void) {
    const size_t sizes[]={26,36,50,513,65539};
    for(unsigned ch=1;ch<=2;++ch) for(unsigned version=3;version<=5;version+=2)
    for(unsigned size=0;size<5;++size) for(unsigned loop=0;loop<2;++loop) {
        size_t n=sizes[size]; if(loop && (n<50 || version!=3)) continue;
        ++cases; uint8_t *p=make_header(n,ch,version,loop);
        struct kosdec_adx_info got,sentinel,expected;
        memset(&sentinel,0xa5,sizeof(sentinel));
        /* Every prefix for small records, boundary prefixes for maximum records. */
        for(size_t cut=0;cut<n;++cut) {
            if(n>1024 && cut>64 && cut!=511 && cut!=512 && cut!=513 && cut<n-8) continue;
            got=sentinel;
            CHECK(kosdec_adx_probe_header(p,cut,&got)==KOSDEC_ADX_NEED_INPUT);
            CHECK(!memcmp(&got,&sentinel,sizeof(got)));
        }
        CHECK(kosdec_adx_probe_header(p,n+64,&expected)==KOSDEC_ADX_DONE);
        CHECK(expected.header_bytes==n && expected.total_frames==65 && expected.sample_rate==44100);
        CHECK(expected.cutoff==500 && expected.encoding==3 && expected.block_bytes==18 && expected.sample_bits==4);
        CHECK(expected.channels==ch && expected.version==version && expected.loop.present==(bool)loop);
        if(loop) CHECK(expected.loop.start_frame==32 && expected.loop.end_frame==61 && expected.loop.start_byte==n+18*ch);
        uint8_t *unaligned=malloc(n+1); CHECK(unaligned); memcpy(unaligned+1,p,n);
        CHECK(kosdec_adx_probe_header(unaligned+1,n,&got)==KOSDEC_ADX_DONE && same(&got,&expected)); free(unaligned);
        struct kosdec_adx_decoder d; kosdec_adx_init(&d);
        size_t at=0;
        while(at<n) {
            struct kosdec_adx_decoder saved=d; got=sentinel;
            CHECK(kosdec_adx_get_info(&d,&got)==KOSDEC_ADX_NEED_INPUT);
            CHECK(!memcmp(&d,&saved,sizeof(d)) && !memcmp(&got,&sentinel,sizeof(got)));
            size_t take=n-at; if(take>17) take=17;
            struct kosdec_adx_result r=kosdec_adx_decode(&d,p+at,take,NULL,0,false);
            CHECK(r.status==KOSDEC_ADX_MORE || r.status==KOSDEC_ADX_NEED_INPUT); at+=r.consumed;
        }
        struct kosdec_adx_decoder saved=d;
        CHECK(kosdec_adx_get_info(&d,&got)==KOSDEC_ADX_DONE && same(&got,&expected));
        CHECK(!memcmp(&d,&saved,sizeof(d)));
        int16_t pcm[64]; CHECK(kosdec_adx_decode(&d,p+n,18*ch,pcm,32,false).status==KOSDEC_ADX_INVALID);
        CHECK(kosdec_adx_get_info(&d,&got)==KOSDEC_ADX_DONE && same(&got,&expected));
        d=saved; kosdec_adx_cancel(&d);
        CHECK(kosdec_adx_get_info(&d,&got)==KOSDEC_ADX_DONE && same(&got,&expected));
        p[n-1]^=1; got=sentinel;
        CHECK(kosdec_adx_probe_header(p,n,&got)==KOSDEC_ADX_INVALID && !memcmp(&got,&sentinel,sizeof(got)));
        free(p);
    }
}
static void rejected(void) {
    const unsigned offsets[]={0,4,5,6,7,18,19,20,28};
    const uint8_t values[]={0,2,17,3,3,4,1,1,1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i) {
        uint8_t *p=make_header(50,2,3,true); p[offsets[i]]=values[i];
        struct kosdec_adx_info got,sentinel; memset(&sentinel,0x3c,sizeof(sentinel)); got=sentinel;
        struct kosdec_adx_decoder d; kosdec_adx_init(&d);
        struct kosdec_adx_result r=kosdec_adx_decode(&d,p,50,NULL,0,false);
        CHECK(r.status==KOSDEC_ADX_INVALID || r.status==KOSDEC_ADX_UNSUPPORTED);
        CHECK(kosdec_adx_probe_header(p,50,&got)==r.status && !memcmp(&got,&sentinel,sizeof(got)));
        CHECK(kosdec_adx_get_info(&d,&got)==r.status && !memcmp(&got,&sentinel,sizeof(got)));
        free(p); ++cases;
    }
}
static void footers(void) {
    const size_t sizes[]={18,512,513,65539};
    for(unsigned i=0;i<4;++i) {
        size_t n=sizes[i]; uint8_t *p=calloc(n+2,1); CHECK(p); ++cases;
        /* Intentionally unaligned record and nonzero suffix. */
        uint8_t *f=p+1; f[0]=0x80; f[1]=1; f[2]=(uint8_t)((n-4)>>8); f[3]=(uint8_t)(n-4); f[n]=0xff;
        for(size_t cut=0;cut<n;++cut) {
            if(n>1024 && cut>8 && cut!=511 && cut!=512 && cut!=513 && cut<n-8) continue;
            uint32_t got=123;
            CHECK(kosdec_adx_probe_endcode(f,cut,&got)==KOSDEC_ADX_NEED_INPUT && got==123);
        }
        uint32_t got=123;
        CHECK(kosdec_adx_probe_endcode(f,n+1,&got)==KOSDEC_ADX_DONE && got==n);
        f[n-1]=1; got=123;
        CHECK(kosdec_adx_probe_endcode(f,n,&got)==KOSDEC_ADX_INVALID && got==123);
        f[0]=0; CHECK(kosdec_adx_probe_endcode(f,n,&got)==KOSDEC_ADX_INVALID && got==123);
        f[0]=0x80; f[2]=0; f[3]=13;
        CHECK(kosdec_adx_probe_endcode(f,n,&got)==KOSDEC_ADX_UNSUPPORTED && got==123);
        free(p);
    }
}
int main(void) {
    struct kosdec_adx_decoder d={0}; struct kosdec_adx_info info; uint32_t bytes=1;
    CHECK(kosdec_adx_get_info(&d,&info)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_get_info(NULL,&info)==KOSDEC_ADX_BAD_ARGUMENT);
    kosdec_adx_init(&d); CHECK(kosdec_adx_get_info(&d,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    kosdec_adx_cancel(&d); CHECK(kosdec_adx_get_info(&d,&info)==KOSDEC_ADX_CANCELLED);
    CHECK(kosdec_adx_probe_header(NULL,0,&info)==KOSDEC_ADX_NEED_INPUT);
    CHECK(kosdec_adx_probe_header(NULL,1,&info)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_probe_header(NULL,0,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_probe_endcode(NULL,0,&bytes)==KOSDEC_ADX_NEED_INPUT && bytes==1);
    CHECK(kosdec_adx_probe_endcode(NULL,1,&bytes)==KOSDEC_ADX_BAD_ARGUMENT && bytes==1);
    CHECK(kosdec_adx_probe_endcode(NULL,0,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    headers(); rejected(); footers(); printf("ADX_QUERY cases=%u failures=0\n",cases); return 0;
}
