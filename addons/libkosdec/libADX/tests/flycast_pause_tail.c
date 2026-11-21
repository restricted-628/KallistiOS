#include <kos.h>
#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "snd_flycast_pcm16_pause.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#ifndef TEST_CASE
#define TEST_CASE 0
#endif
enum { RATE = 44100, RING = 16384, QUEUE = 32768, FRAME_BLOCK = 32,
       TRACK_HEADER = 32, TRACK_BLOCK = 36, TRACK_FOOTER = 18,
       FRAMES = TEST_CASE == 0 ? 22016 : 26048,
       TRACK_SIZE = TRACK_HEADER + FRAMES / FRAME_BLOCK * TRACK_BLOCK + TRACK_FOOTER,
       EARLY_LIMIT = TRACK_HEADER + 24576 / FRAME_BLOCK * TRACK_BLOCK };

struct title_snapshot {
    struct kosdec_adx_source_output clock;
    struct kosdec_adx_time time;
    int64_t real;
    enum kosdec_adx_compat_state state;
};

static struct kosdec_adx_player player;
static struct kosdec_adx_stream stream;
static struct kosdec_adx_compat_service service;
static struct kosdec_adx_compat_title title;
static _Alignas(32) uint8_t track[TRACK_SIZE], stack[32768];
static _Alignas(32) int16_t pcm[QUEUE * 2], scratch[RING + 16];
static _Atomic uint32_t available_bytes = TEST_CASE == 3 ? EARLY_LIMIT : TRACK_SIZE;
static unsigned reprimes;

#define CHECK(test) do { \
    if(!(test)) { \
        fprintf(stderr, "ADX-EOF FAIL line=%d check=%s errno=%d\n", __LINE__, #test, errno); \
        return -1; \
    } \
} while(0)

int __real_snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *, const int16_t *, uint32_t);
int __wrap_snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *data,
                                    uint32_t frames) {
    ++reprimes;
    return __real_snd_flycast_pcm16_resume(pcm, data, frames);
}

static int load(const char *path) {
    file_t file = fs_open(path, O_RDONLY);
    CHECK(file != FILEHND_INVALID);
    for(size_t at = 0; at < TRACK_SIZE; ) {
        ssize_t read = fs_read(file, track + at, TRACK_SIZE - at);
        CHECK(read > 0);
        at += (size_t)read;
    }
    CHECK(!fs_close(file));
    return 0;
}

static struct kosdec_adx_input_result provide(void *context, uint32_t offset,
                                              uint8_t *dest, size_t size) {
    if(offset > TRACK_SIZE || size > TRACK_SIZE - offset)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR, 0};
    uint32_t limit = __atomic_load_n(&available_bytes,__ATOMIC_ACQUIRE);
    if(offset >= limit)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT, 0};
    if(size > limit - offset)
        size = limit - offset;
    memcpy(dest, (const uint8_t *)context + offset, size);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, size};
}

static uint64_t clock_us(void *context) {
    (void)context;
    return timer_us_gettime64();
}

static int play(void) {
    static const struct kosdec_adx_player_config config = {
        .pcm = pcm, .pcm_samples = QUEUE * 2, .capacity = QUEUE,
        .scratch = scratch, .scratch_samples = RING + 16,
        .stack = stack, .stack_bytes = sizeof(stack), .sound_bytes = RING * 2,
        .transfer_timeout_ms = 1000, .prefill_timeout_ms = 3000
    };
    const struct kosdec_adx_compat_config pacing = {.clock_us = clock_us};
    struct kosdec_adx_player *p = &player;
    kosdec_adx_stream_init(&stream, provide, track, TRACK_SIZE, false);
    CHECK(!kosdec_adx_player_init(p, &stream, NULL, &config));
    CHECK(!kosdec_adx_compat_service_init(&service, &title, 1, &pacing));
    CHECK(!kosdec_adx_compat_bind_player(&service, 0, p));
    CHECK(!kosdec_adx_compat_player_enable_stereo_transpose(p));
    CHECK(!kosdec_adx_compat_player_enable_flycast_pause(p));
    CHECK(!kosdec_adx_compat_player_enable_playend(p,1000));
    CHECK(!kosdec_adx_player_start(p));
    return 0;
}

static int service_title(void) {
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_exec_server(&service, &pass));
    CHECK(!pass.failed_titles && !pass.failed_phases);
    return 0;
}

static int snapshot(struct kosdec_adx_player *p, struct title_snapshot *out) {
    CHECK(!kosdec_adx_player_get_source_output(p, 1, &out->clock));
    CHECK(out->clock.valid && !out->clock.source_output_end && !out->clock.software_stopped);
    CHECK(!kosdec_adx_compat_player_get_state(p, &out->state) &&
          (out->state == KOSDEC_ADX_COMPAT_PLAYING || out->state == KOSDEC_ADX_COMPAT_DECEND));
    bool completed = true;
    CHECK(!kosdec_adx_compat_player_is_completed(p, &completed) && !completed);
    CHECK(!kosdec_adx_compat_player_get_time(p, &out->time));
    CHECK(out->time.frequency == RATE && out->time.count >= 0 &&
          (uint64_t)out->time.count == out->clock.source_frames);
    CHECK(!kosdec_adx_compat_player_get_time_real(p, &out->real));
    CHECK(out->real == (int64_t)(((float)out->time.count / RATE) * 100.0f));
    return 0;
}

static int await_source(struct kosdec_adx_player *p, uint64_t target, struct title_snapshot *out) {
    const uint64_t deadline = timer_ms_gettime64() + 3000;
    struct kosdec_adx_source_output clock;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < deadline);
        if(!kosdec_adx_player_get_source_output(p, 1, &clock) &&
           clock.valid && clock.source_frames >= target)
            return snapshot(p, out);
        thd_sleep(1);
    } while(true);
}

static int check_frozen(const struct title_snapshot *cut, const struct title_snapshot *held) {
    CHECK(held->time.count == cut->time.count && held->time.frequency == cut->time.frequency &&
          held->real == cut->real);
    if(TEST_CASE == 2) {
        /* The final decoded queue may become empty during held service.
           DECEND still requires a successful resume and checked stop. */
        CHECK(held->state == cut->state || (cut->state == KOSDEC_ADX_COMPAT_PLAYING &&
                                           held->state == KOSDEC_ADX_COMPAT_DECEND));
    } else {
        CHECK(held->state == cut->state);
    }
    CHECK(held->clock.session == cut->clock.session && held->clock.generation == cut->clock.generation &&
          held->clock.source_frames == cut->clock.source_frames);
    return 0;
}

static int run(void) {
    CHECK(!load("/cd/proof.adx") && !snd_stream_init_ex(2, RING * 2));
    uint32_t initial = snd_mem_available();
    CHECK(!play());

    struct title_snapshot before, cut, held;
    CHECK(!await_source(&player, RING / 2 + 2048, &before));
    CHECK(!kosdec_adx_compat_player_pause(&player, true) && !snapshot(&player, &cut));
    CHECK(cut.clock.next_source_frame < FRAMES);
    struct kosdec_adx_player_info copied;
    CHECK(!kosdec_adx_player_get_info(&player, &copied));
    uint32_t unread = FRAMES - (uint32_t)cut.clock.next_source_frame;
    uint32_t submitted = (uint32_t)(copied.source_frames - cut.clock.next_source_frame);
    int32_t request = 128;
    if(TEST_CASE == 1)
        request = (unread - 512u) & ~15u;
    if(TEST_CASE == 2)
        request = (unread + 15u) & ~15u;
    CHECK(request > 0);
    int32_t removed = -123;
    CHECK(!kosdec_adx_compat_player_discard_samples(&player, request, &removed));
    uint32_t expected_removed = (uint32_t)request > unread ? unread : (uint32_t)request;
    CHECK(removed == (int32_t)expected_removed);
    if(TEST_CASE == 1 || TEST_CASE == 2)
        CHECK((uint32_t)removed > submitted);
    CHECK(!snapshot(&player, &held) && !check_frozen(&cut, &held));
    thd_sleep(420);
    CHECK(!service_title() && !snapshot(&player, &held) && !check_frozen(&cut, &held));

    if(TEST_CASE == 3) {
        CHECK(kosdec_adx_compat_player_pause(&player, false) < 0 && errno == EAGAIN && !reprimes);
        CHECK(!snapshot(&player, &held) && !check_frozen(&cut, &held));
        __atomic_store_n(&available_bytes,TRACK_SIZE,__ATOMIC_RELEASE);
        const uint64_t deadline = timer_ms_gettime64() + 1000;
        while(kosdec_adx_compat_player_pause(&player, false) < 0) {
            CHECK(errno == EAGAIN && timer_ms_gettime64() < deadline);
            CHECK(!service_title());
            thd_sleep(1);
        }
    } else {
        CHECK(!kosdec_adx_compat_player_pause(&player, false));
    }

    CHECK(reprimes == (TEST_CASE == 2 ? 0u : 1u));
    const uint64_t deadline = timer_ms_gettime64() + 2000;
    bool done;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < deadline);
        CHECK(!kosdec_adx_compat_player_is_completed(&player, &done));
        if(done) break;
        struct kosdec_adx_source_output running;
        CHECK(!kosdec_adx_player_get_source_output(&player, 1, &running));
        CHECK(running.source_frames <= (uint64_t)FRAMES - removed);
        thd_sleep(1);
    } while(true);

    enum kosdec_adx_compat_state state;
    struct kosdec_adx_time time;
    int64_t real;
    CHECK(!kosdec_adx_compat_player_get_state(&player, &state) &&
          state == KOSDEC_ADX_COMPAT_PLAYEND);
    CHECK(!kosdec_adx_compat_player_get_time(&player, &time) &&
          time.count == FRAMES && time.frequency == RATE);
    CHECK(!kosdec_adx_compat_player_get_time_real(&player, &real) &&
          real == (int64_t)(((float)FRAMES / RATE) * 100.0f));
    CHECK(!kosdec_adx_player_get_info(&player, &copied) &&
          copied.state == KOSDEC_ADX_PLAYER_STOPPED && !copied.first_error && !copied.stop_error);
    CHECK(!kosdec_adx_compat_unbind(&service, 0) && initial - snd_mem_available() == 131072);
    printf("ADX-EOF case=%d source-cut=%" PRIu64 " submitted=%" PRIu32
           " removed=%" PRId32 " last=%u reprimes=%u\n",
           TEST_CASE, cut.clock.next_source_frame, submitted, removed,
           (unsigned)FRAMES-1, reprimes);
    puts("ADX-EOF PASS terminal resume and checked PLAYEND");
    return 0;
}

int main(void) {
    (void)run();
    fflush(stdout);
    for(;;)
        thd_sleep(1000);
}
