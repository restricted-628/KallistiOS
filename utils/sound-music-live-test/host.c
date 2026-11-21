/* Actual SH4 owner/client connected to actual ARM dispatcher/core. Host port
   and bank-manager service calls are controlled fakes; their own suites remain
   separate. Every scenario runs in a fresh process, as does the runtime owner. */
#define main observation_regression
#include "../sound-observation-test/arm.c"
#undef main
#include <errno.h>
#include <stdlib.h>
#include "music_live.h"
static aica_music_receipt_t receipt;
static aica_bank_header_t bh;
static aica_bank_record_t br[128];
static uint32_t active_session=7, features=AICA_DRIVER_FEATURE_MUSIC;
static unsigned reserved, attached, closed, submitted, pending_releases;
static unsigned reserve_fail, pin_fail;
static int submit_error, skip_submit, read_error;
static unsigned char image[160];
static const uint8_t *mapping(uint32_t address, uint32_t bytes) {
    CHECK(address==0x30000 && bytes==sizeof(image));return image;
}
uint64_t timer_ms_gettime64(void) { return 10; }
uint32_t snd_iface_observation_features(void) { return features; }
int snd_observation_bind_until(uint64_t end, uint32_t *out) { CHECK(end>10);*out=active_session;return 0; }
int snd_observation_session(uint32_t *out) { *out=active_session;return 0; }
int snd_iface_observation_read(uint32_t address, void *out, uint32_t bytes) {
    if(address==AICA_MEM_OBSERVATION_HEADER) {
        CHECK(bytes==sizeof(header));memcpy(out,&header,bytes);return 0;
    }
    CHECK(address==AICA_MEM_MUSIC_RECEIPT && (bytes==4 || bytes==256));
    if(read_error) return -1;
    memcpy(out,&receipt,bytes);return 0;
}
int snd_iface_observation_submit(void *packet_data, uint32_t words, uint64_t end) {
    CHECK(words==16 && end>10);++submitted;
    if(!skip_submit) process_packet(packet_data,words);
    return submit_error?-1:0;
}
int snd_port_reserve_music(snd_port_t *out) {
    const unsigned count = 1;
    CHECK(count==1);
    if(reserve_fail && reserved==reserve_fail) { errno=ENOSPC;return -1; }
    *out=++reserved;return 0;
}
int snd_port_playback_pin(snd_port_t port, unsigned *channel) {
    CHECK(port && port<=64);
    if(pin_fail && port==pin_fail) { errno=EBUSY;return -1; }
    ++attached;*channel=((port-1)*17u)%64u;return 0;
}
int snd_port_playback_unpin(snd_port_t port) { CHECK(port && attached);--attached;return 0; }
int snd_port_close(snd_port_t port) { CHECK(port && reserved);++closed;--reserved;return 0; }
int abk_bank_pending_acquire(abk_bank_t *b) {
    if(b->state!=ABKB_ACTIVE || b->closing || b->pending==UINT32_MAX) return -1;
    ++b->pending;return 0;
}
int abk_bank_pending_release(abk_bank_t *b) {
    CHECK(b->pending);--b->pending;++pending_releases;return 0;
}
static void setup(void) {
    reset();bind(7,1);banks.header=&bh;banks.records=br;banks.map=mapping;
    aica_bank_registry_init(&banks);bh.session=7;
    aica_music_live_init(music,&banks,&receipt);
    br[0].session=7;br[0].generation=1;br[0].state=AICA_BANK_ACTIVE;
    br[0].image_base=0x30000;br[0].image_bytes=sizeof(image);br[0].sample_count=br[0].zone_count=1;
    /* Original tiny immutable PCM16 zone. Registry admission is tested elsewhere;
       these records represent an already admitted bank for the resolver path. */
    image[8]=1;image[64]=128;image[72]=16;image[76]=0x22;image[77]=0x56;image[88]=2;
    image[96+8]=127;image[96+9]=1;image[96+10]=127;image[96+11]=60;
    image[96+14]=255;image[96+15]=128;image[96+22]=15;
}
static void finish(aica_music_owner_t *o) {
    CHECK(aica_music_owner_poll(o)==1 && o->client.state==AICA_MC_DONE);
    CHECK(o->client.receipt.result==0);CHECK(aica_music_owner_consume(o)==0);
}
int main(int argc,char **argv) {
    CHECK(argc==2);unsigned mode=(unsigned)strtoul(argv[1],0,10);
    setup();aica_music_owner_t o={0};abk_bank_t bank={.state=ABKB_ACTIVE,.slot=0,.session=7,.generation=1};
    if(mode==0) { features=0;CHECK(aica_music_owner_init(&o,64,100)<0 && !reserved && !submitted); }
    else if(mode==1) { reserve_fail=17;CHECK(aica_music_owner_init(&o,64,100)<0 && !reserved && !attached && closed==17 && !submitted); }
    else if(mode==2) { pin_fail=9;CHECK(aica_music_owner_init(&o,64,100)<0 && !reserved && !attached && closed==9 && !submitted); }
    else {
        if(mode==3 || mode==4) { submit_error=1;skip_submit=(mode==4); }
        CHECK(aica_music_owner_init(&o,64,100)==0 && reserved==64 && attached==64 && o.retained);
        if(mode==3 || mode==4) {
            CHECK(o.client.uncertain);submit_error=skip_submit=0;
            CHECK(aica_music_owner_retry(&o)==1 && submitted==2);
        }
        finish(&o);CHECK(music->backend.voices==64);
        if(mode==9 || mode==10) {
            if(mode==9) header.active_session=8;else features=0;
            uint32_t args[4]={0};
            CHECK(aica_music_owner_command(&o,AICA_MUSIC_WIRE_QUERY,0,0,args)<0);
            CHECK(o.client.state==AICA_MC_LOST && reserved==64 && !closed);
        }
        else if(mode==5) {
            active_session=8;uint32_t args[4]={0};
            CHECK(aica_music_owner_command(&o,AICA_MUSIC_WIRE_QUERY,0,0,args)<0);
            CHECK(o.client.state==AICA_MC_LOST && reserved==64 && !closed);
        }
        else {
            bank.closing=1;CHECK(aica_music_owner_open(&o,0,&bank)<0 && !bank.pending);bank.closing=0;
            if(mode==8) {
                bank.generation=2;
                CHECK(aica_music_owner_open(&o,0,&bank)>=0 && bank.pending==1);
                CHECK(aica_music_owner_poll(&o)==1 && o.client.receipt.result==AICA_MUSIC_MISSING);
                CHECK(!bank.pending && !o.pending_bank && !br[0].producer_refs);
                CHECK(aica_music_owner_consume(&o)==0 && reserved==64 && !closed);
                printf("HOST terminal OPEN rejection: %u checks passed\n",checks);return 0;
            }
            submit_error=(mode==6 || mode==7);
            CHECK(aica_music_owner_open(&o,0,&bank)>=0 && bank.pending==1 && br[0].producer_refs==1);
            /* Host close may latch closing here but cannot unregister while the
               pending pin or actual ARM producer/voice references remain. */
            bank.closing=1;
            if(mode==6 || mode==7) {
                if(mode==7) br[0].sequence=UINT32_MAX;
                submit_error=0;CHECK(aica_music_owner_quiesce(&o)>=0);
                CHECK(aica_music_owner_poll(&o)==1);
                if(mode==7) {
                    CHECK(o.client.receipt.result==AICA_MUSIC_FAULT && bank.pending==1);
                    CHECK(aica_music_owner_consume(&o)<0 && o.pending_bank==&bank);
                }
                else CHECK(!bank.pending && !o.pending_bank && !br[0].producer_refs);
            }
            else {
                read_error=1;CHECK(aica_music_owner_poll(&o)==0 && bank.pending==1);read_error=0;
                finish(&o);CHECK(!bank.pending && br[0].producer_refs==1);
                uint32_t args[4]={0x90,60,100,10};
                CHECK(aica_music_owner_command(&o,AICA_MUSIC_WIRE_MESSAGE,0,1,args)>=0);finish(&o);
                aica_music_live_service(music,100);
                CHECK(br[0].voice_refs==1 && music->music.voices[0].state==AICA_MUSIC_HELD);
                CHECK(music->backend.start_token[0]==1 && writes>0);
                CHECK(aica_music_owner_quiesce(&o)>=0);finish(&o);
                CHECK(!br[0].producer_refs && br[0].voice_refs==1 && music->bank.leases[0][0]==1);
                CHECK(o.client.admission_closed && music->backend.claimed && reserved==64 && attached==64 && !closed);
            }
        }
        CHECK(reserved==64 && attached==64 && !closed);
    }
    printf("HOST live owner mode%u: %u checks passed\n",mode,checks);return 0;
}
