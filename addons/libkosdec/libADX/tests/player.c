#include <kosdec/adx_player.h>
#include <kosdec/adx_compat_service.h>
#include "player_mock.h"
#include <stdalign.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"PLAYER_FAIL line=%u errno=%d\n",(unsigned)__LINE__,errno);abort();}}while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_stream stream;
    struct kosdec_adx_sequence sequence;
    struct kosdec_adx_player_config config;
    uint8_t *data;size_t bytes, read_limit;unsigned calls;bool fail,wait;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static struct kosdec_adx_input_result input(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=ctx;++f->calls;
    if(f->fail)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(off<f->bytes && cap<=f->bytes-off);
    if(cap>f->read_limit)cap=f->read_limit;
    memcpy(dst,f->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void init(struct fixture *f,unsigned channels,bool sequence) {
    memset(f,0,sizeof(*f));mock_reset();f->data=stream(50,18,channels,32,65,&f->bytes);
    f->read_limit=17;
    memset(f->data+20,0,24);
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
    kosdec_adx_sequence_init(&f->sequence,false);
    struct kosdec_adx_source src={input,f,(uint32_t)f->bytes,17};
    for(unsigned n=0;n<3;++n)CHECK(kosdec_adx_sequence_enqueue(&f->sequence,&src)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_sequence_start(&f->sequence)==KOSDEC_ADX_MORE);
    f->config=(struct kosdec_adx_player_config){.pcm=f->pcm,.scratch=f->scratch,
        .pcm_samples=1024,.capacity=512,.scratch_samples=257,.stack=f->stack,
        .stack_bytes=sizeof(f->stack),.sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(kosdec_adx_player_init(&f->player,sequence?NULL:&f->stream,
        sequence?&f->sequence:NULL,&f->config)==0);
}
static struct kosdec_adx_player_info info(struct fixture *f) {
    struct kosdec_adx_player_info i;CHECK(!kosdec_adx_player_get_info(&f->player,&i));return i;
}
static int until_sound(struct fixture *f) {
    for(unsigned i=0;i<2000;++i) {
        if(kosdec_adx_player_poll(&f->player)<0)return -1;
        if(info(f).state!=KOSDEC_ADX_PLAYER_PREFILL)return 0;
        mock_pause();
    }
    CHECK(false);return -1;
}
static void complete(void) {
    for(unsigned channels=1;channels<=2;++channels)for(unsigned sequence=0;sequence<2;++sequence) {
        struct fixture f;init(&f,channels,sequence);
        CHECK(!kosdec_adx_player_start(&f.player));CHECK(!until_sound(&f));
        struct kosdec_adx_player_info i=info(&f);
        CHECK(i.state==KOSDEC_ADX_PLAYER_SOURCE_END && i.decoder==KOSDEC_ADX_DONE);
        CHECK(i.channels==channels && i.sample_rate==44100 && i.copied_frames==(sequence?195u:65u));
        CHECK(i.starvations==0 && mock_frames==i.copied_frames && mock_allocs==1 && mock_starts==1);
        CHECK(mock_output.channel[0].pan==(channels==1?128:0) && mock_output.channel[1].pan==255);
        for(size_t j=0;j<mock_frames;++j)for(unsigned c=0;c<channels;++c)CHECK(mock_pcm[j*channels+c]==golden[c][j%65]);
        CHECK(!kosdec_adx_player_poll(&f.player));CHECK(info(&f).copied_frames==i.copied_frames);
        CHECK(!kosdec_adx_player_set_output(&f.player,127,63,200,50));
        CHECK(mock_updates==1 && mock_output.channel[0].volume==127 && mock_output.channel[1].pan==50);
        CHECK(kosdec_adx_player_set_output(&f.player,256,0,0,0)<0 && mock_updates==1);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));i=info(&f);
        CHECK(i.state==KOSDEC_ADX_PLAYER_STOPPED && i.retained_sound==7 && !i.first_error);
        CHECK(!mock_callback && !mock_userdata && mock_joins==1 && mock_stops==1);
        CHECK(!kosdec_adx_player_stop(&f.player,1000) && mock_joins==1 && mock_stops==1);
        CHECK(kosdec_adx_player_start(&f.player)<0 && kosdec_adx_player_poll(&f.player)<0);
        free(f.data);
    }
}
static void staged_input(void) {
    for(unsigned sequence=0;sequence<2;++sequence) {
        struct fixture f;
        init(&f,2,sequence!=0);
        f.read_limit=512;
        f.config.capacity=32;
        f.config.sound_bytes=64;
        f.config.use_lsc_reload=true;
        CHECK(!kosdec_adx_player_init(&f.player,sequence?NULL:&f.stream,
                                     sequence?&f.sequence:NULL,&f.config));
        struct kosdec_adx_player_input input;
        CHECK(!kosdec_adx_player_get_input(&f.player,&input) &&
              !input.joint && !input.queued_bytes && !input.safe);
        CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,1));
        CHECK(!kosdec_adx_player_start(&f.player));
        const uint32_t staged=(uint32_t)f.bytes-50-36;
        /* No consumer is running. After one group the PCM ring stays full. */
        for(unsigned i=0;i<1000;++i) {
            CHECK(!kosdec_adx_player_get_input(&f.player,&input));
            if(input.queued_bytes==staged) break;
            mock_pause();
        }
        CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PREFILL);
        CHECK(!input.joint && !input.sectors && input.queued_bytes==staged &&
              input.seconds==64.0f/44100.0f &&
              input.safety_limit_bytes==2048 && !input.safe);
        CHECK(!kosdec_adx_player_set_linked_reload_time(&f.player,1.0f,2,44100));
        CHECK(!kosdec_adx_player_get_input(&f.player,&input) &&
              input.queued_bytes==staged && !input.safety_limit_bytes && input.safe);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(f.calls==1);
        CHECK(!kosdec_adx_player_get_input(&f.player,&input) &&
              !input.joint && !input.queued_bytes && !input.seconds && !input.safe);
        free(f.data);
    }
}
struct compat_input {
    struct fixture *fixture;
    uint32_t visible;
};
static struct kosdec_adx_input_result compat_read(void *context, uint32_t offset,
    uint8_t *bytes, size_t count) {
    struct compat_input *source = context;
    uint32_t limit = __atomic_load_n(&source->visible,__ATOMIC_ACQUIRE);
    if(offset >= limit)
        return (struct kosdec_adx_input_result){.status = KOSDEC_ADX_INPUT_WAIT};
    if(count > limit - offset)
        count = limit - offset;
    return input(source->fixture,offset,bytes,count);
}
static uint64_t compat_clock(void *context) {
    return *(uint64_t *)context;
}
static void compat_start_threshold(void) {
    struct fixture f;
    init(&f,2,false);
    alignas(32) int16_t pcm[8192];
    enum { HEADER = 50, GROUP_SIZE = 36, TRACK_FRAMES = 4096 };
    size_t bytes = HEADER + (TRACK_FRAMES / 32) * GROUP_SIZE + 18;
    uint8_t *track = calloc(bytes,1);
    CHECK(track != NULL);
    memcpy(track,f.data,HEADER);
    memcpy(track + bytes - 18,f.data + f.bytes - 18,18);
    put32(track + 12,TRACK_FRAMES);
    free(f.data);
    f.data = track;
    f.bytes = bytes;
    f.read_limit = 512;
    f.config.pcm = pcm;
    f.config.pcm_samples = 8192;
    f.config.capacity = 4096;
    struct compat_input source = {&f,HEADER + 35 * GROUP_SIZE};
    kosdec_adx_stream_init(&f.stream,compat_read,&source,(uint32_t)bytes,false);
    CHECK(!kosdec_adx_player_init(&f.player,&f.stream,NULL,&f.config));
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock,.clock_context = &now};
    struct kosdec_adx_compat_service service, other;
    struct kosdec_adx_compat_title titles[2], other_titles[1];
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_service_init(&service,titles,2,&config));
    CHECK(!kosdec_adx_compat_service_init(&other,other_titles,1,&config));
    CHECK(!kosdec_adx_compat_bind_player(&service,1,&f.player));
    CHECK(kosdec_adx_compat_bind_player(&other,0,&f.player)<0 && errno==EBUSY);
    CHECK(!kosdec_adx_compat_exec_server(&service,&r) && r.visited==2 && !mock_starts);
    CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(kosdec_adx_player_poll(&f.player)<0 && errno==EBUSY);
    CHECK(kosdec_adx_compat_unbind(&service,1)<0 && errno==EBUSY);

    struct kosdec_adx_decode_progress progress;
    bool held = false;
    for(unsigned i=0;i<1000;++i) {
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
        if(kosdec_adx_player_get_decode_progress(&f.player,&progress)==0 &&
           progress.decoded_frames==1120) {
            held = true;
            break;
        }
        mock_pause();
    }
    CHECK(held && !info(&f).ready_start && !mock_starts && f.player.compat_prefill_frames==1152);
    CHECK(!kosdec_adx_compat_set_frequency(&service,1,2));
    for(unsigned admitted=0;admitted<40;++admitted) {
        now += 1000000;
        CHECK(!kosdec_adx_compat_exec_title(&service,1,&r) &&
              r.watchdog_checks==2 && !r.watchdog_deferred);
    }
    enum kosdec_adx_compat_error error;
    CHECK(!kosdec_adx_compat_get_error(&service,1,&error) && error==KOSDEC_ADX_COMPAT_NO_ERROR);
    now += 1000000;
    CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(!kosdec_adx_compat_get_error(&service,1,&error) && error==KOSDEC_ADX_COMPAT_LOW_DATA);
    CHECK(!kosdec_adx_compat_clear_error(&service,1));
    CHECK(!kosdec_adx_compat_set_frequency(&service,1,120));
    CHECK(!kosdec_adx_compat_exec_server(&service,&r) && !mock_starts);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));

    __atomic_store_n(&source.visible,HEADER + 36 * GROUP_SIZE,__ATOMIC_RELEASE);
    for(unsigned i=0;i<1000 && !info(&f).ready_start;++i) {
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
        mock_pause();
    }
    CHECK(info(&f).ready_start && !mock_starts);
    CHECK(!kosdec_adx_player_get_decode_progress(&f.player,&progress) &&
          progress.decoded_frames==1152);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_compat_exec_server(&service,&r) && mock_starts==1);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PLAYING);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,1));
    CHECK(!kosdec_adx_compat_exec_server(&service,&r) && !r.visited);
    free(track);
}

static void compat_prepare_room(void) {
    struct fixture f;
    init(&f,2,false);
    enum { HEADER=50, GROUP=36, FRAMES=257 };
    size_t bytes=HEADER+((FRAMES+31)/32)*GROUP+18;
    uint8_t *track=calloc(bytes,1);
    CHECK(track != NULL);
    memcpy(track,f.data,HEADER);
    memcpy(track+bytes-18,f.data+f.bytes-18,18);
    put32(track+12,FRAMES);
    free(f.data);
    f.data=track;
    f.bytes=bytes;
    f.read_limit=512;
    f.config.capacity=128;
    f.config.sound_bytes=256;

    struct compat_input source={&f,HEADER+2*GROUP};
    kosdec_adx_stream_init(&f.stream,compat_read,&source,(uint32_t)bytes,false);
    CHECK(!kosdec_adx_player_init(&f.player,&f.stream,NULL,&f.config));
    uint64_t now=0;
    const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));
    CHECK(!kosdec_adx_player_start(&f.player));
    for(unsigned i=0;i<5;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(f.player.compat_prefill_frames==1152 && f.stream.source_frame==64);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PREFILL && !info(&f).ready_start && !mock_starts);

    source.visible=HEADER+3*GROUP;
    for(unsigned i=0;i<3;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(f.stream.source_frame==96 && info(&f).ready_start && !mock_starts);
    /* Model a client that holds the prepared source through its first deadline. */
    f.player.prefill_deadline=0;
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && !r.serviced_phases);
    CHECK(info(&f).ready_start && !mock_starts);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && !r.serviced_phases);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PREFILL && !mock_starts);

    source.visible=(uint32_t)bytes;
    bool played=false;
    for(unsigned i=0;i<50 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++i) {
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
        played|=info(&f).state==KOSDEC_ADX_PLAYER_PLAYING;
    }
    CHECK(played && info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END);
    CHECK(mock_starts==1 && mock_frames==FRAMES);
    for(size_t i=0;i<mock_frames*2;++i) CHECK(mock_pcm[i]==0);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(track);
}

static void check_prepared(struct fixture *f, bool current, bool latched) {
    bool ready=!current, legacy=!latched;
    CHECK(!kosdec_adx_player_is_ready_start(&f->player,&ready) && ready==current);
    CHECK(!kosdec_adx_compat_player_is_ready_start(&f->player,&legacy) && legacy==latched);
}

static void compat_preoutput_pause(bool phased) {
    struct fixture f;
    init(&f,2,false);
    f.read_limit=512;
    uint64_t now=0;
    struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    struct kosdec_adx_time time;
    enum kosdec_adx_compat_state state;
    int32_t removed=-999;
    CHECK(kosdec_adx_compat_player_pause(&f.player,true)<0 && errno==ENOTSUP);
    CHECK(kosdec_adx_compat_player_discard_samples(&f.player,3,&removed)<0 &&
          errno==ENOTSUP && removed==-999);
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_enable_vsync_clock(&service));
    CHECK(!(phased ? kosdec_adx_compat_bind_phased_player(&service,0,&f.player) :
                     kosdec_adx_compat_bind_player(&service,0,&f.player)));
    CHECK(!kosdec_adx_compat_player_discard_samples(&f.player,-17,&removed) && !removed);
    CHECK(!kosdec_adx_compat_player_set_time_mode(&f.player,KOSDEC_ADX_TIME_SOURCE));
    CHECK(!kosdec_adx_compat_player_pause(&f.player,true));
    CHECK(!kosdec_adx_compat_player_get_time(&f.player,&time) && !time.count && time.frequency==1);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));
    CHECK(!kosdec_adx_player_start(&f.player));
    for(unsigned i=0;i<2000 && !info(&f).ready_start;++i) {
        CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
        if(!phased) mock_pause();
    }
    check_prepared(&f,true,true);
    CHECK(!kosdec_adx_compat_player_get_state(&f.player,&state) && state==KOSDEC_ADX_COMPAT_PREP);
    CHECK(!mock_allocs && !mock_starts && !info(&f).production_paused);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
    CHECK(!kosdec_adx_compat_player_get_state(&f.player,&state) && state==KOSDEC_ADX_COMPAT_PLAYING);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PREFILL && !mock_allocs);
    CHECK(!kosdec_adx_compat_player_get_time(&f.player,&time) && !time.count && time.frequency==44100);
    CHECK(!kosdec_adx_compat_player_set_time_mode(&f.player,KOSDEC_ADX_TIME_PAL));

    f.player.prefill_deadline=0;
    for(unsigned i=0;i<8;++i) {
        now+=20000;
        CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
        CHECK(!kosdec_adx_compat_player_get_time(&f.player,&time) && !time.count && time.frequency==5000);
    }
    uint32_t tick=service.vsync_count;
    removed=-999;
    CHECK(kosdec_adx_compat_player_discard_samples(&f.player,5,&removed)<0 &&
          errno==ENOTSUP && removed==-999 && service.vsync_count==tick);
    CHECK(!mock_allocs && !mock_frames);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true)); /* Already logically PLAYING. */
    CHECK(!kosdec_adx_compat_player_pause(&f.player,false) && f.player.prefill_deadline);
    CHECK(!kosdec_adx_compat_player_discard_samples(&f.player,5,&removed) &&
          !removed && service.vsync_count==tick);
    for(unsigned i=0;i<2000 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++i) {
        CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
        if(!phased) mock_pause();
    }
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END);
    CHECK(mock_allocs==1 && mock_starts==1 && mock_frames==65);
    for(unsigned i=0;i<65;++i)
        for(unsigned c=0;c<2;++c) CHECK(mock_pcm[i*2+c]==golden[c][i]);
    CHECK(kosdec_adx_compat_player_pause(&f.player,true)<0 && errno==ENOTSUP);
    CHECK(!kosdec_adx_compat_player_discard_samples(&f.player,5,&removed) && !removed);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!kosdec_adx_compat_player_pause(&f.player,true));
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    removed=-999;
    CHECK(kosdec_adx_compat_player_discard_samples(&f.player,5,&removed)<0 &&
          errno==ENOTSUP && removed==-999);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

static void compat_ready_lifecycle(void) {
    struct fixture f;
    init(&f,2,false);
    f.read_limit=512;
    bool latched=true;
    CHECK(kosdec_adx_compat_player_is_ready_start(&f.player,&latched)<0 && errno==ENOTSUP && latched);
    uint64_t now=0;
    const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
    check_prepared(&f,false,false);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));
    CHECK(!kosdec_adx_player_start(&f.player));
    for(unsigned i=0;i<10 && !info(&f).ready_start;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    check_prepared(&f,true,true);
    CHECK(!mock_starts);
    unsigned calls=f.calls;
    CHECK(!kosdec_adx_player_cancel_source(&f.player) && f.calls==calls);
    check_prepared(&f,false,true);
    CHECK(kosdec_adx_player_bind_source(&f.player,NULL,NULL)<0 && errno==ENOTSUP);
    check_prepared(&f,false,true);

    kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
    CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL) && f.calls==calls);
    check_prepared(&f,false,false);
    for(unsigned i=0;i<10 && !info(&f).ready_start;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    check_prepared(&f,true,true);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && !r.serviced_phases);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PLAYING && mock_starts==1);

    mock_failure=MOCK_STOP_TIMEOUT;
    CHECK(kosdec_adx_player_stop(&f.player,1000)<0 && errno==ETIMEDOUT);
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_STOPPING);
    check_prepared(&f,false,true);
    mock_failure=MOCK_OK;
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_STOPPED);
    check_prepared(&f,false,true);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    CHECK(kosdec_adx_compat_player_is_ready_start(&f.player,&latched)<0 && errno==ENOTSUP && latched);
    free(f.data);
}

static void compat_delayed_footer(void) {
    for(unsigned damaged = 0; damaged < 2; ++damaged) {
        struct fixture f;
        init(&f,2,false);
        f.read_limit = 512;
        f.config.sound_bytes = 64;
        struct compat_input source = {&f,(uint32_t)f.bytes - 18};
        kosdec_adx_stream_init(&f.stream,compat_read,&source,(uint32_t)f.bytes,false);
        CHECK(!kosdec_adx_player_init(&f.player,&f.stream,NULL,&f.config));
        uint64_t now = 0;
        const struct kosdec_adx_compat_config config = {.clock_us = compat_clock,.clock_context = &now};
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        struct kosdec_adx_compat_result result;
        enum kosdec_adx_compat_state state = KOSDEC_ADX_COMPAT_STOP;
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
        CHECK(!kosdec_adx_compat_set_frequency(&service,0,1200));
        CHECK(!kosdec_adx_player_start(&f.player));
        for(unsigned pass = 0; pass < 12 && state != KOSDEC_ADX_COMPAT_DECEND; ++pass) {
            CHECK(!kosdec_adx_compat_exec_server(&service,&result));
            CHECK(!kosdec_adx_compat_player_get_state(&f.player,&state));
        }
        CHECK(state == KOSDEC_ADX_COMPAT_DECEND);
        CHECK(info(&f).state == KOSDEC_ADX_PLAYER_PLAYING && !mock_stops);
        struct kosdec_adx_decode_progress progress;
        CHECK(!kosdec_adx_player_get_decode_progress(&f.player,&progress));
        CHECK(progress.samples_complete && progress.status == KOSDEC_ADX_MORE);
        CHECK(!kosdec_adx_compat_exec_title(&service,0,&result));
        CHECK(info(&f).state == KOSDEC_ADX_PLAYER_PLAYING);

        if(damaged)
            f.data[f.bytes - 18] = 0;
        source.visible = (uint32_t)f.bytes;
        int rv = 0;
        for(unsigned pass = 0; pass < 8 && !rv && info(&f).state == KOSDEC_ADX_PLAYER_PLAYING; ++pass)
            rv = kosdec_adx_compat_exec_server(&service,&result);
        if(damaged)
            CHECK(rv < 0 && errno == EIO && info(&f).state == KOSDEC_ADX_PLAYER_FAILED);
        else
            CHECK(!rv && info(&f).state == KOSDEC_ADX_PLAYER_SOURCE_END);
        CHECK(!mock_stops);
        if(!damaged) {
            CHECK(mock_frames == 65);
            for(unsigned frame = 0; frame < 65; ++frame)
                for(unsigned ch = 0; ch < 2; ++ch)
                    CHECK(mock_pcm[frame * 2 + ch] == golden[ch][frame]);
        }
        bool completed = true;
        CHECK(!kosdec_adx_compat_player_is_completed(&f.player,&completed) && !completed);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f.data);
    }
}

static void compat_inactive_values(struct kosdec_adx_player *p) {
    static const struct {
        int32_t offset;
        int32_t centiseconds[3];
    } cases[] = {
        {0, {0, 0, 0}},
        {-59, {-5900, 0, -1}},
        {60, {6000, 1, 1}},
        {-1234, {-123400, -20, -24}},
        {16777217, {1677721600, 279900, 335544}},
        {INT32_MAX, {INT32_MAX, 35827220, 42949672}},
        {INT32_MIN, {INT32_MIN, -35827220, -42949672}},
    };
    static const uint32_t frequency[] = {1, 5994, 5000};
    for(unsigned mode = 0; mode < 3; ++mode) {
        CHECK(!kosdec_adx_compat_player_set_time_mode(p,(enum kosdec_adx_time_mode)mode));
        for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            struct kosdec_adx_time out = {91, 93};
            int64_t centiseconds = 97;
            CHECK(!kosdec_adx_player_set_time_offset(p,cases[i].offset));
            CHECK(!kosdec_adx_compat_player_get_time(p,&out));
            CHECK(out.count == cases[i].offset && out.frequency == frequency[mode]);
            CHECK(!kosdec_adx_compat_player_get_time_real(p,&centiseconds));
            CHECK(centiseconds == cases[i].centiseconds[mode]);
        }
    }
}

static void compat_inactive_time(void) {
    struct fixture f;
    init(&f,2,false);
    f.read_limit=512;
    struct kosdec_adx_time out={91,93};
    int64_t centiseconds=97;
    CHECK(kosdec_adx_compat_player_get_time(&f.player,&out)<0 && errno==ENOTSUP);
    CHECK(kosdec_adx_compat_player_set_time_mode(&f.player,KOSDEC_ADX_TIME_PAL)<0 && errno==ENOTSUP);
    CHECK(out.count==91 && out.frequency==93);
    uint64_t now=0;
    const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
    compat_inactive_values(&f.player);
    CHECK(kosdec_adx_player_set_time_mode(&f.player,KOSDEC_ADX_TIME_NTSC)<0 && errno==ENOTSUP);
    CHECK(kosdec_adx_player_get_time(&f.player,0,&out)<0 && errno==EINVAL);

    CHECK(!kosdec_adx_player_set_wait_start(&f.player,true));
    CHECK(!kosdec_adx_player_start(&f.player) && !f.player.compat_prefill_frames);
    compat_inactive_values(&f.player); /* DECINFO before any source call. */
    for(unsigned i=0;i<10 && !info(&f).ready_start;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(f.player.compat_prefill_frames && info(&f).ready_start && !mock_starts);
    compat_inactive_values(&f.player); /* Held PREP. */
    CHECK(kosdec_adx_compat_player_set_time_mode(&f.player,(enum kosdec_adx_time_mode)3)<0 &&
          errno==EINVAL);
    CHECK(!kosdec_adx_compat_player_get_time(&f.player,&out) && out.frequency==5000);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player,false));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && info(&f).state==KOSDEC_ADX_PLAYER_PLAYING);
    CHECK(kosdec_adx_compat_player_get_time_real(&f.player,&centiseconds)<0 &&
          errno==ENOTSUP && centiseconds==97);
    mock_failure=MOCK_STOP_TIMEOUT;
    CHECK(kosdec_adx_player_stop(&f.player,1000)<0 && errno==ETIMEDOUT);
    CHECK(kosdec_adx_compat_player_get_time_real(&f.player,&centiseconds)<0 &&
          errno==ENODATA && centiseconds==97);
    mock_failure=MOCK_OK;
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    compat_inactive_values(&f.player); /* Checked STOP even with a prior timeout. */
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

static void compat_attenuation_cache(void) {
    struct fixture f;
    init(&f,2,false);
    struct kosdec_adx_player *p = &f.player;
    uint64_t now = 0, request = 91;
    int16_t cached = 93;
    const struct kosdec_adx_compat_config config = {.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    CHECK(kosdec_adx_compat_player_set_attenuation(p,-60,&request)<0 && errno==ENOTSUP && request==91);
    CHECK(kosdec_adx_compat_player_get_requested_attenuation(p,&cached)<0 && errno==ENOTSUP && cached==93);
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && !cached);
    static const struct { int32_t input; int16_t expected; } cases[] = {
        {-999,-999}, {-65537,-1}, {32768,INT16_MIN}, {INT32_MIN,0}, {-60,-60}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        request=91;
        CHECK(!kosdec_adx_compat_player_set_attenuation(p,cases[i].input,&request) && !request);
        CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==cases[i].expected);
        CHECK(!kosdec_adx_player_get_requested_attenuation(p,&cached) && !cached);
    }
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && !cached);
    CHECK(!kosdec_adx_player_set_wait_start(p,true));
    CHECK(!kosdec_adx_player_start(p));
    struct kosdec_adx_compat_result result;
    for(unsigned pass=0; pass<32 && !info(&f).ready_start; ++pass)
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(&f).ready_start && info(&f).state==KOSDEC_ADX_PLAYER_PREFILL && !mock_starts);
    request=91;
    CHECK(!kosdec_adx_compat_player_set_attenuation(p,-60,&request) && !request);
    CHECK(!kosdec_adx_player_stop(p,1000) && !mock_starts);
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-60);
    request=91;
    CHECK(!kosdec_adx_compat_player_set_attenuation(p,-90,&request) && !request);
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-90);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
    puts("ADX_COMPAT_ATTENUATION_CACHE_PASS before start, held PREP, STOP, fresh binding");
}

static void compat_routing_domain(void) {
    /* Final register values observed from linked stock request-domain probes. */
    static const uint8_t direct[] = {
        0, 0, 0, 0, 1, 2, 3, 3, 4, 4, 5, 5,
        6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10,
        10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13,
        14, 14, 14, 14, 15, 15, 15, 15, 15, 15
    };
    static const uint8_t effect[] = {
        0, 0, 0, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10,
        11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14,
        14, 14, 14, 14, 15, 15, 15, 15, 15, 15
    };
    struct fixture f;
    init(&f,2,false);
    struct kosdec_adx_player *p = &f.player;
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    CHECK(kosdec_adx_compat_player_set_direct_level(p,-15)<0 && errno==ENOTSUP);
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    int32_t db, bus, transpose, cents;
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && !db);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && !bus && db==-45);
    CHECK(!kosdec_adx_compat_player_get_transpose(p,&transpose,&cents) && !transpose && !cents);
    CHECK(!kosdec_adx_compat_player_cache_transpose(p,INT32_MIN,INT32_MAX));
    CHECK(!kosdec_adx_compat_player_get_transpose(p,&transpose,&cents) && transpose==-96 && cents==99);
    for(int db_input=-47; db_input<=2; ++db_input) {
        int effective=db_input < -45 ? -45 : db_input > 0 ? 0 : db_input;
        unsigned index = (unsigned)(effective+45);
        CHECK(kosdec_adx_compat_player_set_direct_level(p,db_input)==KOSDEC_ADX_ROUTING_STAGED);
        CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && db==effective);
        for(int bus_input=0; bus_input<=16; ++bus_input) {
            CHECK(kosdec_adx_compat_player_set_fx(p,bus_input,db_input)==KOSDEC_ADX_ROUTING_STAGED);
            CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==bus_input && db==effective);
            for(unsigned ch=0;ch<2;++ch) {
                snd_channel_routing_t r=p->output.channel[ch].routing;
                CHECK(r.direct_level==direct[index] && r.effect_send==effect[index] &&
                      r.effect_channel==(bus_input & 15));
            }
        }
        CHECK(kosdec_adx_compat_player_set_fx(p,-1,-20)<0 && errno==EINVAL);
        CHECK(kosdec_adx_compat_player_set_fx(p,17,-20)<0 && errno==EINVAL);
        CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==16 && db==effective);
    }
    CHECK(kosdec_adx_compat_player_set_direct_level(p,INT32_MIN)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && db==-45);
    CHECK(kosdec_adx_compat_player_set_fx_level(p,INT32_MIN)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==16 && db==-45);
    CHECK(!mock_allocs && !mock_updates);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    for(unsigned ch=0;ch<2;++ch) {
        snd_channel_routing_t r=p->output.channel[ch].routing;
        CHECK(r.direct_level==15 && !r.effect_send && !r.effect_channel);
    }
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && !db);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && !bus && db==-45);
    CHECK(!kosdec_adx_compat_player_get_transpose(p,&transpose,&cents) && !transpose && !cents);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

static void compat_routing_live(unsigned channels) {
    struct fixture f;
    init(&f,channels,false);
    struct kosdec_adx_player *p=&f.player;
    uint64_t now=0;
    const struct kosdec_adx_compat_config config = {.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_player_set_rate(p,32000));
    CHECK(!kosdec_adx_player_set_output(p,200,170,143,72));
    CHECK(kosdec_adx_compat_player_set_direct_level(p,-15)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(kosdec_adx_compat_player_set_fx(p,7,-12)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(!kosdec_adx_compat_player_cache_transpose(p,12,-50));
    CHECK(!mock_updates && !mock_starts);
    CHECK(!kosdec_adx_player_start(p));
    for(unsigned i=0;i<32 && !mock_starts;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(mock_starts==1 && mock_output.channels==channels && mock_output.sample_rate==32000);
    for(unsigned ch=0;ch<channels;++ch) {
        CHECK(mock_output.channel[ch].routing.direct_level==12);
        CHECK(mock_output.channel[ch].routing.effect_channel==7 && mock_output.channel[ch].routing.effect_send==13);
    }
    snd_stream_config_t before=mock_output;
    CHECK(kosdec_adx_compat_player_set_fx_level(p,-42)==KOSDEC_ADX_ROUTING_ADMITTED);
    CHECK(mock_update_fields==SND_CHANNEL_UPDATE_ROUTING && mock_updates==1);
    for(unsigned ch=0;ch<channels;++ch) {
        CHECK(mock_output.channel[ch].routing.direct_level==12 &&
              mock_output.channel[ch].routing.effect_channel==7 &&
              mock_output.channel[ch].routing.effect_send==2);
        CHECK(mock_output.channel[ch].volume==before.channel[ch].volume &&
              mock_output.channel[ch].pan==before.channel[ch].pan);
    }
    CHECK(mock_output.sample_rate==before.sample_rate);
    CHECK(kosdec_adx_compat_player_set_direct_level(p,-42)==KOSDEC_ADX_ROUTING_ADMITTED);
    CHECK(mock_update_fields==SND_CHANNEL_UPDATE_ROUTING && mock_updates==2);
    CHECK(mock_output.channel[0].routing.direct_level==0 &&
          mock_output.channel[0].routing.effect_send==2 && mock_output.channel[0].routing.effect_channel==7);
    CHECK(mock_output.channel[1].routing.direct_level==(channels==2 ? 0 : 12));
    for(unsigned i=0;i<32 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++i)
        CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    CHECK(!kosdec_adx_player_release_source(p));
    kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
    CHECK(!kosdec_adx_player_bind_source(p,&f.stream,NULL));
    int32_t bus, db, transpose, cents;
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && db==-42);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==7 && db==-42);
    CHECK(!kosdec_adx_compat_player_get_transpose(p,&transpose,&cents) && transpose==12 && cents==-50);
    if(channels==2) {
        mock_failure=MOCK_ROUTING_SECOND;
        CHECK(kosdec_adx_compat_player_set_direct_level(p,-15)<0 && errno==EPIPE);
        CHECK(mock_update_fields==SND_CHANNEL_UPDATE_ROUTING && mock_updates==3);
        CHECK(mock_output.channel[0].routing.direct_level==12 &&
              mock_output.channel[1].routing.direct_level==0);
        CHECK(p->output.channel[0].routing.direct_level==0 && p->output.channel[1].routing.direct_level==0);
        CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && db==-15);
        CHECK(info(&f).state==KOSDEC_ADX_PLAYER_FAILED && info(&f).first_error==EPIPE);
        CHECK(kosdec_adx_compat_player_set_fx(p,5,-20)<0 && errno==EBUSY && mock_updates==3);
        CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==7 && db==-42);
        mock_failure=MOCK_OK;
    }
    CHECK(!kosdec_adx_player_stop(p,1000));
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && db==(channels==2 ? -15 : -42));
    unsigned updates=mock_updates;
    CHECK(kosdec_adx_compat_player_set_fx(p,16,INT32_MAX)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(mock_updates==updates);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && bus==16 && db==0);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

static void compat_transport_fault(void) {
    for(unsigned truncated=0;truncated<2;++truncated) {
        struct fixture f;
        init(&f,2,false);
        f.fail = !truncated;
        if(truncated)
            kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes-1,false);
        uint64_t now = 0;
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        const struct kosdec_adx_compat_config config = {
            .clock_us = compat_clock,.clock_context = &now
        };
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_player(&service,0,&f.player));
        CHECK(!kosdec_adx_player_start(&f.player));
        const enum kosdec_adx_status expected = truncated ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
        bool terminal = false;
        for(unsigned i=0;i<1000;++i) {
            struct kosdec_adx_decode_progress progress;
            if(!kosdec_adx_player_get_decode_progress(&f.player,&progress) &&
               progress.status==expected) {
                terminal = true;
                break;
            }
            mock_pause();
        }
        CHECK(terminal);
        struct kosdec_adx_compat_result r;
        CHECK(kosdec_adx_compat_exec_title(&service,0,&r)<0 && errno==EIO &&
              r.failed_titles==1 && !r.watchdog_checks);
        uint32_t faults;
        CHECK(!kosdec_adx_compat_get_faults(&service,0,&faults) && !faults);
        now=13332;
        CHECK(kosdec_adx_compat_exec_title(&service,0,&r)<0 && errno==EIO &&
              r.failed_titles==1 && r.watchdog_checks==(truncated?0u:1u));
        CHECK(!kosdec_adx_compat_get_faults(&service,0,&faults) &&
              faults==(truncated?0u:KOSDEC_ADX_COMPAT_FAULT_TRANSPORT));
        enum kosdec_adx_compat_error code;
        CHECK(!kosdec_adx_compat_get_error(&service,0,&code) && code==KOSDEC_ADX_COMPAT_NO_ERROR);
        CHECK(!mock_starts && !mock_stops);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f.data);
    }
}

static void compat_initial_pitch(void) {
    const struct {
        int semitones;
        uint32_t source_rate, native_rate, output_rate;
        bool rebind, cancel;
    } cases[] = {
        {-12, 44100, 0, 22050, false, false},
        {0, 44100, 0, 44100, false, false},
        {12, 44100, 0, 88200, true, false},
        {12, 44100, 32000, 32000, false, false},
        {12, 32000, 0, 0, false, false},
        {12, 44100, 0, 44100, false, true}
    };
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        struct fixture f;
        init(&f,2,false);
        f.read_limit=512;
        uint32_t rate=cases[k].source_rate;
        for(unsigned byte=0;byte<4;++byte)
            f.data[8+byte]=(uint8_t)(rate>>(24-8*byte));
        uint64_t now=0;
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
        CHECK(!kosdec_adx_compat_player_set_initial_transpose(&f.player,cases[k].semitones,0));
        CHECK(!mock_starts && !mock_allocs);
        CHECK(kosdec_adx_compat_player_set_initial_transpose(&f.player,INT32_MAX,INT32_MAX)<0 &&
              errno==ERANGE);
        int32_t semitones, cents;
        CHECK(!kosdec_adx_compat_player_get_transpose(&f.player,&semitones,&cents) &&
              semitones==96 && cents==99);
        CHECK(kosdec_adx_compat_player_set_initial_transpose(&f.player,INT32_MIN,INT32_MIN)<0 &&
              errno==ERANGE);
        CHECK(!kosdec_adx_compat_player_get_transpose(&f.player,&semitones,&cents) &&
              semitones==-96 && cents==-99);
        CHECK(kosdec_adx_compat_player_set_initial_transpose(&f.player,24,99)<0 && errno==ENOTSUP);
        CHECK(kosdec_adx_compat_player_set_initial_transpose(&f.player,-24,-99)<0 && errno==ENOTSUP);
        CHECK(kosdec_adx_player_set_rate(&f.player,88200)<0 && errno==EINVAL);
        if(cases[k].native_rate)
            CHECK(!kosdec_adx_player_set_rate(&f.player,cases[k].native_rate));
        CHECK(!kosdec_adx_player_start(&f.player));
        if(cases[k].cancel) {
            CHECK(!kosdec_adx_player_cancel_source(&f.player) && !mock_allocs);
            kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
            CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
        }
        for(unsigned source=0;source<(cases[k].rebind ? 2u : 1u);++source) {
            for(unsigned step=0;step<32;++step) {
                struct kosdec_adx_compat_result result;
                now+=13332;
                if(kosdec_adx_compat_exec_server(&service,&result)<0) {
                    CHECK(!cases[k].output_rate && errno==ENOTSUP);
                    break;
                }
                if(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END)
                    break;
            }
            if(!cases[k].output_rate) {
                CHECK(info(&f).state==KOSDEC_ADX_PLAYER_FAILED && !mock_allocs && !mock_starts);
                break;
            }
            struct kosdec_adx_player_info state=info(&f);
            CHECK(state.state==KOSDEC_ADX_PLAYER_SOURCE_END && state.source_frames==65 &&
                  state.sample_rate==rate && state.output_rate==cases[k].output_rate);
            CHECK(mock_output.sample_rate==cases[k].output_rate && mock_starts==1 &&
                  mock_frames==65*(source+1) && !mock_updates);
            CHECK(kosdec_adx_compat_player_set_initial_transpose(&f.player,-12,0)<0 &&
                  errno==ENOTSUP);
            CHECK(!kosdec_adx_compat_player_get_transpose(&f.player,&semitones,&cents) &&
                  semitones==-24 && cents==-99);
            if(cases[k].rebind && source==0) {
                CHECK(!kosdec_adx_player_release_source(&f.player));
                kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
                CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
            }
        }
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f.data);
    }
}

static void compat_short_and_rebind(void) {
    struct fixture f;
    init(&f,2,false);
    uint64_t now = 0;
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock,.clock_context = &now};
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_player(&service,0,&f.player));
    CHECK(!kosdec_adx_player_start(&f.player));
    struct kosdec_adx_compat_result r;
    for(unsigned source=0;source<2;++source) {
        bool header = false;
        for(unsigned i=0;i<1000;++i) {
            struct kosdec_adx_decode_progress progress;
            if(!kosdec_adx_player_get_decode_progress(&f.player,&progress) &&
               progress.sample_rate) {
                CHECK(progress.decoded_frames == 0);
                header = true;
                break;
            }
            mock_pause();
        }
        CHECK(header);
        CHECK(!kosdec_adx_compat_exec_server(&service,&r) &&
              info(&f).state == KOSDEC_ADX_PLAYER_PREFILL && !info(&f).source_frames);
        bool decoded = false;
        for(unsigned i=0;i<1000;++i) {
            struct kosdec_adx_decode_progress progress;
            if(!kosdec_adx_player_get_decode_progress(&f.player,&progress) &&
               progress.status == KOSDEC_ADX_DONE && progress.decoded_frames == 65) {
                decoded = true;
                break;
            }
            mock_pause();
        }
        CHECK(decoded);
        CHECK(!kosdec_adx_compat_exec_server(&service,&r) &&
              info(&f).state == KOSDEC_ADX_PLAYER_PLAYING && info(&f).source_frames == 65);
        CHECK(!kosdec_adx_compat_exec_server(&service,&r) &&
              info(&f).state == KOSDEC_ADX_PLAYER_SOURCE_END && info(&f).source_frames == 65);
        if(source==0) {
            CHECK(!kosdec_adx_player_release_source(&f.player));
            kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,false);
            CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
            CHECK(!f.player.compat_prefill_frames);
        }
    }
    CHECK(mock_starts==1 && mock_frames==130);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

struct phased_file {
    struct fixture *fixture;
    uint32_t next, rewind_offset;
    unsigned calls, pass_calls, rewinds;
    bool in_file, check_rewind, fail, early_end;
};

static int allow_file(void *context) {
    ((struct phased_file *)context)->in_file=true;
    return 0;
}

static int finish_file(void *context) {
    struct phased_file *file=context;
    CHECK(file->pass_calls<=4);
    file->pass_calls=0;
    file->in_file=false;
    return 0;
}

static struct kosdec_adx_input_result read_phased(void *context, uint32_t off,
                                                  uint8_t *out, size_t n) {
    struct phased_file *file=context;
    CHECK(file->in_file && n<=512);
    ++file->pass_calls;
    ++file->calls;
    if(file->check_rewind && off<file->next) {
        ++file->rewinds;
        file->rewind_offset=off;
    }
    if(file->fail || (file->check_rewind && !file->rewinds)) {
        uint32_t fault_offset=file->fail ? 86 : 122;
        if(off>=fault_offset)
            return (struct kosdec_adx_input_result){
                file->early_end ? KOSDEC_ADX_INPUT_END : KOSDEC_ADX_INPUT_ERROR,0
            };
        if(n>fault_offset-off)
            n=fault_offset-off;
    }
    struct kosdec_adx_input_result result=input(file->fixture,off,out,n);
    if(result.status==KOSDEC_ADX_INPUT_DATA)
        file->next=off+(uint32_t)result.bytes;
    return result;
}

enum phased_case { PHASED_SEEK, PHASED_LOOP, PHASED_SEQUENCE, PHASED_ERROR,
                   PHASED_ERROR_SEQUENCE, PHASED_EARLY_END };
static void finite_phased(enum phased_case mode) {
    struct fixture f;
    init(&f,2,false);
    f.read_limit=512;
    bool transport_error=mode==PHASED_ERROR || mode==PHASED_ERROR_SEQUENCE;
    struct phased_file file={
        .fixture=&f,.check_rewind=mode==PHASED_LOOP,
        .fail=transport_error || mode==PHASED_EARLY_END,.early_end=mode==PHASED_EARLY_END
    };
    int16_t expected[2*195];
    size_t expected_frames=0;
    if(mode==PHASED_LOOP) {
        free(f.data);
        f.data=stream(50,18,2,32,63,&f.bytes);
        struct kosdec_adx_stream reference;
        kosdec_adx_stream_init(&reference,input,&f,(uint32_t)f.bytes,true);
        for(unsigned step=0;step<100;++step) {
            struct kosdec_adx_stream_result result=kosdec_adx_stream_read(
                &reference,expected+2*expected_frames,195-expected_frames);
            expected_frames+=result.frames;
            if(reference.wraps)
                kosdec_adx_stream_release_loop(&reference);
            CHECK(result.status<=KOSDEC_ADX_DONE);
            if(result.status==KOSDEC_ADX_DONE)
                break;
        }
        CHECK(expected_frames==96 && reference.wraps==1);
    }
    bool sequence=mode==PHASED_SEQUENCE || mode==PHASED_ERROR_SEQUENCE;
    if(sequence) {
        struct kosdec_adx_source source={read_phased,&file,(uint32_t)f.bytes,1};
        kosdec_adx_sequence_init(&f.sequence,false);
        CHECK(kosdec_adx_sequence_enqueue(&f.sequence,&source)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_sequence_enqueue(&f.sequence,&source)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_sequence_start(&f.sequence)==KOSDEC_ADX_MORE);
    } else {
        kosdec_adx_stream_init(&f.stream,read_phased,&file,(uint32_t)f.bytes,mode==PHASED_LOOP);
        if(mode==PHASED_SEEK)
            CHECK(kosdec_adx_stream_seek_begin(&f.stream,33)==KOSDEC_ADX_MORE);
    }
    CHECK(!kosdec_adx_player_init(&f.player,sequence?NULL:&f.stream,sequence?&f.sequence:NULL,&f.config));

    uint64_t time=0;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_config config={
        .clock_us=compat_clock,.clock_context=&time,
        .phases={
            [KOSDEC_ADX_COMPAT_RENDERER]={allow_file,&file},
            [KOSDEC_ADX_COMPAT_FILES]={finish_file,&file},
        },
    };
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f.player));
    CHECK(!kosdec_adx_player_start(&f.player) && !f.player.executor);
    struct kosdec_adx_compat_result r;
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && !r.serviced_phases && !file.calls);
    time=mode==PHASED_ERROR ? 13331 : 13332;
    CHECK(!kosdec_adx_compat_exec_server(&service,&r));
    unsigned initial_reads=file.fail || file.check_rewind ? 2u : 1u;
    CHECK(!f.stream.source_frame && !mock_frames && file.calls==initial_reads);
    uint32_t faults;
    CHECK(!kosdec_adx_compat_get_faults(&service,0,&faults));
    CHECK(faults==(mode==PHASED_ERROR_SEQUENCE ? KOSDEC_ADX_COMPAT_FAULT_TRANSPORT : 0u));
    if(mode==PHASED_ERROR) {
        CHECK(!r.watchdog_checks);
        ++time;
        CHECK(!kosdec_adx_compat_exec_title(&service,0,&r) && r.watchdog_checks==1);
        CHECK(!r.serviced_phases && file.calls==2);
        CHECK(!kosdec_adx_compat_get_faults(&service,0,&faults) &&
              faults==KOSDEC_ADX_COMPAT_FAULT_TRANSPORT);
    }
    if(file.fail) {
        enum kosdec_adx_compat_error code;
        struct kosdec_adx_decode_progress progress;
        CHECK(!kosdec_adx_compat_get_error(&service,0,&code) && code==KOSDEC_ADX_COMPAT_NO_ERROR);
        CHECK(!kosdec_adx_player_get_decode_progress(&f.player,&progress));
        CHECK(progress.status==KOSDEC_ADX_MORE && progress.decoded_frames==0 &&
              progress.input_bytes==86 && !info(&f).first_error);
    }

    bool released=false, failed=false;
    for(unsigned i=0;i<30 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++i) {
        time+=13332;
        int status=kosdec_adx_compat_exec_server(&service,&r);
        if(status<0) {
            CHECK(file.fail && errno==EIO && file.calls==2);
            CHECK(r.failed_phases==(1u<<KOSDEC_ADX_COMPAT_DECODER));
            failed=true;
            break;
        }
        if(mode==PHASED_LOOP || mode==PHASED_EARLY_END)
            CHECK(!kosdec_adx_compat_get_faults(&service,0,&faults) && !faults);
        CHECK(!(r.serviced_phases & ((1u<<KOSDEC_ADX_COMPAT_STREAMS)|(1u<<KOSDEC_ADX_COMPAT_RELOAD))));
        if(!released && info(&f).decoded_loops) {
            CHECK(mode==PHASED_LOOP && file.rewinds==1 && file.rewind_offset==86);
            const struct kosdec_adx_command release={.kind=KOSDEC_ADX_RELEASE_LOOP};
            CHECK(kosdec_adx_player_post(&f.player,&release)==KOSDEC_ADX_MORE);
            released=true;
        }
    }
    if(file.fail) {
        struct kosdec_adx_decode_progress progress;
        enum kosdec_adx_status terminal=transport_error ? KOSDEC_ADX_IO_ERROR : KOSDEC_ADX_TRUNCATED;
        CHECK(failed && info(&f).decoder==terminal);
        CHECK(!kosdec_adx_player_get_decode_progress(&f.player,&progress));
        CHECK(progress.decoded_frames==32 && progress.status==terminal);
    } else {
        CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END && !failed && mock_starts==1);
        if(mode==PHASED_LOOP) {
            enum kosdec_adx_status receipt;
            CHECK(released && kosdec_adx_player_take(&f.player,&receipt) && receipt==KOSDEC_ADX_MORE);
            CHECK(mock_frames==expected_frames && !memcmp(mock_pcm,expected,4*expected_frames));
        } else {
            unsigned skip=mode==PHASED_SEEK?33u:0;
            unsigned frames=sequence?130u:65u-skip;
            CHECK(mock_frames==frames);
            for(unsigned i=0;i<frames;++i)
                for(unsigned ch=0;ch<2;++ch)
                    CHECK(mock_pcm[2*i+ch]==golden[ch][(skip+i)%65]);
        }
    }
    CHECK(!kosdec_adx_player_stop(&f.player,1000) && !mock_joins);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f.data);
}

static void failures(void) {
    for(enum mock_failure failure=MOCK_CREATE;failure<=MOCK_CALLBACK;++failure) {
        struct fixture f;init(&f,2,false);
        if(failure<=MOCK_EXEC_START) {
            mock_failure=failure;CHECK(kosdec_adx_player_start(&f.player)<0);
        }
        else {
            CHECK(!kosdec_adx_player_start(&f.player));
            if(failure<=MOCK_SOUND_START || failure==MOCK_CALLBACK)mock_failure=failure;
            int rc=until_sound(&f);
            if(failure<=MOCK_SOUND_START || failure==MOCK_CALLBACK)CHECK(rc<0);
            else {
                CHECK(!rc);mock_failure=failure;
                if(failure==MOCK_POLL)CHECK(kosdec_adx_player_poll(&f.player)<0);
                if(failure==MOCK_UPDATE)CHECK(kosdec_adx_player_set_output(&f.player,0,0,128,128)<0);
            }
        }
        struct kosdec_adx_player_info i=info(&f);
        if(failure==MOCK_STOP || failure==MOCK_JOIN) {
            CHECK(kosdec_adx_player_stop(&f.player,1000)<0);
            CHECK(info(&f).state==KOSDEC_ADX_PLAYER_STOPPING && info(&f).stop_error==EIO);
            CHECK(mock_joins==0 && kosdec_adx_player_poll(&f.player)<0);
            if(failure==MOCK_STOP)CHECK(mock_callback && mock_userdata);
            else CHECK(!mock_callback && !mock_userdata);
        }
        else CHECK(i.state==KOSDEC_ADX_PLAYER_FAILED && i.first_error);
        int original=info(&f).first_error;mock_failure=MOCK_OK;
        CHECK(!kosdec_adx_player_stop(&f.player,1000));i=info(&f);
        CHECK(i.state==KOSDEC_ADX_PLAYER_STOPPED && i.first_error==original && !i.stop_error);
        CHECK(!mock_callback && !mock_userdata);free(f.data);
    }
}
static void controls(void) {
    struct fixture f;init(&f,2,true);f.config.sound_bytes=64;f.config.capacity=32;
    CHECK(!kosdec_adx_player_init(&f.player,NULL,&f.sequence,&f.config));
    CHECK(!kosdec_adx_player_set_output(&f.player,200,199,64,192));
    CHECK(!kosdec_adx_player_start(&f.player));
    struct kosdec_adx_command pause={.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=true};
    CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_NEED_OUTPUT);
    enum kosdec_adx_status receipt;unsigned n=0;
    while(!kosdec_adx_player_take(&f.player,&receipt)){CHECK(n++<2000);mock_pause();}
    CHECK(receipt==KOSDEC_ADX_MORE && info(&f).production_paused);
    pause.enabled=false;CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_MORE);
    n=0;while(!kosdec_adx_player_take(&f.player,&receipt)){CHECK(n++<2000);mock_pause();}
    CHECK(receipt==KOSDEC_ADX_MORE);CHECK(!until_sound(&f));
    CHECK(mock_output.channel[0].volume==200 && mock_output.channel[1].pan==192);
    pause.enabled=true;CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_MORE);
    n=0;while(!kosdec_adx_player_take(&f.player,&receipt)){CHECK(n++<2000);mock_pause();}
    CHECK(receipt==KOSDEC_ADX_MORE);
    for(n=0;n<10;++n)CHECK(!kosdec_adx_player_poll(&f.player));
    CHECK(info(&f).starvations>0 && info(&f).state==KOSDEC_ADX_PLAYER_PLAYING);
    pause.enabled=false;CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_MORE);
    n=0;while(!kosdec_adx_player_take(&f.player,&receipt)){CHECK(n++<2000);mock_pause();}
    CHECK(receipt==KOSDEC_ADX_MORE);
    for(n=0;n<2000 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++n) {
        CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();
    }
    CHECK(info(&f).copied_frames==195);
    for(size_t j=0;j<mock_frames;++j)for(unsigned c=0;c<2;++c)CHECK(mock_pcm[j*2+c]==golden[c][j%65]);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    init(&f,1,false);f.fail=true;CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(until_sound(&f)<0 && info(&f).decoder==KOSDEC_ADX_IO_ERROR && !mock_allocs);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    init(&f,1,false);f.wait=true;f.config.prefill_timeout_ms=3;
    CHECK(!kosdec_adx_player_init(&f.player,&f.stream,NULL,&f.config));
    CHECK(!kosdec_adx_player_start(&f.player));CHECK(until_sound(&f)<0 && errno==ETIMEDOUT);
    CHECK(!kosdec_adx_player_stop(&f.player,1000) && !mock_allocs);free(f.data);
    init(&f,1,false);CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!mock_allocs && !mock_joins);free(f.data);
}
static void admission(void) {
    struct fixture f;init(&f,2,false);
    struct kosdec_adx_player saved=f.player;
    for(unsigned n=0;n<9;++n) {
        struct kosdec_adx_player_config c=f.config;
        switch(n) {
        case 0:c.sound_bytes=63;break;
        case 1:c.sound_bytes=SND_STREAM_BUFFER_MAX_PCM16+64;break;
        case 2:c.capacity=32;break;
        case 3:c.scratch_samples=256;break;
        case 4:c.scratch=(int16_t *)((char *)c.scratch+1);break;
        case 5:c.stack_bytes=1024;break;
        case 6:c.stack=(char *)c.stack+1;break;
        case 7:c.transfer_timeout_ms=0;break;
        case 8:c.prefill_timeout_ms=0;break;
        }
        CHECK(kosdec_adx_player_init(&f.player,&f.stream,NULL,&c)<0 && errno==EINVAL);
        CHECK(!memcmp(&saved,&f.player,sizeof(saved)) && !mock_allocs && !mock_joins);
    }
    f.wait=true;CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!mock_allocs && mock_joins==1 && info(&f).decoder==KOSDEC_ADX_CANCELLED);
    free(f.data);
}
static void late_errors(void) {
    for(unsigned n=0;n<3;++n) {
        struct fixture f;init(&f,2,false);
        CHECK(!kosdec_adx_player_start(&f.player) && !until_sound(&f));
        mock_async_error=n==0?EIO:ENODATA;mock_status_failure=n==2;
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(info(&f).first_error==(n==0?EIO:n==1?0:EPIPE));
        CHECK(!info(&f).stop_error && !mock_async_error && !mock_callback && !mock_userdata);
        free(f.data);
    }
}
static void reinitialize_source(struct fixture *f,unsigned channels,unsigned rate,bool sequence) {
    /* Legal only after release joins all source users. Poison/free old backing. */
    memset(f->data,0xa5,f->bytes);free(f->data);
    f->data=stream(50,18,channels,32,65,&f->bytes);memset(f->data+20,0,24);
    put32(f->data+8,rate);
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
    kosdec_adx_sequence_init(&f->sequence,false);
    if(sequence) {
        struct kosdec_adx_source src={input,f,(uint32_t)f->bytes,33};
        for(unsigned n=0;n<3;++n)CHECK(kosdec_adx_sequence_enqueue(&f->sequence,&src)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_sequence_start(&f->sequence)==KOSDEC_ADX_MORE);
    }
}
static void filter_settings(void) {
    struct fixture f;
    init(&f, 2, false);
    struct kosdec_adx_filter_settings settings = {19, 31};
    CHECK(kosdec_adx_player_get_filter_settings(NULL, &settings) < 0 && errno == EINVAL);
    CHECK(settings.cutoff == 19 && settings.resonance == 31);
    CHECK(kosdec_adx_player_get_filter_settings(&f.player, NULL) < 0 && errno == EINVAL);
    CHECK(!kosdec_adx_player_get_filter_settings(&f.player, &settings));
    CHECK(settings.cutoff == 0 && settings.resonance == 0);
    CHECK(!kosdec_adx_player_set_filter_settings(&f.player, INT32_MIN, INT32_MAX));
    CHECK(!mock_allocs && !mock_updates);
    CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(!until_sound(&f));
    CHECK(!kosdec_adx_player_release_source(&f.player));
    reinitialize_source(&f, 2, 44100, false);
    CHECK(!kosdec_adx_player_bind_source(&f.player, &f.stream, NULL));
    CHECK(!until_sound(&f));
    CHECK(!kosdec_adx_player_get_filter_settings(&f.player, &settings));
    CHECK(settings.cutoff == INT32_MIN && settings.resonance == INT32_MAX);
    CHECK(!kosdec_adx_player_set_filter_settings(&f.player, 1234, -2));
    CHECK(!mock_updates && mock_starts == 1 && mock_frames == 130);
    CHECK(!kosdec_adx_player_stop(&f.player, 1000));
    CHECK(!kosdec_adx_player_get_filter_settings(&f.player, &settings));
    CHECK(settings.cutoff == 1234 && settings.resonance == -2);
    free(f.data);
}
static void reusable_sources(void) {
    for(unsigned channels=1;channels<=2;++channels) {
        struct fixture f;init(&f,channels,false);
        CHECK(kosdec_adx_player_release_source(&f.player)<0);
        CHECK(kosdec_adx_player_bind_source(&f.player,&f.stream,NULL)<0);
        CHECK(!kosdec_adx_player_enable_decode_progress(&f.player));
        CHECK(!kosdec_adx_player_start(&f.player));
        uint64_t expected=0;
        for(unsigned track=0;track<24;++track) {
            CHECK(!until_sound(&f));
            for(unsigned n=0;n<2000 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++n) {
                CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();
            }
            struct kosdec_adx_player_info i=info(&f);
            unsigned frames=track%2?195:65;expected+=frames;
            CHECK(i.state==KOSDEC_ADX_PLAYER_SOURCE_END && i.sources_started==track+1);
            CHECK(i.source_frames==frames && i.copied_frames==expected && !i.starvations);
            CHECK(mock_allocs==1 && mock_starts==1 && !mock_stops && mock_frames==expected);
            for(size_t j=0;j<mock_frames;++j)for(unsigned c=0;c<channels;++c)
                CHECK(mock_pcm[j*channels+c]==golden[c][j%65]);
            struct kosdec_adx_decode_progress progress;
            CHECK(!kosdec_adx_player_get_decode_progress(&f.player,&progress) &&
                  progress.decoded_frames==frames && progress.source_frame==65 &&
                  progress.sample_rate==44100 && progress.samples_complete);
            CHECK(!kosdec_adx_player_release_source(&f.player));
            CHECK(kosdec_adx_player_get_decode_progress(&f.player,&progress)<0 &&
                  errno==ENODATA && progress.decoded_frames==frames);
            CHECK(mock_joins==track+1 && info(&f).state==KOSDEC_ADX_PLAYER_IDLE);
            unsigned calls=f.calls;
            for(unsigned n=0;n<4;++n)CHECK(!kosdec_adx_player_poll(&f.player));
            CHECK(f.calls==calls && mock_frames==expected && !info(&f).starvations);
            enum kosdec_adx_status receipt;
            struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
            CHECK(kosdec_adx_player_post(&f.player,&command)==KOSDEC_ADX_BAD_ARGUMENT);
            CHECK(!kosdec_adx_player_take(&f.player,&receipt));
            CHECK(!kosdec_adx_player_set_output(&f.player,201,202,20,230));
            CHECK(mock_output.channel[0].volume==201);
            struct kosdec_adx_player saved=f.player;
            CHECK(kosdec_adx_player_bind_source(&f.player,NULL,NULL)<0);
            CHECK(!memcmp(&saved,&f.player,sizeof(saved)));
            if(track==23)break;
            bool sequence=(track+1)%2;
            reinitialize_source(&f,channels,44100,sequence);
            CHECK(!kosdec_adx_player_bind_source(&f.player,sequence?NULL:&f.stream,sequence?&f.sequence:NULL));
            CHECK(info(&f).source_frames==0 && info(&f).state==KOSDEC_ADX_PLAYER_PREFILL);
        }
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(mock_stops==1 && mock_joins==24 && !info(&f).first_error);
        free(f.data);
    }
}
static void discard_receipt(void) {
    struct fixture f;init(&f,2,false);f.wait=true;
    CHECK(!kosdec_adx_player_start(&f.player));
    struct kosdec_adx_command resume={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    CHECK(kosdec_adx_player_post(&f.player,&resume)==KOSDEC_ADX_MORE);
    __atomic_store_n(&f.wait,false,__ATOMIC_RELEASE);
    CHECK(!until_sound(&f));
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END);
    /* Leave the accepted command receipt uncollected across release. */
    CHECK(!kosdec_adx_player_release_source(&f.player));
    enum kosdec_adx_status receipt;
    CHECK(!kosdec_adx_player_take(&f.player,&receipt));
    reinitialize_source(&f,2,44100,false);
    CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
    CHECK(!kosdec_adx_player_take(&f.player,&receipt));
    CHECK(!until_sound(&f) && info(&f).copied_frames==130);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
}
static void rebind_failures(void) {
    /* Two format mismatches, three executor failures, input error/timeout,
       idle output failure, and stop while a new source is waiting. */
    for(unsigned n=0;n<12;++n) {
        struct fixture f;init(&f,2,false);
        CHECK(!kosdec_adx_player_start(&f.player) && !until_sound(&f));
        mock_failure=MOCK_JOIN;
        CHECK(kosdec_adx_player_release_source(&f.player)<0 && mock_joins==0);
        CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END && !f.player.pcm_enabled);
        CHECK(!kosdec_adx_player_poll(&f.player) && mock_frames==65);
        mock_failure=MOCK_OK;
        if(n==11) {
            CHECK(!kosdec_adx_player_stop(&f.player,1000));
            CHECK(mock_joins==1 && mock_stops==1);free(f.data);continue;
        }
        CHECK(!kosdec_adx_player_release_source(&f.player));
        CHECK(mock_joins==1);
        if(n==7) {
            mock_failure=MOCK_POLL;CHECK(kosdec_adx_player_poll(&f.player)<0);
        }
        else {
            reinitialize_source(&f,n==0?1:2,n==1?22050:44100,false);
            f.fail=n==5;f.wait=n==6 || n>=8;
            if(n==6)f.player.memory.prefill_timeout_ms=3;
            if(n>=2 && n<=4)mock_failure=(enum mock_failure)(MOCK_CREATE+n-2);
            int rc=kosdec_adx_player_bind_source(&f.player,&f.stream,NULL);
            if(n>=2 && n<=4)CHECK(rc<0);
            else {
                CHECK(!rc);
                if(n==9 || n==10) {
                    mock_failure=n==9?MOCK_POLL:MOCK_CALLBACK;
                    CHECK(kosdec_adx_player_poll(&f.player)<0);
                    CHECK(info(&f).first_error==(n==9?EIO:EOVERFLOW));
                }
                else if(n==8) {
                    uint64_t callbacks=info(&f).callbacks;
                    CHECK(!kosdec_adx_player_poll(&f.player));
                    CHECK(info(&f).callbacks>callbacks && mock_frames==65 && !info(&f).starvations);
                    CHECK(!kosdec_adx_player_set_output(&f.player,123,124,128,128));
                    CHECK(mock_output.channel[0].volume==123);
                }
                else {
                    CHECK(until_sound(&f)<0);
                    CHECK(info(&f).first_error==(n<2?EPROTO:n==6?ETIMEDOUT:EIO));
                }
            }
        }
        CHECK(mock_frames==65 && mock_allocs==1 && mock_starts==1);
        mock_failure=MOCK_OK;CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(!mock_userdata && !mock_callback && mock_stops==1);free(f.data);
    }
}
static void short_sequence_prefill(void) {
    struct fixture f;init(&f,2,false);
    put32(f.data+12,1);memmove(f.data+50+36,f.data+f.bytes-18,18);f.bytes=50+36+18;
    kosdec_adx_sequence_init(&f.sequence,false);
    struct kosdec_adx_source src={input,&f,(uint32_t)f.bytes,0};
    for(unsigned n=0;n<3;++n)CHECK(kosdec_adx_sequence_enqueue(&f.sequence,&src)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_sequence_start(&f.sequence)==KOSDEC_ADX_MORE);
    f.config.capacity=32;f.config.sound_bytes=64;
    CHECK(!kosdec_adx_player_init(&f.player,NULL,&f.sequence,&f.config));
    CHECK(!kosdec_adx_player_start(&f.player) && !until_sound(&f));
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END && info(&f).copied_frames==3);
    for(unsigned j=0;j<3;++j)for(unsigned c=0;c<2;++c)CHECK(mock_pcm[j*2+c]==golden[c][0]);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
}
static void cancel_seek_rate(void) {
    for(unsigned phase=0;phase<6;++phase) {
        struct fixture f;init(&f,2,phase>=2 && phase<=3);
        if(phase>=2 && phase<=3) {
            f.config.sound_bytes=64;f.config.capacity=32;
            kosdec_adx_sequence_repeat(&f.sequence,true);
            CHECK(!kosdec_adx_player_init(&f.player,NULL,&f.sequence,&f.config));
        }
        if(phase==1)f.wait=true;
        CHECK(!kosdec_adx_player_set_rate(&f.player,22050));
        if(phase)CHECK(!kosdec_adx_player_start(&f.player));
        if(phase>=2)CHECK(!until_sound(&f));
        if(phase==3) {
            struct kosdec_adx_command pause={.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=true};
            CHECK(kosdec_adx_player_post(&f.player,&pause)==KOSDEC_ADX_MORE);
            enum kosdec_adx_status receipt;unsigned n=0;
            while(!kosdec_adx_player_take(&f.player,&receipt)){CHECK(n++<2000);mock_pause();}
            CHECK(receipt==KOSDEC_ADX_MORE && info(&f).production_paused);
        }
        if(phase==5) {
            mock_failure=MOCK_JOIN;CHECK(kosdec_adx_player_cancel_source(&f.player)<0);
            CHECK(info(&f).state==KOSDEC_ADX_PLAYER_RELEASING);
            size_t frames=mock_frames;CHECK(!kosdec_adx_player_poll(&f.player) && mock_frames==frames);
            struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
            CHECK(kosdec_adx_player_post(&f.player,&command)==KOSDEC_ADX_BAD_ARGUMENT);
            mock_failure=MOCK_OK;
        }
        CHECK(!kosdec_adx_player_cancel_source(&f.player));
        struct kosdec_adx_player_info i=info(&f);size_t prefix=mock_frames;
        CHECK(i.state==(phase>=2?KOSDEC_ADX_PLAYER_IDLE:KOSDEC_ADX_PLAYER_READY));
        CHECK(!i.first_error && mock_joins==(phase?1u:0u));
        CHECK(!kosdec_adx_player_cancel_source(&f.player));
        CHECK(kosdec_adx_player_start(&f.player)<0);
        f.wait=false;reinitialize_source(&f,2,44100,false);
        CHECK(kosdec_adx_stream_seek_begin(&f.stream,33)==KOSDEC_ADX_MORE);
        CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
        CHECK(!until_sound(&f));
        for(unsigned n=0;n<2000 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++n) {
            CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();
        }
        i=info(&f);CHECK(i.source_frames==32 && i.copied_frames==prefix+32);
        CHECK(i.sample_rate==44100 && i.output_rate==22050 && mock_output.sample_rate==22050);
        CHECK(mock_allocs==1 && mock_starts==1 && !mock_stops);
        for(unsigned j=0;j<32;++j)for(unsigned c=0;c<2;++c)CHECK(mock_pcm[(prefix+j)*2+c]==golden[c][33+j]);
        CHECK(!kosdec_adx_player_set_rate(&f.player,0));
        CHECK(info(&f).output_rate==44100 && mock_output.sample_rate==44100);
        struct kosdec_adx_player saved=f.player;
        CHECK(kosdec_adx_player_set_rate(&f.player,7999)<0);
        CHECK(kosdec_adx_player_set_rate(&f.player,48001)<0 && !memcmp(&saved,&f.player,sizeof(saved)));
        CHECK(!kosdec_adx_player_stop(&f.player,1000) && !info(&f).first_error);
        free(f.data);
    }
    struct fixture f;init(&f,2,false);f.wait=true;CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(!kosdec_adx_player_cancel_source(&f.player));f.wait=false;
    reinitialize_source(&f,2,44100,false);CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
    CHECK(!until_sound(&f));mock_failure=MOCK_UPDATE;
    CHECK(kosdec_adx_player_set_rate(&f.player,22050)<0 && info(&f).first_error==EIO);
    mock_failure=MOCK_OK;CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    init(&f,2,false);f.fail=true;CHECK(!kosdec_adx_player_start(&f.player));
    for(unsigned n=0;info(&f).decoder<KOSDEC_ADX_DONE;++n){CHECK(n<2000);mock_pause();}
    CHECK(kosdec_adx_player_cancel_source(&f.player)<0 && info(&f).first_error==EIO);
    CHECK(info(&f).decoder==KOSDEC_ADX_IO_ERROR && !f.player.source_bound);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    init(&f,2,false);f.wait=true;CHECK(!kosdec_adx_player_start(&f.player));
    mock_failure=MOCK_JOIN;CHECK(kosdec_adx_player_cancel_source(&f.player)<0);
    mock_failure=MOCK_OK;CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(!info(&f).first_error && mock_joins==1);free(f.data);
}
static void expanded_start_rates(void) {
    const unsigned rates[]={8000,11025,16000,22050,24000,32000,44100,48000};
    for(unsigned n=0;n<sizeof(rates)/sizeof(rates[0]);++n)for(unsigned ch=1;ch<=2;++ch) {
        struct fixture f;init(&f,ch,false);put32(f.data+8,rates[n]);
        CHECK(!kosdec_adx_player_start(&f.player) && !until_sound(&f));
        struct kosdec_adx_player_info i=info(&f);
        CHECK(i.state==KOSDEC_ADX_PLAYER_SOURCE_END && i.source_frames==65);
        CHECK(i.sample_rate==rates[n] && i.output_rate==rates[n] && mock_output.sample_rate==rates[n]);
        CHECK(i.channels==ch && mock_frames==65 && !i.starvations && !mock_updates);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    }
}
static void low_rate_admission(void) {
    const unsigned rates[]={1,2,7999};
    for(unsigned n=0;n<3;++n)for(unsigned override=0;override<2;++override) {
        struct fixture f;init(&f,2,false);put32(f.data+8,rates[n]);
        f.data[16]=f.data[17]=0;
        if(override)CHECK(!kosdec_adx_player_set_rate(&f.player,8000));
        CHECK(!kosdec_adx_player_start(&f.player));
        if(override) {
            CHECK(!until_sound(&f) && mock_allocs==1 && mock_output.sample_rate==8000);
            struct kosdec_adx_player saved=f.player;
            CHECK(kosdec_adx_player_set_rate(&f.player,0)<0 && errno==EINVAL);
            CHECK(!memcmp(&saved,&f.player,sizeof(saved)) && !mock_updates);
        }
        else {
            CHECK(until_sound(&f)<0 && info(&f).first_error==ENOTSUP);
            CHECK(!mock_allocs && !mock_starts);
        }
        CHECK(!kosdec_adx_player_stop(&f.player,1000));free(f.data);
    }
}
static void loop_snapshot(void) {
    struct fixture f;init(&f,2,false);
    CHECK(info(&f).decoded_loops==0);
    CHECK(!kosdec_adx_player_cancel_source(&f.player));
    free(f.data);f.data=stream(50,18,2,32,63,&f.bytes);
    kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,true);
    CHECK(!kosdec_adx_player_bind_source(&f.player,&f.stream,NULL));
    for(unsigned i=0;i<2000 && info(&f).decoded_loops<2;++i) {
        CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();
    }
    CHECK(info(&f).decoded_loops>=2);
    struct kosdec_adx_command release={.kind=KOSDEC_ADX_RELEASE_LOOP};
    CHECK(kosdec_adx_player_post(&f.player,&release)==KOSDEC_ADX_MORE);
    enum kosdec_adx_status receipt;unsigned tries=0;
    while(!kosdec_adx_player_take(&f.player,&receipt)) {CHECK(tries++<2000);mock_pause();}
    CHECK(receipt==KOSDEC_ADX_MORE);
    for(unsigned i=0;i<2000 && info(&f).state!=KOSDEC_ADX_PLAYER_SOURCE_END;++i) {
        CHECK(!kosdec_adx_player_poll(&f.player));mock_pause();
    }
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_SOURCE_END);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(info(&f).decoded_loops==(uint32_t)f.stream.wraps && info(&f).decoded_loops>=2);
    free(f.data);
}
static void running_input_stall(bool enabled) {
    struct fixture f;
    init(&f,1,false);
    free(f.data);
    f.data=stream(50,18,1,32,65,&f.bytes);
    kosdec_adx_stream_init(&f.stream,input,&f,(uint32_t)f.bytes,true);
    f.config.input_stall_timeout_ms=enabled?20:0;
    CHECK(!kosdec_adx_player_init(&f.player,&f.stream,NULL,&f.config));
    CHECK(!kosdec_adx_player_start(&f.player) && !until_sound(&f));
    CHECK(info(&f).state==KOSDEC_ADX_PLAYER_PLAYING);

    /* A paused producer with short refills must never look like blocked input. */
    struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=true};
    enum kosdec_adx_status receipt=KOSDEC_ADX_BAD_ARGUMENT;
    CHECK(kosdec_adx_player_post(&f.player,&command)==KOSDEC_ADX_MORE);
    for(unsigned n=0;n<1000 && !kosdec_adx_player_take(&f.player,&receipt);++n)
        mock_pause();
    CHECK(receipt==KOSDEC_ADX_MORE);
    uint64_t short_before=info(&f).starvations;
    for(unsigned n=0;n<32;++n) { CHECK(!kosdec_adx_player_poll(&f.player));mock_pause(); }
    CHECK(info(&f).starvations>short_before && !info(&f).input_stalled);

    __atomic_store_n(&f.wait,true,__ATOMIC_RELEASE);
    command.enabled=false;
    CHECK(kosdec_adx_player_post(&f.player,&command)==KOSDEC_ADX_MORE);
    receipt=KOSDEC_ADX_BAD_ARGUMENT;
    for(unsigned n=0;n<1000 && !kosdec_adx_player_take(&f.player,&receipt);++n)
        mock_pause();
    CHECK(receipt==KOSDEC_ADX_MORE);
    if(enabled) {
        for(unsigned n=0;n<1000 && !(__atomic_load_n(&f.player.input_wait_generation,__ATOMIC_ACQUIRE)&1);++n)
            mock_pause();
        CHECK(__atomic_load_n(&f.player.input_wait_generation,__ATOMIC_ACQUIRE)&1);
        /* No owner refill demand yet. The first short refill always arms the timer. */
        for(unsigned n=0;n<25;++n) mock_pause();
        CHECK(!kosdec_adx_player_poll(&f.player) && !info(&f).input_stalled);
    }
    bool failed=false;
    for(unsigned n=0;n<(enabled?1000u:32u);++n) {
        if(kosdec_adx_player_poll(&f.player)<0) { CHECK(errno==ETIMEDOUT);failed=true;break; }
        mock_pause();
    }
    CHECK(failed==enabled && info(&f).input_stalled==enabled);
    if(enabled) {
        struct kosdec_adx_player_error error;
        CHECK(info(&f).state==KOSDEC_ADX_PLAYER_FAILED && info(&f).decoder==KOSDEC_ADX_MORE);
        CHECK(!kosdec_adx_player_get_error(&f.player,&error) && error.code==ETIMEDOUT);
    }
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(info(&f).retained_sound==7);
    free(f.data);
}

int main(void) {
    staged_input();
    compat_start_threshold();compat_prepare_room();compat_ready_lifecycle();compat_delayed_footer();compat_inactive_time();
    compat_preoutput_pause(true);compat_preoutput_pause(false);
    puts("ADX_COMPAT_PREOUTPUT_PAUSE_PASS logical PLAYING, time, resume and discard gate");
    compat_attenuation_cache();
    compat_routing_domain();compat_routing_live(1);compat_routing_live(2);
    puts("ADX_COMPAT_ROUTING_PASS all whole-dB levels, mono/stereo, rebind and partial admission");
    compat_initial_pitch();
    puts("ADX_COMPAT_INITIAL_PITCH_PASS staging, request rejection, PCM source rate and output reuse");
    compat_short_and_rebind();
    compat_transport_fault();
    finite_phased(PHASED_SEEK); finite_phased(PHASED_LOOP);
    finite_phased(PHASED_SEQUENCE); finite_phased(PHASED_ERROR);
    finite_phased(PHASED_ERROR_SEQUENCE); finite_phased(PHASED_EARLY_END);
    running_input_stall(false);
    running_input_stall(true);
    loop_snapshot();
    filter_settings();
    low_rate_admission();expanded_start_rates();complete();failures();controls();admission();late_errors();reusable_sources();discard_receipt();rebind_failures();cancel_seek_rate();short_sequence_prefill();
    puts("PLAYER_PASS PCM, controls, 48 successive sources, cancel/seek/rate, rebind failures, retained output and joined producers");
    return 0;
}
