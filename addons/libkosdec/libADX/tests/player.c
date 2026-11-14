#include <kosdec/adx_player.h>
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
    uint8_t *data;size_t bytes;unsigned calls;bool fail,wait;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
static struct kosdec_adx_input_result input(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=ctx;++f->calls;
    if(f->fail)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE))return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    CHECK(off<f->bytes && cap<=f->bytes-off);
    if(cap>17)cap=17;memcpy(dst,f->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void init(struct fixture *f,unsigned channels,bool sequence) {
    memset(f,0,sizeof(*f));mock_reset();f->data=stream(50,18,channels,32,65,&f->bytes);
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
static void reusable_sources(void) {
    for(unsigned channels=1;channels<=2;++channels) {
        struct fixture f;init(&f,channels,false);
        CHECK(kosdec_adx_player_release_source(&f.player)<0);
        CHECK(kosdec_adx_player_bind_source(&f.player,&f.stream,NULL)<0);
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
            CHECK(!kosdec_adx_player_release_source(&f.player));
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
int main(void) {
    loop_snapshot();
    low_rate_admission();expanded_start_rates();complete();failures();controls();admission();late_errors();reusable_sources();discard_receipt();rebind_failures();cancel_seek_rate();short_sequence_prefill();
    puts("PLAYER_PASS PCM, controls, 48 successive sources, cancel/seek/rate, rebind failures, retained output and joined producers");
    return 0;
}
