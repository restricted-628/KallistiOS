#include <kos.h>
#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

enum { RATE = 44100, RING = 16384, QUEUE = 32768, FRAMES = 81920,
       TRACK_SIZE = 32 + FRAMES / 32 * 18 + 18 };
static struct kosdec_adx_player player;
static struct kosdec_adx_stream stream;
static struct kosdec_adx_compat_service service;
static struct kosdec_adx_compat_title title;
static _Alignas(32) uint8_t track[TRACK_SIZE], stack[32768];
static _Alignas(32) int16_t pcm[QUEUE * 2], scratch[RING+16];

#define CHECK(test) do { \
    if(!(test)) { \
        printf("ADX-MONO FAIL line=%d check=%s errno=%d\n", __LINE__, #test, errno); \
        return -1; \
    } \
} while(0)

static int load(void) {
    file_t file = fs_open("/cd/proof.adx", O_RDONLY);
    CHECK(file != FILEHND_INVALID);
    for(size_t at = 0; at < sizeof(track); ) {
        ssize_t read = fs_read(file, track + at, sizeof(track) - at);
        CHECK(read > 0);
        at += (size_t)read;
    }
    CHECK(!fs_close(file));
    return 0;
}

static struct kosdec_adx_input_result provide(void *context, uint32_t offset,
                                               uint8_t *dest, size_t size) {
    (void)context;
    if(offset > sizeof(track) || size > sizeof(track)-offset)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR, 0};
    memcpy(dest, track+offset, size);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, size};
}

static uint64_t clock_us(void *context) {
    (void)context;
    return timer_us_gettime64();
}

static int service_title(void) {
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_exec_server(&service, &pass));
    CHECK(!pass.failed_titles && !pass.failed_phases);
    return 0;
}

static int prepare(void) {
    static const struct kosdec_adx_player_config config = {
        .pcm = pcm, .pcm_samples = QUEUE * 2, .capacity = QUEUE,
        .scratch = scratch, .scratch_samples = RING+16, .stack = stack,
        .stack_bytes = sizeof(stack), .sound_bytes = RING*2, .transfer_timeout_ms = 1000,
        .prefill_timeout_ms = 3000
    };
    const struct kosdec_adx_compat_config pacing = {.clock_us = clock_us};
    kosdec_adx_stream_init(&stream, provide, NULL, sizeof(track), false);
    CHECK(!kosdec_adx_player_init(&player, &stream, NULL, &config));
    CHECK(!kosdec_adx_compat_service_init(&service, &title, 1, &pacing));
    CHECK(!kosdec_adx_compat_bind_player(&service, 0, &player));
#ifndef TEST_BASELINE
    CHECK(!kosdec_adx_compat_player_enable_mono_clock(&player));
    CHECK(!kosdec_adx_compat_player_enable_playend(&player,1000));
#endif
    CHECK(!kosdec_adx_player_start(&player));
    return 0;
}

static int rates(int32_t request, uint32_t admitted) {
    struct kosdec_adx_player_info info;
    int32_t pitch, cents;
    CHECK(!kosdec_adx_compat_player_get_transpose(&player, &pitch, &cents) &&
          pitch == request && !cents);
    CHECK(!kosdec_adx_player_get_info(&player, &info) && info.output_rate == admitted &&
          info.sample_rate == RATE && info.channels == 1 && !info.first_error);
    return 0;
}

#ifndef TEST_BASELINE
static int observe(struct kosdec_adx_source_output *clock) {
    struct kosdec_adx_time time;
    int64_t real;
    CHECK(!kosdec_adx_player_get_source_output(&player, 1, clock) && clock->valid);
    CHECK(!kosdec_adx_compat_player_get_time(&player, &time) && time.frequency == RATE &&
          time.count >= 0 && (uint64_t)time.count == clock->source_frames);
    CHECK(!kosdec_adx_compat_player_get_time_real(&player, &real) &&
          real == (int64_t)(((float)time.count/RATE)*100.0f));
    return 0;
}

static int reach(uint64_t goal, struct kosdec_adx_source_output *clock) {
    const uint64_t deadline = timer_ms_gettime64() + 3200;
    do {
        CHECK(!service_title() && !observe(clock) && timer_ms_gettime64() < deadline);
        if(clock->source_frames >= goal) return 0;
        thd_sleep(1);
    } while(true);
}

static int change(int32_t pitch, uint32_t rate, struct kosdec_adx_source_output *clock) {
    uint64_t before = clock->source_frames;
    CHECK(!kosdec_adx_compat_player_set_live_transpose(&player, pitch, 0));
    CHECK(!observe(clock) && !rates(pitch,rate) && clock->source_frames >= before);
    printf("ADX-MONO rate=%" PRIu32 " before=%" PRIu64 " after=%" PRIu64 "\n",
           rate, before, clock->source_frames);
    return 0;
}
#endif

static int run(void) {
    CHECK(!load() && !snd_stream_init_ex(2, RING*2));
#ifndef TEST_BASELINE
    uint32_t initial = snd_mem_available();
#endif
    CHECK(!prepare());
#ifdef TEST_BASELINE
    const uint64_t deadline = timer_ms_gettime64() + 3000;
    enum kosdec_adx_compat_state state;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < deadline);
        CHECK(!kosdec_adx_compat_player_get_state(&player,&state));
        if(state == KOSDEC_ADX_COMPAT_PLAYING) break;
        thd_sleep(1);
    } while(true);
    CHECK(!kosdec_adx_compat_player_set_live_transpose(&player,-12,0) && !rates(-12,22050));
    struct kosdec_adx_time time = {.count = 17, .frequency = 3};
    CHECK(kosdec_adx_compat_player_get_time(&player,&time) < 0 && errno == ENOTSUP &&
          time.count == 17 && time.frequency == 3);
    CHECK(!kosdec_adx_player_stop(&player,1000));
    puts("ADX-MONO BASELINE transpose admitted, GetTime=ENOTSUP");
#else
    const uint64_t ready_deadline = timer_ms_gettime64()+3000;
    struct kosdec_adx_source_output clock;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < ready_deadline);
        if(!kosdec_adx_player_get_source_output(&player,1,&clock) && clock.valid) break;
        thd_sleep(1);
    } while(true);
    uint32_t owned = snd_mem_available();
    CHECK(initial-owned == RING*2);
    CHECK(!reach(RING-768,&clock) && !change(-12,22050,&clock));
    CHECK(!reach(2*RING-768,&clock) && !change(0,RATE,&clock));
    CHECK(!reach(3*RING+1024,&clock));
    const uint64_t done_deadline = timer_ms_gettime64()+2500;
    bool completed;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < done_deadline);
        CHECK(!kosdec_adx_compat_player_is_completed(&player,&completed));
        if(completed) break;
        CHECK(!observe(&clock) && clock.source_frames <= FRAMES);
        thd_sleep(1);
    } while(true);
    enum kosdec_adx_compat_state state;
    struct kosdec_adx_time time;
    int64_t real;
    CHECK(!kosdec_adx_compat_player_get_state(&player,&state) && state == KOSDEC_ADX_COMPAT_PLAYEND);
    CHECK(!kosdec_adx_compat_player_get_time(&player,&time) && time.count == FRAMES && time.frequency == RATE);
    CHECK(!kosdec_adx_compat_player_get_time_real(&player,&real) &&
          real == (int64_t)(((float)FRAMES/RATE)*100.0f));
    CHECK(!kosdec_adx_player_get_source_output(&player,1,&clock) && clock.software_stopped && !clock.valid);
    CHECK(!kosdec_adx_player_stop(&player,1000) && snd_mem_available() == owned);
    puts("ADX-MONO PASS ordered rate changes through PLAYEND");
#endif
    return 0;
}

int main(void) {
    (void)run();
    fflush(stdout);
    for(;;)
        thd_sleep(1000);
}
