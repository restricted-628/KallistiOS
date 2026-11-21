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

#ifndef ADX_TEST_LATE_CUT
#define ADX_TEST_LATE_CUT 0
#endif

#ifndef ADX_TEST_VSYNC_MODE
#define ADX_TEST_VSYNC_MODE 0
#endif
#if ADX_TEST_VSYNC_MODE == 1
enum { VIDEO_HZ = 5994, DISPLAY_OFFSET = -17 };
#elif ADX_TEST_VSYNC_MODE == 2
enum { VIDEO_HZ = 5000, DISPLAY_OFFSET = -17 };
#else
enum { VIDEO_HZ = 0, DISPLAY_OFFSET = 0 };
#endif

enum {
    RATE = 44100, RING = 16384, QUEUE = 32768,
    FRAMES = ADX_TEST_LATE_CUT ? 393216 : 196608,
    TRACK_HEADER = 32, TRACK_BLOCK = 36, TRACK_FOOTER = 18,
    TRACK_SIZE = TRACK_HEADER + FRAMES / 32 * TRACK_BLOCK + TRACK_FOOTER,
    CYCLES = 5, HOLD_MS = RING * 1000 / RATE + 40
};

struct title_snapshot {
    struct kosdec_adx_source_output clock;
    struct kosdec_adx_time time;
    int64_t real;
    enum kosdec_adx_compat_state state;
};

struct cycle_result {
    struct title_snapshot before, cut, held, resumed;
    int32_t requested, removed;
    uint32_t memory_free;
};

static struct kosdec_adx_player players[2];
static struct kosdec_adx_stream streams[2];
static struct kosdec_adx_compat_service service;
static struct kosdec_adx_compat_title title;
static struct cycle_result cycles[CYCLES];
static _Alignas(32) uint8_t track[2][TRACK_SIZE], stack[32768];
static _Alignas(32) int16_t pcm[QUEUE * 2], scratch[RING + 16];

#define CHECK(test) do { \
    if(!(test)) { \
        fprintf(stderr, "ADX-FIVE FAIL line=%d check=%s errno=%d\n", __LINE__, #test, errno); \
        return -1; \
    } \
} while(0)

static bool lose_cut_once, cut_lost;
static unsigned private_pauses;
static uint64_t lost_cut, retry_cut;

int __real_snd_flycast_pcm16_pause(snd_ordinary_pcm16_t *, uint32_t, uint64_t *);
int __wrap_snd_flycast_pcm16_pause(snd_ordinary_pcm16_t *pcm, uint32_t timeout,
                                   uint64_t *cut) {
    ++private_pauses;
    int result = __real_snd_flycast_pcm16_pause(pcm, timeout, cut);
    if(!result && lose_cut_once) {
        lost_cut = *cut;
        lose_cut_once = false;
        cut_lost = true;
        errno = ETIMEDOUT;
        return -1;
    }
    if(!result && cut_lost && !retry_cut) retry_cut = *cut;
    return result;
}

static int load(unsigned lane, const char *path) {
    file_t file = fs_open(path, O_RDONLY);
    CHECK(file != FILEHND_INVALID);
    for(size_t at = 0; at < TRACK_SIZE; ) {
        ssize_t read = fs_read(file, track[lane] + at, TRACK_SIZE - at);
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
    memcpy(dest, (const uint8_t *)context + offset, size);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, size};
}

static uint64_t clock_us(void *context) {
    (void)context;
    return timer_us_gettime64();
}

static int play(unsigned lane) {
    static const struct kosdec_adx_player_config config = {
        .pcm = pcm, .pcm_samples = QUEUE * 2, .capacity = QUEUE,
        .scratch = scratch, .scratch_samples = RING + 16,
        .stack = stack, .stack_bytes = sizeof(stack), .sound_bytes = RING * 2,
        .transfer_timeout_ms = 1000, .prefill_timeout_ms = 3000
    };
    const struct kosdec_adx_compat_config pacing = {.clock_us = clock_us};
    struct kosdec_adx_player *p = &players[lane];
    kosdec_adx_stream_init(&streams[lane], provide, track[lane], TRACK_SIZE, false);
    CHECK(!kosdec_adx_player_init(p, &streams[lane], NULL, &config));
    CHECK(!kosdec_adx_compat_service_init(&service, &title, 1, &pacing));
    if(VIDEO_HZ && !lane) {
        CHECK(!kosdec_adx_compat_enable_vsync_clock(&service));
        service.vsync_count = UINT32_MAX - 4;
    }
    CHECK(!kosdec_adx_compat_bind_player(&service, 0, p));
    if(VIDEO_HZ && !lane) {
        CHECK(!kosdec_adx_compat_player_set_time_mode(p, ADX_TEST_VSYNC_MODE));
        CHECK(!kosdec_adx_player_set_time_offset(p, DISPLAY_OFFSET));
    }
    CHECK(!kosdec_adx_compat_player_enable_stereo_transpose(p));
#ifdef ADX_TEST_ENABLE_ACTIVE_PAUSE
    if(!lane) { CHECK(!ADX_TEST_ENABLE_ACTIVE_PAUSE(p)); }
#endif
    CHECK(!kosdec_adx_player_start(p));
    return 0;
}

static int service_title(void) {
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_exec_server(&service, &pass));
    CHECK(!pass.failed_titles && !pass.failed_phases);
    return 0;
}

static int deliver_tick(void) {
    struct kosdec_adx_compat_result pass;
    CHECK(!kosdec_adx_compat_exec_vsync(&service, &pass));
    CHECK(!pass.failed_titles && !pass.failed_phases);
    return 0;
}

static int64_t video_anchor(uint64_t source) {
    return (int64_t)(((float)source / RATE) * VIDEO_HZ) + DISPLAY_OFFSET;
}

static int snapshot(struct kosdec_adx_player *p, struct title_snapshot *out) {
    CHECK(!kosdec_adx_player_get_source_output(p, 1, &out->clock));
    CHECK(out->clock.valid && !out->clock.source_output_end && !out->clock.software_stopped);
    CHECK(!kosdec_adx_compat_player_get_state(p, &out->state) &&
          out->state == KOSDEC_ADX_COMPAT_PLAYING);
    bool completed = true;
    CHECK(!kosdec_adx_compat_player_is_completed(p, &completed) && !completed);
    CHECK(!kosdec_adx_compat_player_get_time(p, &out->time));
    if(VIDEO_HZ) {
        CHECK(!kosdec_adx_compat_player_get_time(p, &out->time));
        CHECK(out->time.frequency == VIDEO_HZ);
    } else {
        CHECK(out->time.frequency == RATE && out->time.count >= 0 &&
              (uint64_t)out->time.count == out->clock.source_frames);
    }
    CHECK(!kosdec_adx_compat_player_get_time_real(p, &out->real));
    CHECK(out->real == (int64_t)(((float)out->time.count / out->time.frequency) * 100.0f));
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
          held->real == cut->real && held->state == cut->state);
    CHECK(held->clock.session == cut->clock.session && held->clock.generation == cut->clock.generation &&
          held->clock.source_frames == cut->clock.source_frames);
    return 0;
}

static int cycle(struct kosdec_adx_player *p, struct cycle_result *result, uint64_t target,
                 int32_t request, uint32_t owned) {
    CHECK(!await_source(p, target, &result->before));
    struct kosdec_adx_player_info before;
    CHECK(!kosdec_adx_player_get_info(p, &before) && before.channels == 2 && !before.starvations);
    if(VIDEO_HZ) CHECK(!snapshot(p, &result->before));
    uint32_t pause_anchor_tick = p->compat_vsync_anchor_tick;
    if(lose_cut_once) {
        CHECK(kosdec_adx_compat_player_pause(p, true) < 0 && errno == ETIMEDOUT && cut_lost);
        int32_t rejected = -123;
        CHECK(kosdec_adx_compat_player_discard_samples(p, 128, &rejected) < 0 &&
              errno == EBUSY && rejected == -123);
        CHECK(kosdec_adx_compat_player_pause(p, false) < 0 && errno == EBUSY);
        if(VIDEO_HZ) CHECK(!deliver_tick());
        else CHECK(!service_title());
        CHECK(!snapshot(p, &result->held) && !check_frozen(&result->before, &result->held));
        CHECK(!kosdec_adx_compat_player_pause(p, true) && retry_cut == lost_cut);
    } else {
        CHECK(!kosdec_adx_compat_player_pause(p, true));
    }
    CHECK(!snapshot(p, &result->cut));
    CHECK(result->cut.clock.source_frames >= result->before.clock.source_frames);
    if(VIDEO_HZ) {
        CHECK(result->cut.time.count == video_anchor(result->cut.clock.source_frames));
        CHECK(p->compat_vsync_anchor_tick == pause_anchor_tick);
        CHECK(!deliver_tick());
    }
    unsigned cut_queries = private_pauses;
    CHECK(!kosdec_adx_compat_player_pause(p, true) && private_pauses == cut_queries);
    CHECK(!snapshot(p, &result->held) && !check_frozen(&result->cut, &result->held));
    CHECK(result->held.clock.next_source_frame == result->cut.clock.next_source_frame);
    result->requested = request;
    result->removed = -1;
    CHECK(!kosdec_adx_compat_player_discard_samples(p, request, &result->removed));
    CHECK(result->removed == request);
    if(VIDEO_HZ) CHECK(p->compat_vsync_anchor_tick == service.vsync_count);
    const uint64_t deadline = timer_ms_gettime64() + HOLD_MS;
    uint64_t next_tick = timer_us_gettime64();
    uint32_t before_hold = service.vsync_count;
    do {
        if(VIDEO_HZ && timer_us_gettime64() >= next_tick) {
            CHECK(!deliver_tick());
            next_tick += UINT64_C(100000000) / (VIDEO_HZ ? VIDEO_HZ : 1);
        } else {
            CHECK(!service_title());
        }
        CHECK(!snapshot(p, &result->held));
        CHECK(!check_frozen(&result->cut, &result->held));
        CHECK(snd_mem_available() == owned);
        thd_sleep(1);
    } while(timer_ms_gettime64() < deadline);

    CHECK(!kosdec_adx_compat_player_pause(p, false));
    if(VIDEO_HZ) {
        struct title_snapshot restarted, next;
        CHECK(service.vsync_count - before_hold > 4 &&
              p->compat_vsync_anchor_tick == service.vsync_count);
        CHECK(!snapshot(p, &restarted) &&
              restarted.time.count == video_anchor(restarted.clock.source_frames));
        CHECK(!deliver_tick() && !snapshot(p, &next));
        CHECK(next.time.count == restarted.time.count + 100);
    }
    CHECK(!await_source(p, result->cut.clock.source_frames + 256, &result->resumed));
    CHECK(result->resumed.clock.epoch == result->cut.clock.epoch + 1);
    const uint64_t advanced = result->resumed.clock.source_frames - result->cut.clock.source_frames;
    const uint64_t expected = result->cut.clock.next_source_frame + (uint32_t)result->removed;
    CHECK(result->resumed.clock.next_source_frame == expected + advanced);
    result->memory_free = snd_mem_available();
    CHECK(result->memory_free == owned);
    return 0;
}

static int checked_stop(unsigned lane, uint32_t owned) {
    struct kosdec_adx_player *p = &players[lane];
    CHECK(!kosdec_adx_player_stop(p, 1000) && !kosdec_adx_player_stop(p, 1000));
    if(!lane) {
        struct kosdec_adx_source_output stopped;
        CHECK(!kosdec_adx_player_get_source_output(p, 1, &stopped) &&
              stopped.software_stopped && !stopped.valid && !stopped.stop_error);
    }
    enum kosdec_adx_compat_state state;
    CHECK(!kosdec_adx_compat_player_get_state(p, &state) && state == KOSDEC_ADX_COMPAT_STOP);
    CHECK(!kosdec_adx_compat_unbind(&service, 0) && snd_mem_available() == owned);
    return 0;
}

static int run(void) {
    CHECK(!load(0, "/cd/proof.adx") && !load(1, "/cd/fresh.adx"));
    CHECK(!snd_stream_init_ex(2, RING * 2));
    uint32_t initial = snd_mem_available();
    CHECK(!play(0));

    struct title_snapshot started;
    CHECK(!await_source(&players[0], RING / 2, &started));
    uint32_t owned = snd_mem_available();
    CHECK(initial - owned == 8u * RING);
    const int32_t requests[CYCLES] = {0, 128, 256, 0, 128};
    const struct snd_ordinary_pcm16 *lease = players[0].ordinary_pcm;
    CHECK(lease);
    uint64_t target = RING / 2 + 2048;
    const uint64_t late_targets[CYCLES] = {
        2*RING+9000, 3*RING-1900, 3*RING+1200, 4*RING-1900, 4*RING+9500
    };
    for(unsigned i = 0; i < CYCLES; ++i) {
        uint64_t base = i ? cycles[i-1].cut.clock.output_frames : 0;
        if(ADX_TEST_LATE_CUT) {
            uint64_t elapsed = i ? cycles[i-1].cut.clock.source_frames : 0;
            target = elapsed + late_targets[i] - (i ? SND_FLYCAST_PCM16_PAUSE_PREROLL : 0);
        }
        lose_cut_once = i == 1;
        CHECK(!cycle(&players[0], &cycles[i], target, requests[i], owned));
        if(ADX_TEST_LATE_CUT) {
            uint64_t before = cycles[i].before.clock.output_frames - base;
            uint64_t cut = cycles[i].cut.clock.output_frames - base;
            CHECK(before >= late_targets[i] && cut > before && cut > 2*RING);
            CHECK(cut/RING == before/RING + (i == 1 || i == 3));
        }
        struct kosdec_adx_player_info state;
        CHECK(players[0].ordinary_pcm == lease &&
              !kosdec_adx_player_get_info(&players[0], &state) && state.sources_started == 1);
        target = cycles[i].cut.clock.source_frames + RING / 2 + 2048 + 257 * (i % 2);
    }
    uint32_t video_events = service.vsync_count - (UINT32_MAX - 4);
    CHECK(!checked_stop(0, owned));

    /* A fresh title is deliberately outside the same-lease resume loop. */
    thd_sleep(300);
    CHECK(!play(1));
    struct kosdec_adx_time fresh;
    enum kosdec_adx_compat_state state;
    const uint64_t fresh_deadline = timer_ms_gettime64() + 3000;
    do {
        CHECK(!service_title() && timer_ms_gettime64() < fresh_deadline);
        CHECK(!kosdec_adx_compat_player_get_state(&players[1], &state));
        CHECK(!kosdec_adx_compat_player_get_time(&players[1], &fresh));
        if(state == KOSDEC_ADX_COMPAT_PLAYING && fresh.count >= 512)
            break;
        thd_sleep(1);
    } while(true);
    CHECK(fresh.frequency == RATE && fresh.count < RING / 2 &&
          players[1].ordinary_pcm != lease);
    uint32_t fresh_owned = snd_mem_available();
    CHECK(owned - fresh_owned == 4u * RING && !checked_stop(1, fresh_owned));
    for(unsigned i = 0; i < CYCLES; ++i) {
        const struct cycle_result *r = &cycles[i];
        if(ADX_TEST_LATE_CUT) {
            uint64_t base = i ? cycles[i-1].cut.clock.output_frames : 0;
            printf("ADX-FIVE late=%u local-before=%" PRIu64 " local-cut=%" PRIu64 "\n",
                   i+1, r->before.clock.output_frames-base, r->cut.clock.output_frames-base);
        }
        printf("ADX-FIVE cycle=%u session=%" PRIu32 " pre=%" PRIu64 " cut=%" PRIu64
               " first-unread=%" PRIu64 " discard-result=%" PRId32 " resumed-next=%" PRIu64
               " resumed-count=%" PRIu64 " epoch=%" PRIu64 " aram=%" PRIu32 "\n",
               i + 1, r->cut.clock.session, r->before.clock.source_frames, r->cut.clock.source_frames,
               r->cut.clock.next_source_frame, r->removed, r->resumed.clock.next_source_frame,
               r->resumed.clock.source_frames, r->resumed.clock.epoch, initial - r->memory_free);
    }
    printf("ADX-FIVE fresh-count=%" PRIu64 " same-title-aram=%" PRIu32
           " fresh-title-aram=%" PRIu32 "\n", (uint64_t)fresh.count,
           initial - owned, owned - fresh_owned);
    CHECK(cut_lost && retry_cut == lost_cut && private_pauses == CYCLES + 1);
    if(VIDEO_HZ) printf("ADX-FIVE timing hz=%u delivered=%" PRIu32 "\n", (unsigned)VIDEO_HZ, video_events);
    puts("ADX-FIVE PASS five pauses, real PCM survivors, checked stop and fresh title");
    return 0;
}

int main(void) {
    (void)run();
    fflush(stdout);
    for(;;)
        thd_sleep(1000);
}
