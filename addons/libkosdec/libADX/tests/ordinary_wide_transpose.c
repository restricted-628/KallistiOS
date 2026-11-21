#include <kos.h>
#include <dc/g2bus.h>
#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "snd_ordinary_pcm16.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#ifndef TEST_CHANNELS
#define TEST_CHANNELS 2
#endif

enum { CHANNELS = TEST_CHANNELS, WIDE = 65504, SMALL = 8192,
       QUEUE = CHANNELS == 2 ? 262144 : 65536,
       WIDE_FRAMES = 393216, LOW_FRAMES = 49152, START = 44100, LOW_START = 8000,
       SAMPLE_BYTES = 18 * CHANNELS,
       TRACK_SIZE = 32 + WIDE_FRAMES / 32 * SAMPLE_BYTES + 18 };

static struct kosdec_adx_player players[2], *player;
static struct kosdec_adx_stream stream;
static struct kosdec_adx_compat_service service;
static struct kosdec_adx_compat_title title;
static _Alignas(32) uint8_t data[TRACK_SIZE], stack[32768];
static _Alignas(32) int16_t pcm[QUEUE*2], scratch[WIDE+1];
static uint32_t track_bytes, nominal, frames;
static unsigned slot;
static const char *profile;

#define CHECK(test) do { \
    if(!(test)) { \
        int error = errno; \
        fprintf(stderr,"ADX-WIDE FAIL %s line=%d errno=%d check=%s\n", \
                profile,__LINE__,error,#test); \
        return -1; \
    } \
} while(0)

static int load(const char *path) {
    file_t file = fs_open(path,O_RDONLY);
    CHECK(file != FILEHND_INVALID);
    for(uint32_t pos = 0; pos < track_bytes; ) {
        ssize_t got = fs_read(file,data+pos,track_bytes-pos);
        CHECK(got > 0);
        pos += (uint32_t)got;
    }
    CHECK(!fs_close(file));
    return 0;
}

static struct kosdec_adx_input_result provide(void *context, uint32_t offset,
    uint8_t *dest, size_t bytes) {
    (void)context;
    if(offset > track_bytes || bytes > track_bytes-offset)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    memcpy(dest,data+offset,bytes);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,bytes};
}

static uint64_t clock_us(void *context) {
    (void)context;
    return timer_us_gettime64();
}

static int service_title(void) {
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_exec_server(&service,&pass));
    CHECK(!pass.failed_titles && !pass.failed_phases);
    return 0;
}

static int status(snd_ordinary_pcm16_status_t *out) {
    CHECK(!snd_ordinary_pcm16_status(player->ordinary_pcm,out) && !out->error);
    CHECK(CHANNELS == 2 || out->frames[1] == 0);
    struct kosdec_adx_time time;
    CHECK(!kosdec_adx_compat_player_get_time(player,&time) && time.frequency == nominal);
    CHECK(time.count >= 0 && (uint64_t)time.count <= out->frames[0] &&
          (uint64_t)time.count == (out->frames[0] < frames ? out->frames[0] : frames));
    struct kosdec_adx_player_info info;
    CHECK(!kosdec_adx_player_get_info(player,&info));
    if(info.first_error || info.starvations)
        fprintf(stderr,"ADX-WIDE %s first_error=%d starvations=%" PRIu64 "\n",
                profile,info.first_error,info.starvations);
    CHECK(info.output_rate == out->rate && info.sample_rate == nominal &&
          !info.first_error && !info.starvations);
    return 0;
}

static int word(unsigned expected) {
    for(unsigned ch = 0; ch < CHANNELS; ++ch) {
        uintptr_t reg = 0xa0700018u+0x80u*(slot+ch);
        CHECK((g2_read_32(reg)&0x7fffu) == expected);
    }
    return 0;
}

static int start(unsigned lane, const char *path, unsigned ring, unsigned pitch) {
    player = &players[lane];
    slot = lane*CHANNELS;
    track_bytes = 32+frames/32*SAMPLE_BYTES+18;
    CHECK(!load(path));
    const struct kosdec_adx_player_config config = {
        .pcm = pcm,.pcm_samples = QUEUE*2,.capacity = QUEUE,
        .scratch = scratch,.scratch_samples = WIDE+1,.stack = stack,
        .stack_bytes = sizeof(stack),.sound_bytes = ring*2,
        .prefill_timeout_ms = 4000,.transfer_timeout_ms = 1000
    };
    const struct kosdec_adx_compat_config pacing = {.clock_us = clock_us};
    kosdec_adx_stream_init(&stream,provide,NULL,track_bytes,false);
    CHECK(!kosdec_adx_player_init(player,&stream,NULL,&config));
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&pacing));
    CHECK(!kosdec_adx_compat_bind_player(&service,0,player));
    if(CHANNELS == 1)
        CHECK(!kosdec_adx_compat_player_enable_mono_clock(player));
    else
        CHECK(!kosdec_adx_compat_player_enable_stereo_transpose(player));
    CHECK(!kosdec_adx_player_start(player));
    const uint64_t deadline = timer_ms_gettime64()+4000;
    while(!player->ordinary_pcm) {
        CHECK(timer_ms_gettime64() < deadline && !service_title());
        if(!player->ordinary_pcm) thd_sleep(1);
    }
    CHECK(!word(pitch));
    printf("ADX-WIDE %s start rate=%" PRIu32 " pitch=%04x frames=%" PRIu32 " ring=%u\n",
           profile,nominal,pitch,frames,ring);
    return 0;
}

static int wait_for(uint64_t cursor, uint64_t committed, bool tail, uint32_t rate) {
    const uint64_t deadline = timer_ms_gettime64()+8500;
    do {
        snd_ordinary_pcm16_status_t seen;
        struct kosdec_adx_player_info info;
        CHECK(timer_ms_gettime64()<deadline && !service_title() && !status(&seen));
        CHECK(seen.rate == rate && !kosdec_adx_player_get_info(player,&info));
        if(seen.frames[0] >= cursor && seen.committed_frames >= committed &&
           (!tail || info.state == KOSDEC_ADX_PLAYER_SOURCE_END)) return 0;
        thd_sleep(1);
    } while(true);
}

static int cached(int semitones, int cents) {
    int32_t have_semitones, have_cents;
    CHECK(!kosdec_adx_compat_player_get_transpose(player,&have_semitones,&have_cents) &&
          have_semitones == semitones && have_cents == cents);
    return 0;
}

static int change(int semitones, int cents, uint32_t rate, unsigned pitch, bool tail) {
    snd_ordinary_pcm16_status_t before, after;
    struct kosdec_adx_player_info info;
    const uint64_t deadline = timer_ms_gettime64()+1000;
    do {
        CHECK(!status(&before) && !kosdec_adx_player_get_info(player,&info));
        CHECK(tail ? info.state == KOSDEC_ADX_PLAYER_SOURCE_END && before.frames[0] < frames :
                     info.state == KOSDEC_ADX_PLAYER_PLAYING);
        if(!kosdec_adx_compat_player_set_live_transpose(player,semitones,cents))
            break;
        CHECK(errno == EAGAIN && timer_ms_gettime64() < deadline);
        CHECK(!status(&after) && after.rate == before.rate && !cached(semitones,cents));
        CHECK(!service_title());
        thd_sleep(1);
    } while(true);

    CHECK(!status(&after) && after.rate == rate && !word(pitch) && !cached(semitones,cents));
    if(tail) {
        enum kosdec_adx_compat_state state;
        CHECK(!kosdec_adx_compat_player_get_state(player,&state) &&
              state == KOSDEC_ADX_COMPAT_DECEND);
    }
    printf("ADX-WIDE %s update before=%" PRIu64 "/%" PRIu64 " after=%" PRIu64 "/%" PRIu64
           " rate=%" PRIu32 " pitch=%04x tail=%u\n",profile,before.frames[0],before.frames[1],
           after.frames[0],after.frames[1],rate,pitch,(unsigned)tail);
    return 0;
}

static int finish(void) {
    snd_ordinary_pcm16_status_t final;
    CHECK(!status(&final));
    const uint32_t owned = snd_mem_available();
    CHECK(!kosdec_adx_player_stop(player,1000) && !kosdec_adx_player_stop(player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0) && snd_mem_available() == owned);
    printf("ADX-WIDE %s stop at=%" PRIu64 "/%" PRIu64 "\n",
           profile,final.frames[0],final.frames[1]);
    thd_sleep(250);
    return 0;
}

static int wide(void) {
    profile = "wide";
    nominal = START;
    frames = WIDE_FRAMES;
    CHECK(!start(0,"/cd/proof.adx",WIDE,0x0000));
    const uint64_t deadline = timer_ms_gettime64()+2000;
    snd_channel_status_ex_t leading;
    do {
        CHECK(timer_ms_gettime64() < deadline && !snd_channel_get_status_ex(slot,&leading));
        if(leading.position >= WIDE-13000) break;
        thd_sleep(1);
    } while(true);
    CHECK(leading.position < WIDE-3500);
    CHECK(kosdec_adx_compat_player_set_live_transpose(player,24,0) < 0 && errno == EAGAIN);
    snd_ordinary_pcm16_status_t deferred;
    CHECK(!status(&deferred) && deferred.rate == START && deferred.committed_frames == WIDE);
    CHECK(!word(0x0000) && !cached(24,0));
    puts("ADX-WIDE ordinary pre-post EAGAIN preserved the running player");
    CHECK(!wait_for(0,WIDE+WIDE/2,false,START));
    CHECK(!word(0x0000));
    CHECK(!change(24,0,176400,0x1000,false));
    CHECK(!wait_for(3*WIDE,3*WIDE+WIDE/2,false,176400));
    CHECK(!change(25,-1,186736,0x103c,false));
    CHECK(!wait_for(5*WIDE,0,true,186736));
    CHECK(!change(0,0,START,0x0000,true));
    CHECK(!wait_for(WIDE_FRAMES+128,0,true,START));
    CHECK(kosdec_adx_compat_player_set_live_transpose(player,12,0) < 0 && errno == ENOTSUP);
    CHECK(!word(0x0000));
    return finish();
}

static int low(void) {
    profile = "low";
    nominal = LOW_START;
    frames = LOW_FRAMES;
    CHECK(!start(1,"/cd/fresh.adx",SMALL,0x69ce));
    CHECK(!wait_for(SMALL-512,SMALL+SMALL/2,false,LOW_START));
    CHECK(!change(-12,0,3997,0x61cd,false));
    CHECK(!wait_for(2*SMALL+512,2*SMALL+SMALL/2,false,3997));
    CHECK(!change(-25,1,1889,0x597b,false));
    CHECK(!wait_for(3*SMALL+1024,3*SMALL+SMALL/2,false,1889));
    CHECK(!change(0,0,7994,0x69cd,false));
    CHECK(!wait_for(LOW_FRAMES+128,0,true,7994));
    return finish();
}

int main(void) {
    profile = "init";
    if(snd_stream_init_ex(2,WIDE*2) || wide() || low())
        puts("ADX-WIDE FAIL");
    else
        puts("ADX-WIDE PASS exact requests, real tails and checked stop");
    fflush(stdout);
    for(;;) thd_sleep(1000);
}
