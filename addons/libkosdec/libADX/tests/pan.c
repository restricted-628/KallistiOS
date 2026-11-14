#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <stdalign.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"PAN_FAIL line=%u errno=%d\n",(unsigned)__LINE__,errno);abort();}}while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_stream stream;
    uint8_t *data; size_t bytes;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static struct kosdec_adx_input_result input(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=ctx;
    if(cap>17)cap=17;
    memcpy(dst,f->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void init(struct fixture *f,unsigned channels) {
    memset(f,0,sizeof(*f));mock_reset();
    f->data=stream(50,18,channels,32,65,&f->bytes);memset(f->data+20,0,24);
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
    struct kosdec_adx_player_config c={.pcm=f->pcm,.scratch=f->scratch,.pcm_samples=1024,
        .capacity=512,.scratch_samples=257,.stack=f->stack,.stack_bytes=sizeof(f->stack),
        .sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init(&f->player,&f->stream,NULL,&c));
}
static int pan(struct fixture *f,unsigned ch) {
    int v=99;CHECK(!kosdec_adx_player_get_pan(&f->player,ch,&v));return v;
}
/* Independent register-position interpretation of the existing KOS byte API. */
static int position(unsigned byte) {
    if(byte<128)return -(int)((127-byte)>>3);
    return (int)((byte-128)>>3);
}
static void poll_ready(struct fixture *f) {
    for(unsigned i=0;i<3000;++i) {
        bool ready;CHECK(!kosdec_adx_player_poll(&f->player));
        CHECK(!kosdec_adx_player_is_ready_start(&f->player,&ready));
        if(ready)return;mock_pause();
    }
    CHECK(0);
}
static void invalid(struct fixture *f) {
    int out=99;
    CHECK(kosdec_adx_player_get_pan(NULL,0,&out)<0 && errno==EINVAL && out==99);
    CHECK(kosdec_adx_player_get_pan(&f->player,2,&out)<0 && errno==EINVAL && out==99);
    CHECK(kosdec_adx_player_get_pan(&f->player,0,NULL)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_set_pan(NULL,0,0)<0 && errno==EINVAL);
    int values[]={INT_MIN,-129,-127,-16,16,127,INT_MAX};
    struct kosdec_adx_player before=f->player;unsigned updates=mock_updates;
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        CHECK(kosdec_adx_player_set_pan(&f->player,0,values[i])<0 && errno==EINVAL);
        CHECK(!memcmp(&before,&f->player,sizeof(before)) && mock_updates==updates);
    }
    CHECK(kosdec_adx_player_set_pan(&f->player,UINT_MAX,0)<0 && errno==EINVAL);
    CHECK(!memcmp(&before,&f->player,sizeof(before)) && mock_updates==updates);
}
static void positions(struct fixture *f,bool live) {
    for(unsigned ch=0;ch<2;++ch)for(int v=-15;v<=15;++v) {
        int other=pan(f,1-ch);snd_stream_config_t before=f->player.output;
        unsigned updates=mock_updates;
        CHECK(!kosdec_adx_player_set_pan(&f->player,ch,v));
        CHECK(pan(f,ch)==v && pan(f,1-ch)==other);
        CHECK(position(f->player.output.channel[ch].pan)==v);
        before.channel[ch].pan=f->player.output.channel[ch].pan;
        CHECK(!memcmp(&before,&f->player.output,sizeof(before)));
        CHECK(mock_updates==updates+(live?1u:0u));
        if(live)CHECK(mock_update_fields==SND_CHANNEL_UPDATE_PAN &&
            !memcmp(&mock_output,&f->player.output,sizeof(mock_output)));
    }
}
static void cases(unsigned channels) {
    struct fixture f;init(&f,channels);
    CHECK(pan(&f,0)==KOSDEC_ADX_PAN_AUTO && pan(&f,1)==KOSDEC_ADX_PAN_AUTO);
    invalid(&f);positions(&f,false);
    /* Old API keeps exact bytes and supplies normalized positions to getter. */
    for(unsigned b=0;b<256;++b) {
        CHECK(!kosdec_adx_player_set_output(&f.player,47,83,b,255-b));
        CHECK(f.player.output.channel[0].pan==b && f.player.output.channel[1].pan==255-b);
        CHECK(pan(&f,0)==position(b) && pan(&f,1)==position(255-b));
    }
    CHECK(!kosdec_adx_player_set_pan(&f.player,0,7));
    CHECK(!kosdec_adx_player_set_pan(&f.player,1,KOSDEC_ADX_PAN_AUTO));
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));
    CHECK(!kosdec_adx_player_start(&f.player));poll_ready(&f);
    CHECK(!mock_starts && !mock_updates);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_player_poll(&f.player));
    CHECK(position(mock_output.channel[0].pan)==7 && mock_output.channel[1].pan==255);
    CHECK(mock_output.channel[0].volume==47 && mock_output.channel[1].volume==83);
    CHECK(pan(&f,1)==KOSDEC_ADX_PAN_AUTO);
    positions(&f,true);
    CHECK(!kosdec_adx_player_set_pan(&f.player,0,KOSDEC_ADX_PAN_AUTO));
    CHECK(position(mock_output.channel[0].pan)==(channels==1?0:-15));
    CHECK(!kosdec_adx_player_set_pan(&f.player,1,-6));
    /* Join/rebind keeps independent AUTO, explicit pan and volume settings. */
    CHECK(!kosdec_adx_player_release_source(&f.player));
    CHECK(!kosdec_adx_player_set_pan(&f.player,1,12));
    kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
    CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));poll_ready(&f);
    CHECK(mock_starts==1 && mock_allocs==1);
    CHECK(position(mock_output.channel[0].pan)==(channels==1?0:-15));
    CHECK(position(mock_output.channel[1].pan)==12 && pan(&f,0)==KOSDEC_ADX_PAN_AUTO);
    CHECK(mock_output.channel[0].volume==47 && mock_output.channel[1].volume==83);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(pan(&f,0)==KOSDEC_ADX_PAN_AUTO && pan(&f,1)==12);
    CHECK(kosdec_adx_player_set_pan(&f.player,0,0)<0 && errno==EINVAL);
    free(f.data);
}
static void failed_update(void) {
    struct fixture f;init(&f,2);CHECK(!kosdec_adx_player_start(&f.player));poll_ready(&f);
    CHECK(!kosdec_adx_player_set_pan(&f.player,1,9));
    snd_stream_config_t before=f.player.output;unsigned updates=mock_updates;
    mock_failure=MOCK_UPDATE;
    CHECK(kosdec_adx_player_set_pan(&f.player,1,-5)<0 && errno==EIO);
    CHECK(mock_updates==updates+1 && f.player.info.state==KOSDEC_ADX_PLAYER_FAILED);
    CHECK(!memcmp(&before,&f.player.output,sizeof(before)) && pan(&f,1)==9);
    CHECK(f.player.info.first_error==EIO);
    CHECK(kosdec_adx_player_set_pan(&f.player,0,0)<0 && errno==EINVAL && mock_updates==updates+1);
    mock_failure=MOCK_OK;CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
}
int main(void) {
    cases(1);cases(2);failed_update();
    printf("ADX_PAN checks=%u PASS\n",checks);return 0;
}
