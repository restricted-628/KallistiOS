#include <kosdec/adx_amplitude.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED); if(!(x)) { \
    fprintf(stderr,"amplitude:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint32_t peak(const int16_t *pcm,size_t start,size_t n,unsigned stride) {
    int32_t lo=0,hi=0;
    for(size_t i=start;i<start+n;++i) {int32_t v=pcm[i*stride];if(v<lo)lo=v;if(v>hi)hi=v;}
    return (uint32_t)(hi>-lo?hi:-lo);
}
static void verify(const struct kosdec_adx_amplitude_record *r,const int16_t *pcm,
                   size_t start,unsigned window,unsigned stride,unsigned rate) {
    CHECK(r->start_frame==start && r->frames==window && r->sample_rate==rate);
    CHECK(r->peak==peak(pcm,start,window,stride));
}
static void matrix(void) {
    int16_t pcm[2*2053];
    for(unsigned i=0;i<4106;++i)pcm[i]=(int16_t)((i*251u)%65536u-32768);
    unsigned windows[]={1,7,32,441,4410};unsigned fragments[]={1,11,513,2053};
    unsigned rates[]={1,8000,44100,48000};
    for(unsigned channels=1;channels<=2;++channels)
    for(unsigned w=0;w<5;++w)for(unsigned f=0;f<4;++f)for(unsigned rt=0;rt<4;++rt) {
        struct kosdec_adx_amplitude a;struct kosdec_adx_amplitude_record storage[8],out;
        CHECK(kosdec_adx_amplitude_init(&a,storage,8,channels,4,rates[rt],windows[w])==KOSDEC_ADX_DONE);
        for(unsigned ch=0;ch<channels;++ch) {
            size_t used=0,read=0;
            while(used<2053) {
                size_t n=2053-used;if(n>fragments[f])n=fragments[f];
                struct kosdec_adx_amplitude_result r=kosdec_adx_amplitude_feed(&a,ch,pcm+used*channels+ch,n,channels);
                CHECK(r.status==KOSDEC_ADX_MORE || r.status==KOSDEC_ADX_NEED_OUTPUT);
                CHECK(r.consumed<=n && r.consumed<=512);used+=r.consumed;
                while(kosdec_adx_amplitude_read(&a,ch,&out)==KOSDEC_ADX_DONE) {
                    verify(&out,pcm+ch,read,windows[w],channels,rates[rt]);read+=windows[w];
                }
            }
            CHECK(read==2053/windows[w]*windows[w]);
        }
        CHECK(kosdec_adx_amplitude_finish(&a)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_amplitude_finish(&a)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_amplitude_status(&a)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_amplitude_read(&a,0,&out)==KOSDEC_ADX_NEED_END);
        CHECK(kosdec_adx_amplitude_feed(&a,0,pcm,1,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    }
}
static void bounds_selection(void) {
    struct kosdec_adx_amplitude a,before;struct kosdec_adx_amplitude_record storage[4],out,unchanged;
    int16_t pcm[]={-32768,32767,-1,0,17,19,23,29};
    CHECK(kosdec_adx_amplitude_work_size(2,4)==8*sizeof(out));
    CHECK(!kosdec_adx_amplitude_work_size(0,4));CHECK(!kosdec_adx_amplitude_work_size(1,3));
    CHECK(!kosdec_adx_amplitude_work_size(3,4));CHECK(!kosdec_adx_amplitude_work_size(1,SIZE_MAX));
    memset(&a,0xa5,sizeof(a));before=a;
    CHECK(kosdec_adx_amplitude_init(&a,storage,4,2,4,44100,4)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&a,&before,sizeof(a)));
    CHECK(kosdec_adx_amplitude_init(&a,storage,4,1,4,0,4)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_amplitude_init(&a,storage,4,1,4,44100,0)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_amplitude_init(&a,storage,4,1,4,44100,2)==KOSDEC_ADX_DONE);
    before=a;memset(&out,0xa5,sizeof(out));unchanged=out;
    CHECK(kosdec_adx_amplitude_read(&a,0,&out)==KOSDEC_ADX_NEED_INPUT);
    CHECK(!memcmp(&out,&unchanged,sizeof(out)));
    CHECK(kosdec_adx_amplitude_feed(&a,1,pcm,1,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_amplitude_feed(&a,0,NULL,1,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_amplitude_feed(&a,0,pcm,1,3).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&a,&before,sizeof(a)));
    /* Exercise cursor rollover without needing billions of records. */
    a.channel[0].write_cursor=a.channel[0].read_cursor=UINT32_MAX-1;
    CHECK(kosdec_adx_amplitude_feed(&a,0,pcm,8,1).consumed==8);
    CHECK(kosdec_adx_amplitude_feed(&a,0,pcm,1,1).status==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(kosdec_adx_amplitude_select(&a,0,0,&out)==KOSDEC_ADX_DONE && out.peak==32768 && out.start_frame==0);
    CHECK(kosdec_adx_amplitude_select(&a,0,1,&out)==KOSDEC_ADX_DONE && out.start_frame==2 && out.peak==1);
    CHECK(kosdec_adx_amplitude_select(&a,0,6,&out)==KOSDEC_ADX_DONE && out.start_frame==6 && out.peak==29);
    CHECK(kosdec_adx_amplitude_select(&a,0,100,&out)==KOSDEC_ADX_DONE && out.start_frame==6);
    CHECK(kosdec_adx_amplitude_select(&a,0,0,&out)==KOSDEC_ADX_DONE && out.start_frame==6);
    CHECK(kosdec_adx_amplitude_finish(&a)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_amplitude_read(&a,0,&out)==KOSDEC_ADX_DONE && out.start_frame==6);
    CHECK(kosdec_adx_amplitude_read(&a,0,&out)==KOSDEC_ADX_NEED_END);
}
static void codec_filter(void) {
    for(unsigned ch=1;ch<=2;++ch)for(unsigned fragment=1;fragment<=71;fragment+=10)
    for(unsigned overflow=0;overflow<2;++overflow) {
        struct kosdec_adx_amplitude a;struct kosdec_adx_amplitude_record storage[32],r;
        CHECK(kosdec_adx_amplitude_init(&a,storage,32,ch,overflow?2:16,44100,7)==KOSDEC_ADX_DONE);
        struct kosdec_adx_decoder d,plain;kosdec_adx_init(&d);kosdec_adx_init(&plain);
        CHECK(kosdec_adx_set_filter(&d,kosdec_adx_amplitude_filter,&a)==KOSDEC_ADX_DONE);
        size_t n,used=0,frames=0;uint8_t *p=stream(50,18,ch,32,61,&n);int16_t all[130];bool done=false;
        for(unsigned i=0;i<1000;++i) {
            size_t take=n-used;if(take>fragment)take=fragment;int16_t pcm[64],reference[64];
            struct kosdec_adx_result x=kosdec_adx_decode(&d,p+used,take,pcm,32,used+take==n);
            struct kosdec_adx_result y=kosdec_adx_decode(&plain,p+used,take,reference,32,used+take==n);
            CHECK(x.status==y.status && x.frames==y.frames && x.consumed==y.consumed);
            CHECK(!memcmp(pcm,reference,x.frames*ch*sizeof(*pcm)));
            CHECK(!memcmp(d.history,plain.history,sizeof(d.history)) && d.clips==plain.clips);
            memcpy(all+frames*ch,pcm,x.frames*ch*sizeof(*pcm));frames+=x.frames;used+=x.consumed;
            if(x.status==KOSDEC_ADX_DONE){done=true;break;}CHECK(x.status<KOSDEC_ADX_DONE);
        }
        CHECK(done && frames==65 && used==n);
        if(overflow) {
            CHECK(kosdec_adx_amplitude_status(&a)==KOSDEC_ADX_NEED_OUTPUT);
            CHECK(kosdec_adx_amplitude_finish(&a)==KOSDEC_ADX_NEED_OUTPUT);
            for(unsigned k=0;k<2;++k)CHECK(kosdec_adx_amplitude_read(&a,0,&r)==KOSDEC_ADX_DONE);
            CHECK(kosdec_adx_amplitude_read(&a,0,&r)==KOSDEC_ADX_NEED_OUTPUT);
            CHECK(kosdec_adx_amplitude_feed(&a,0,all,1,ch).consumed==0);
        } else {
            CHECK(kosdec_adx_amplitude_finish(&a)==KOSDEC_ADX_DONE);
            for(unsigned c=0;c<ch;++c)for(unsigned start=0;start<63;start+=7) {
                CHECK(kosdec_adx_amplitude_read(&a,c,&r)==KOSDEC_ADX_DONE);verify(&r,all+c,start,7,ch,44100);
            }
        }
        free(p);
    }
}
struct concurrent {struct kosdec_adx_amplitude a;struct kosdec_adx_amplitude_record records[8];};
static int16_t sample(uint64_t i) {return (int16_t)((i*163u)%65536u-32768);}
static void *producer(void *ctx) {
    struct concurrent *c=ctx;uint64_t at=0;
    while(at<200000) {
        int16_t pcm[31];unsigned n=200000-at<31?(unsigned)(200000-at):31;
        for(unsigned i=0;i<n;++i)pcm[i]=sample(at+i);
        struct kosdec_adx_amplitude_result r=kosdec_adx_amplitude_feed(&c->a,0,pcm,n,1);
        CHECK(r.status==KOSDEC_ADX_MORE || r.status==KOSDEC_ADX_NEED_OUTPUT);at+=r.consumed;sched_yield();
    }
    CHECK(kosdec_adx_amplitude_finish(&c->a)==KOSDEC_ADX_DONE);return NULL;
}
static void concurrent(void) {
    struct concurrent c;pthread_t thread;
    CHECK(kosdec_adx_amplitude_init(&c.a,c.records,8,1,8,48000,13)==KOSDEC_ADX_DONE);
    CHECK(!pthread_create(&thread,NULL,producer,&c));uint64_t at=0;
    for(;;) {
        struct kosdec_adx_amplitude_record r;
        enum kosdec_adx_status s=kosdec_adx_amplitude_read(&c.a,0,&r);
        if(s==KOSDEC_ADX_NEED_END)break;
        if(s==KOSDEC_ADX_NEED_INPUT){sched_yield();continue;}
        CHECK(s==KOSDEC_ADX_DONE && r.start_frame==at && r.frames==13 && r.sample_rate==48000);
        int16_t pcm[13];for(unsigned i=0;i<13;++i)pcm[i]=sample(at+i);
        CHECK(r.peak==peak(pcm,0,13,1));at+=13;
    }
    CHECK(!pthread_join(thread,NULL));CHECK(at==200000/13*13);
}
int main(void) {matrix();bounds_selection();codec_filter();concurrent();printf("amplitude: %u checks passed\n",checks);return 0;}
