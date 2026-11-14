#include <kosdec/adx_player.h>
#include <kos/timer.h>
#include "player_mock.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned checks;
#define CHECK(x) do{__atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);if(!(x)){fprintf(stderr,"start_gate:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fixture {
    struct kosdec_adx_player player;struct kosdec_adx_wave wave;
    uint8_t bytes[44+4096];unsigned length,calls;bool wait,fail;pthread_t owner;
    alignas(32) int16_t pcm[1024],scratch[257];alignas(32) uint8_t stack[16384];
};
static void put(uint8_t *p,uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[i]=(uint8_t)(n>>(8*i));}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=ctx;CHECK(!pthread_equal(f->owner,pthread_self()));
    __atomic_add_fetch(&f->calls,1,__ATOMIC_RELAXED);
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(__atomic_load_n(&f->fail,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    CHECK(cap && cap<=512 && off<f->length && cap<=f->length-off);
    if(cap>31)cap=31;memcpy(dst,f->bytes+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void init(struct fixture *f,unsigned frames,unsigned channels,unsigned timeout) {
    memset(f,0,sizeof(*f));mock_reset();f->owner=pthread_self();f->length=44+frames*channels*2;
    CHECK(f->length<=sizeof(f->bytes));memcpy(f->bytes,"RIFF",4);put(f->bytes+4,f->length-8,4);
    memcpy(f->bytes+8,"WAVEfmt ",8);put(f->bytes+16,16,4);put(f->bytes+20,1,2);put(f->bytes+22,channels,2);
    put(f->bytes+24,32000,4);put(f->bytes+28,32000*channels*2,4);put(f->bytes+32,channels*2,2);put(f->bytes+34,16,2);
    memcpy(f->bytes+36,"data",4);put(f->bytes+40,frames*channels*2,4);
    for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<channels;++c)put(f->bytes+44+(i*channels+c)*2,i*13+c*7,2);
    CHECK(kosdec_adx_wave_init(&f->wave,source,f,f->length)==KOSDEC_ADX_MORE);
    struct kosdec_adx_player_config config={.pcm=f->pcm,.scratch=f->scratch,.pcm_samples=1024,.capacity=512,.scratch_samples=257,
        .stack=f->stack,.stack_bytes=sizeof(f->stack),.sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=timeout};
    CHECK(!kosdec_adx_player_init_wave(&f->player,&f->wave,&config));
}
static struct kosdec_adx_player_info info(struct fixture *f){struct kosdec_adx_player_info i;CHECK(!kosdec_adx_player_get_info(&f->player,&i));return i;}
static bool ready(struct fixture *f){bool r=true;CHECK(!kosdec_adx_player_is_ready_start(&f->player,&r));return r;}
static void wait_ready(struct fixture *f) {
    for(unsigned i=0;i<3000;++i){CHECK(!kosdec_adx_player_poll(&f->player));if(ready(f))return;mock_pause();}CHECK(0);
}
static void finish(struct fixture *f) {
    for(unsigned i=0;i<3000;++i){CHECK(!kosdec_adx_player_poll(&f->player));if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)return;mock_pause();}CHECK(0);
}
static void gate(void) {
    unsigned lengths[]={0,1,65,1024};
    for(unsigned channels=1;channels<=2;++channels)for(unsigned k=0;k<4;++k) {
        struct fixture f;init(&f,lengths[k],channels,500);CHECK(!ready(&f));
        CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));CHECK(!kosdec_adx_player_start(&f.player));
        wait_ready(&f);CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PREFILL && info(&f).wait_start && !mock_allocs && !mock_starts && !mock_frames);
        uint64_t until=timer_ms_gettime64()+(k==3 && channels==2?600:4);
        while(timer_ms_gettime64()<until){CHECK(!kosdec_adx_player_poll(&f.player));CHECK(ready(&f) && !mock_allocs && !mock_frames);mock_pause();}
        unsigned before=__atomic_load_n(&f.calls,__ATOMIC_RELAXED);
        CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));CHECK(ready(&f));
        if(!lengths[k])CHECK(__atomic_load_n(&f.calls,__ATOMIC_RELAXED)==before);
        CHECK(!kosdec_adx_player_poll(&f.player));CHECK(mock_allocs==1 && mock_starts==1 && ready(&f));
        /* Closing after admission never pauses current playback, but persists. */
        CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));finish(&f);CHECK(mock_frames==lengths[k]);
        for(unsigned i=0;i<lengths[k];++i)for(unsigned c=0;c<channels;++c)CHECK(mock_pcm[i*channels+c]==(int16_t)(i*13+c*7));
        CHECK(!kosdec_adx_player_release_source(&f.player) && !ready(&f));
        CHECK(kosdec_adx_wave_seek(&f.wave,0)==KOSDEC_ADX_DONE);CHECK(!kosdec_adx_player_bind_wave(&f.player,&f.wave));
        wait_ready(&f);CHECK(mock_frames==lengths[k] && mock_allocs==1 && mock_starts==1 && info(&f).sources_started==1);
        for(unsigned i=0;i<5;++i){CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();}
        CHECK(mock_frames==lengths[k]);CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));finish(&f);
        CHECK(mock_frames==2*lengths[k] && info(&f).sources_started==2);
        CHECK(!kosdec_adx_player_stop(&f.player,1000) && !ready(&f));
        CHECK(kosdec_adx_player_set_wait_start(&f.player,true)<0 && errno==EINVAL);
    }
}
static void failure(void) {
    struct fixture f;init(&f,1024,1,500);CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));wait_ready(&f);
    CHECK(!kosdec_adx_player_cancel_source(&f.player) && !ready(&f) && !mock_allocs);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    init(&f,1024,1,500);CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));CHECK(!kosdec_adx_player_start(&f.player));wait_ready(&f);
    CHECK(!kosdec_adx_player_stop(&f.player,1000) && !ready(&f) && !mock_allocs && mock_joins==1);
    for(unsigned mode=0;mode<3;++mode) {
        init(&f,65,1,30);f.wait=mode==0;f.fail=mode==1;
        if(mode==2){put(f.bytes+24,1,4);put(f.bytes+28,2,4);}
        CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));CHECK(!kosdec_adx_player_start(&f.player));
        int result=0;for(unsigned i=0;i<3000 && result==0;++i){result=kosdec_adx_player_poll(&f.player);mock_pause();}
        CHECK(result<0 && !ready(&f) && !mock_allocs);
        CHECK(info(&f).first_error==(mode==0?ETIMEDOUT:mode==1?EIO:ENOTSUP));
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
    }
    bool result=true;CHECK(kosdec_adx_player_is_ready_start(NULL,&result)<0 && result);
    CHECK(kosdec_adx_player_is_ready_start(&f.player,NULL)<0);
    CHECK(kosdec_adx_player_set_wait_start(NULL,false)<0);
}
int main(void){gate();failure();printf("START_GATE: %u checks passed\n",checks);return 0;}
