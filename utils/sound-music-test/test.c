#include "aica_music.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static unsigned checks,starts,updates,stops,releases,lookups,cancels;
static int resolving,retain_error,start_error,ended;
static uint32_t leases[64],g;
static aica_channel_config_t outputs[64];
static aica_music_t m;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static int resolve(void *ctx,const aica_music_event_t *e,unsigned bank,unsigned program,
                   unsigned drum,aica_music_tone_t *out) {
    (void)ctx;++lookups;CHECK(bank<128 && program<128 && drum<=1);
    if(resolving) return AICA_MUSIC_RETRY;
    *out=(aica_music_tone_t){.config={.base=0x31000,.type=0,.length=1000,.freq=22050,.vol=255,.pan=128,.direct_level=15},
        .bank=e->bank,.generation=e->bank_generation,.root_key=60};return 0;
}
static void cancel(void *ctx) { (void)ctx;++cancels; }
static int retain(void *ctx,unsigned voice,const aica_music_tone_t *tone) {
    (void)ctx;CHECK(voice<64 && tone->generation==1);
    if(retain_error) return retain_error;
    leases[voice]|=1u<<tone->bank;return 0;
}
static int start(void *ctx,unsigned voice,uint32_t token,const aica_channel_config_t *c) {
    (void)ctx;CHECK(voice<64 && token && leases[voice]);++starts;outputs[voice]=*c;return start_error;
}
static void update(void *ctx,unsigned voice,const aica_channel_config_t *c) {
    (void)ctx;CHECK(leases[voice]);++updates;outputs[voice]=*c;
}
static void stop(void *ctx,unsigned voice,int release) {
    (void)ctx;CHECK(leases[voice]);++stops;if(release) ++releases;
}
static int is_ended(void *ctx,unsigned voice,uint32_t token) {
    (void)ctx;CHECK(voice<64 && token);return ended;
}
static aica_music_ops_t ops={.resolve=resolve,.cancel_resolve=cancel,.retain=retain,.start=start,
    .update=update,.stop=stop,.ended=is_ended};
static void init(unsigned count) {
    starts=updates=stops=releases=lookups=cancels=0;
    resolving=retain_error=start_error=ended=0;memset(leases,0,sizeof(leases));
    CHECK(aica_music_init(&m,count,&ops)==0);
    CHECK(aica_music_open(&m,0,0,1,&g)==0);
}
static void message(unsigned status,unsigned a,unsigned b,unsigned priority) {
    CHECK(aica_music_message(&m,0,g,status,a,b,priority)==0);
    CHECK(aica_music_service(&m,0)==1);
}
static void pitch_tests(void) {
    int pitch;unsigned rates[]={1,32,8000,22050,44100,96000,192000};
    for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);i++) {
        uint32_t previous=0;
        for(pitch=-24*256;pitch<=24*256;pitch++) {
            double exact=(double)rates[i]*pow(2.0,(double)pitch/(12.0*256.0));
            uint32_t got=aica_music_rate(rates[i],pitch);
            if(exact<1) exact=1;if(exact>192000) exact=192000;
            CHECK(got>=previous && got>=1 && got<=192000);previous=got;
            CHECK(fabs((double)got-exact)<=exact*0.0005+1.0);
        }
        CHECK(aica_music_rate(rates[i],0)==rates[i]);
    }
    CHECK(aica_music_rate(22050,12*256)==44100);
    CHECK(aica_music_rate(22050,INT32_MIN)==1);
    CHECK(aica_music_rate(22050,INT32_MAX)==192000);
    CHECK(aica_music_rate(UINT32_MAX,15*12*256)==192000);
}
static void music_tests(void) {
    uint32_t old;unsigned before;
    init(4);message(0x90,60,127,8);CHECK(starts==1 && m.voices[0].state==AICA_MUSIC_HELD);
    CHECK(outputs[0].freq==22050 && outputs[0].vol==201);
    message(0xb0,64,127,0);message(0x80,60,0,0);
    CHECK(m.voices[0].state==AICA_MUSIC_SUSTAINED && !stops);
    message(0xb0,64,0,0);CHECK(m.voices[0].state==AICA_MUSIC_RELEASE && releases==1 && leases[0]);
    message(0x90,72,127,8);CHECK(outputs[1].freq==44100);
    message(0xe0,0,0,0);CHECK(outputs[1].freq<44100);
    message(0xe0,0,64,0);CHECK(outputs[1].freq==44100);
    message(0xb0,7,0,0);CHECK(!outputs[1].vol);
    message(0xb0,121,0,0);CHECK(outputs[1].vol==201);
    message(0xb0,48,99,0);CHECK(m.ports[0].mailbox==99);
    CHECK(aica_music_parameter(&m,0,g,AICA_MUSIC_VOLUME,-127,100)==0);
    CHECK(aica_music_service(&m,50)==0 && m.ports[0].fades[0].value==-63);
    CHECK(aica_music_service(&m,UINT32_MAX)==0 && m.ports[0].fades[0].value==-127);
    CHECK(aica_music_reset(&m,0,g)==0);aica_music_service(&m,0);CHECK(outputs[1].vol==201);
    CHECK(aica_music_mute(&m,0,g,1)==0);before=starts;
    message(0x90,60,100,8);CHECK(starts==before);
    CHECK(aica_music_mute(&m,0,g,0)==0);message(0x90,60,100,8);CHECK(starts==before+1);
    before=starts;message(0xa0,60,100,0);message(0xd0,100,0,0);CHECK(starts==before);
    old=g;CHECK(aica_music_close(&m,0,g)==0);CHECK(leases[0]);
    CHECK(aica_music_open(&m,0,1,1,&g)==0 && g!=old);
    CHECK(aica_music_message(&m,0,old,0x90,60,127,8)==AICA_MUSIC_INVALID);
    CHECK(aica_music_close(&m,0,old)==AICA_MUSIC_INVALID);
    aica_music_quiesce(&m);CHECK(aica_music_open(&m,1,0,1,&old)==AICA_MUSIC_BUSY);
}
static void failure_tests(void) {
    uint32_t sentinel=123;unsigned before;
    init(2);CHECK(aica_music_open(&m,0,0,1,&sentinel)==AICA_MUSIC_BUSY && sentinel==123);
    for(unsigned i=0;i<AICA_MUSIC_QUEUE;i++) CHECK(aica_music_message(&m,0,g,0x90,60,127,8)==0);
    CHECK(aica_music_message(&m,0,g,0x90,60,127,8)==AICA_MUSIC_BUSY && m.count==64);
    resolving=1;CHECK(aica_music_service(&m,1)==0 && m.count==64 && !starts && lookups==1);
    CHECK(aica_music_close(&m,0,g)==0);CHECK(aica_music_service(&m,1)==4 && m.count==60 && !starts);
    aica_music_quiesce(&m);CHECK(!m.count && cancels);
    init(1);retain_error=AICA_MUSIC_BUSY;message(0x90,60,127,8);CHECK(!starts && !leases[0] && m.rejected==1);
    retain_error=0;message(0x90,60,127,8);before=starts;
    message(0x90,61,127,7);CHECK(starts==before && m.declined==1);
    message(0x90,61,127,8);CHECK(starts==before+1 && m.stolen==1);
    message(0x90,62,127,0);CHECK(starts==before+2);
    ended=1;aica_music_service(&m,1);CHECK(!m.voices[0].state && leases[0]);ended=0;
    m.token=UINT32_MAX;before=starts;message(0x90,60,127,8);CHECK(starts==before && m.last_result==AICA_MUSIC_EXHAUSTED);
    init(1);start_error=-1;
    CHECK(aica_music_message(&m,0,g,0x90,60,127,8)==0);
    CHECK(aica_music_message(&m,0,g,0x90,61,127,8)==0);
    CHECK(aica_music_service(&m,0)==1 && m.count==1);
    CHECK(m.faulted && leases[0] && starts==1);
    before=starts;CHECK(aica_music_message(&m,0,g,0x90,60,127,8)==AICA_MUSIC_FAULT);
    CHECK(aica_music_service(&m,1)==0 && starts==before);
    aica_music_quiesce(&m);CHECK(stops==1 && leases[0]);
    init(1);m.ports[1].generation=UINT32_MAX;
    CHECK(aica_music_open(&m,1,0,1,&sentinel)==AICA_MUSIC_EXHAUSTED && sentinel==123);
    CHECK(aica_music_parameter(&m,0,g,0,128,0)==AICA_MUSIC_INVALID);
    CHECK(aica_music_parameter(&m,0,g,3,0,1)==AICA_MUSIC_INVALID);
    CHECK(aica_music_parameter(&m,0,g,2,0,32768)==AICA_MUSIC_INVALID);
    CHECK(aica_music_message(&m,0,g,0xf0,0,0,0)==AICA_MUSIC_INVALID);
    CHECK(aica_music_message(&m,0,g,0x90,128,0,0)==AICA_MUSIC_INVALID);
    CHECK(aica_music_message(&m,0,g,0x90,60,127,32)==AICA_MUSIC_INVALID);
}
int main(void) {
    pitch_tests();music_tests();failure_tests();
    printf("resident music: %u checks PASS; engine bytes=%zu\n",checks,sizeof(m));return 0;
}
