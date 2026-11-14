/* Actual player/fiber/pipe composition over the existing host Audio model. */
#include <kosdec/adx_recovery.h>
#include "player_mock.h"
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks,prepares;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"recovery_player:%u %s errno=%d\n",(unsigned)__LINE__,#x,errno); abort(); } } while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_stream source;
    uint8_t *data;size_t bytes;bool fault;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static struct kosdec_adx_input_result input(void *context,uint32_t off,uint8_t *out,size_t n) {
    struct fixture *f=context;
    if(f->fault) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(n>17) n=17;
    memcpy(out,f->data+off,n);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void init(struct fixture *f) {
    memset(f,0,sizeof(*f));f->data=stream(50,18,2,32,65,&f->bytes);
    memset(f->data+20,0,24);
    kosdec_adx_stream_init(&f->source,input,f,(uint32_t)f->bytes,false);
    struct kosdec_adx_player_config config={.pcm=f->pcm,.scratch=f->scratch,.pcm_samples=1024,
        .capacity=512,.scratch_samples=257,.stack=f->stack,.stack_bytes=sizeof(f->stack),
        .sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init(&f->player,&f->source,NULL,&config));
}
static int prepare(void *context,const struct kosdec_adx_player_info *old,struct kosdec_adx_player **next) {
    CHECK(old->state==KOSDEC_ADX_PLAYER_STOPPED && old->first_error==EIO);
    ++prepares;*next=&((struct fixture *)context)->player;return 0;
}
static void linked_player(void) {
    mock_reset();struct fixture f;init(&f);
    f.data=realloc(f.data,2*f.bytes);CHECK(f.data);
    memcpy(f.data+f.bytes,f.data,f.bytes);f.bytes*=2;
    kosdec_adx_stream_init(&f.source,input,&f,(uint32_t)f.bytes,false);
    CHECK(kosdec_adx_stream_set_linked(&f.source,true)==KOSDEC_ADX_DONE);
    CHECK(!kosdec_adx_player_start(&f.player));bool done=false;
    for(unsigned n=0;n<3000;++n) {
        CHECK(!kosdec_adx_player_poll(&f.player));
        struct kosdec_adx_player_info info;CHECK(!kosdec_adx_player_get_info(&f.player,&info));
        if(info.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
            CHECK(info.source_frames==130 && f.source.linked_entries==2 && info.linked);done=true;break;
        }
        mock_pause();
    }
    CHECK(done && !kosdec_adx_player_stop(&f.player,1000));free(f.data);
}
int main(void) {
    for(unsigned kind=0;kind<3;++kind) {
        mock_reset();prepares=0;
        struct fixture first,next;init(&first);init(&next);
        if(kind==2) first.data[0]=0;else first.fault=true;
        struct kosdec_adx_recovery r;
        CHECK(!kosdec_adx_recovery_init(&r,&first.player,kind==0?KOSDEC_ADX_RECOVERY_STOP:
            KOSDEC_ADX_RECOVERY_REPLAY,2,1,prepare,&next));
        bool finished=false;
        for(unsigned n=0;n<3000;++n) {
            int result=kosdec_adx_recovery_step(&r,n,1000);
            CHECK(result>=0);
            struct kosdec_adx_player_info pi;
            CHECK(!kosdec_adx_player_get_info(r.player,&pi));
            if(kind==1 && pi.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
                CHECK(prepares==1 && pi.source_frames==65);
                CHECK(!kosdec_adx_player_stop(r.player,1000));
                CHECK(kosdec_adx_recovery_step(&r,n,1000)==1);finished=true;break;
            }
            if(result==1) { CHECK(kind!=1 && prepares==0);finished=true;break; }
            mock_pause();
        }
        CHECK(finished);
        struct kosdec_adx_recovery_info info;
        CHECK(!kosdec_adx_recovery_get_info(&r,&info));
        CHECK(info.first_error==EIO && info.replays==(kind==1));
        CHECK(!kosdec_adx_player_stop(&first.player,1000));
        CHECK(!kosdec_adx_player_stop(&next.player,1000));
        free(first.data);free(next.data);
    }
    linked_player();
    printf("ADX_RECOVERY_PLAYER checks=%u failures=0\n",checks);return 0;
}
