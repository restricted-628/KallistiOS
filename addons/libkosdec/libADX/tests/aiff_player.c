#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned checks;
#define CHECK(x) do {__atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);if(!(x)){fprintf(stderr,"aiff_player:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_aiff aiff;
    struct kosdec_adx_player_config config;
    uint8_t data[320];unsigned bytes,channels,bits,calls,filtered;
    bool wait,fail,worker_only;
    pthread_t controller;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static void put(uint8_t *p,unsigned n,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[i]=(uint8_t)(n>>(8*(bytes-i-1)));}
static int16_t sample(unsigned frame,unsigned ch,unsigned bits) {
    if(bits==8)return (int16_t)(((int)((frame*7+ch*97)%256)-128)*256);
    return (int16_t)((int)((frame*7919+ch*1234)%65536)-32768);
}
static unsigned calls(struct fixture *f){return __atomic_load_n(&f->calls,__ATOMIC_RELAXED);}
static struct kosdec_adx_input_result source(void *ctx,uint32_t off,uint8_t *out,size_t cap) {
    struct fixture *f=ctx;CHECK(!f->worker_only || !pthread_equal(pthread_self(),f->controller));
    __atomic_add_fetch(&f->calls,1,__ATOMIC_RELAXED);
    if(__atomic_load_n(&f->fail,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(cap && cap<=512 && off<f->bytes && cap<=f->bytes-off);
    if(cap>7)cap=7;memcpy(out,f->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void filter(void *ctx,unsigned channel,int16_t *pcm,size_t frames,unsigned stride) {
    struct fixture *f=ctx;CHECK(!f->worker_only || !pthread_equal(pthread_self(),f->controller));
    CHECK(channel<stride && stride==f->channels && frames && frames<=32);
    for(size_t i=0;i<frames;++i)pcm[i*stride]/=2;
    __atomic_add_fetch(&f->filtered,(unsigned)frames,__ATOMIC_RELAXED);
}
static void init(struct fixture *f,unsigned bits,unsigned ch,bool worker) {
    memset(f,0,sizeof(*f));mock_reset();f->bits=bits;f->channels=ch;f->worker_only=worker;f->controller=pthread_self();
    unsigned data_bytes=65*ch*(bits/8);f->bytes=54+data_bytes+(data_bytes&1);
    memcpy(f->data,"FORM",4);put(f->data+4,f->bytes-8,4);memcpy(f->data+8,"AIFFCOMM",8);
    put(f->data+16,18,4);put(f->data+20,ch,2);put(f->data+22,65,4);put(f->data+26,bits,2);
    put(f->data+28,0x400d,2);f->data[30]=0xfa; /* exact32000 extended */
    memcpy(f->data+38,"SSND",4);put(f->data+42,data_bytes+8,4);
    for(unsigned i=0;i<65;++i)for(unsigned c=0;c<ch;++c) {
        int16_t n=sample(i,c,bits);put(f->data+54+(i*ch+c)*(bits/8),bits==8?(unsigned)(n/256):(uint16_t)n,bits/8);
    }
    CHECK(kosdec_adx_aiff_init(&f->aiff,source,f,f->bytes)==KOSDEC_ADX_MORE);
    f->config=(struct kosdec_adx_player_config){.pcm=f->pcm,.scratch=f->scratch,.pcm_samples=1024,.capacity=512,
        .scratch_samples=257,.stack=f->stack,.stack_bytes=sizeof(f->stack),.sound_bytes=512,
        .transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init_aiff(&f->player,&f->aiff,&f->config));
}
static struct kosdec_adx_player_info info(struct fixture *f) {
    struct kosdec_adx_player_info i;CHECK(!kosdec_adx_player_get_info(&f->player,&i));return i;
}
static int finish(struct fixture *f) {
    for(unsigned i=0;i<3000;++i) {
        if(kosdec_adx_player_poll(&f->player)<0)return -1;
        if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)return 0;
        mock_pause();
    }CHECK(0);return -1;
}
static void playback(void) {
    for(unsigned bits=8;bits<=16;bits+=8)for(unsigned ch=1;ch<=2;++ch) {
        struct fixture f;init(&f,bits,ch,true);CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
        CHECK(mock_frames==65 && info(&f).decoder==KOSDEC_ADX_DONE && info(&f).sample_rate==32000);
        uint32_t loops=99;CHECK(kosdec_adx_pipe_get_loop_count(&f.player.producer.pipe,&loops) && !loops);
        CHECK(!kosdec_adx_player_release_source(&f.player));
        CHECK(kosdec_adx_aiff_set_filter(&f.aiff,filter,&f)==KOSDEC_ADX_BAD_ARGUMENT);
        CHECK(kosdec_adx_aiff_seek(&f.aiff,0)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_aiff_set_filter(&f.aiff,filter,&f)==KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_player_bind_aiff(&f.player,&f.aiff));CHECK(!finish(&f));
        CHECK(mock_frames==130 && mock_allocs==1 && mock_starts==1);
        for(unsigned i=0;i<130;++i)for(unsigned c=0;c<ch;++c)CHECK(mock_pcm[i*ch+c]==sample(i%65,c,bits)/(i<65?1:2));
        CHECK(__atomic_load_n(&f.filtered,__ATOMIC_RELAXED)==65*ch);
        CHECK(!kosdec_adx_player_stop(&f.player,1000) && mock_joins==2 && info(&f).retained_sound==7);
        unsigned before=calls(&f);for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before);
    }
}
static void failures(void) {
    for(unsigned fail=0;fail<2;++fail) {
        struct fixture f;init(&f,16,2,true);f.wait=!fail;f.fail=fail;
        CHECK(!kosdec_adx_player_start(&f.player));
        if(fail){CHECK(finish(&f)<0 && info(&f).first_error==EIO);}
        else {for(unsigned i=0;i<2000 && !calls(&f);++i)mock_pause();CHECK(calls(&f)>0);CHECK(!kosdec_adx_player_cancel_source(&f.player));}
        CHECK(!kosdec_adx_player_stop(&f.player,1000));unsigned before=calls(&f);
        for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before && mock_joins==1 && !mock_allocs);
    }
    struct fixture f;init(&f,16,1,true);CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
    CHECK(!kosdec_adx_player_release_source(&f.player));
    put(f.data+28,0x400e,2);f.data[30]=0xac;f.data[31]=0x44;
    CHECK(kosdec_adx_aiff_init(&f.aiff,source,&f,f.bytes)==KOSDEC_ADX_MORE);
    CHECK(!kosdec_adx_player_bind_aiff(&f.player,&f.aiff));CHECK(finish(&f)<0 && info(&f).first_error==EPROTO && mock_frames==65);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
}
static void service(void) {
    struct fixture f;init(&f,16,2,false);struct kosdec_adx_service s;
    CHECK(kosdec_adx_aiff_set_filter(&f.aiff,filter,&f)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_service_init_aiff(&s,&f.aiff,f.pcm,1024,32)==KOSDEC_ADX_MORE);
    for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(!calls(&f));struct kosdec_adx_command cmd={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_MORE);
    for(unsigned i=0;i<1000 && s.pipe.write_cursor!=32;++i)CHECK(kosdec_adx_service_step(&s).status<KOSDEC_ADX_DONE);
    CHECK(s.pipe.write_cursor==32);unsigned before=calls(&f),filtered=f.filtered;
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_NEED_OUTPUT && calls(&f)==before && f.filtered==filtered);
    int16_t pcm[64];CHECK(kosdec_adx_pipe_read(&s.pipe,pcm,32).frames==32 && calls(&f)==before && f.filtered==filtered);
    for(unsigned i=0;i<32;++i)for(unsigned c=0;c<2;++c)CHECK(pcm[i*2+c]==sample(i,c,16)/2);
    cmd.enabled=true;CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt));CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE && calls(&f)==before);
    for(unsigned kind=KOSDEC_ADX_SEQUENCE_REPEAT;kind<=KOSDEC_ADX_ENQUEUE;++kind) {
        cmd.kind=(enum kosdec_adx_command_kind)kind;cmd.source.read_at=source;cmd.source.bytes=1;
        CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_UNSUPPORTED);
    }
    kosdec_adx_service_request_cancel(&s);CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_CANCELLED && calls(&f)==before && f.filtered==filtered);
}
int main(void){service();playback();failures();printf("AIFF player: %u checks passed\n",checks);return 0;}
