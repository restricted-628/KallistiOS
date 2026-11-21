#include <kos.h>
#include <dc/g2bus.h>
#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "snd_flycast_pcm16_pause.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#ifndef ADX_TEST_LIVE_TRANSPOSE
#define ADX_TEST_LIVE_TRANSPOSE 0
#endif

#ifndef ADX_TEST_WIDE_HELD
#define ADX_TEST_WIDE_HELD 0
#endif

#ifndef ADX_TEST_PAUSE_RATE
#define ADX_TEST_PAUSE_RATE 44100
#endif

#ifndef ADX_TEST_PAUSE_MONO
#define ADX_TEST_PAUSE_MONO 0
#endif

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
    RATE = ADX_TEST_PAUSE_RATE,
    OUTPUT_RATE = ADX_TEST_LIVE_TRANSPOSE ? 22050 : RATE,
    WIDE_ENDPOINT = ADX_TEST_WIDE_HELD == 2,
    FAST_RATE = WIDE_ENDPOINT ? 186736 : 88200,
    FAST_PITCH = WIDE_ENDPOINT ? 0x103c : 0x0800,
    FAST_SEMITONES = WIDE_ENDPOINT ? 25 : 12,
    FAST_CENTS = WIDE_ENDPOINT ? -1 : 0,
    RING = WIDE_ENDPOINT ? 65504 : (ADX_TEST_WIDE_HELD ? 32768 : 16384),
    QUEUE = WIDE_ENDPOINT ? 262144 : (ADX_TEST_WIDE_HELD ? 131072 : 32768),
    FRAMES = ADX_TEST_WIDE_HELD == 1 ? 262144 : (ADX_TEST_LATE_CUT ? 393216 : 196608),
    CHANNELS = ADX_TEST_PAUSE_MONO ? 1 : 2,
    TRACK_HEADER = 32, TRACK_BLOCK = 18*CHANNELS, TRACK_FOOTER = 18,
    TRACK_SIZE = TRACK_HEADER + FRAMES / 32 * TRACK_BLOCK + TRACK_FOOTER,
    CYCLES = ADX_TEST_WIDE_HELD ? 2 : 5, HOLD_MS = RING * 1000 / OUTPUT_RATE + 40
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
    uint64_t committed;
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
    if(ADX_TEST_PAUSE_MONO) {
        CHECK(!kosdec_adx_compat_player_enable_mono_clock(p));
    } else {
        CHECK(!kosdec_adx_compat_player_enable_stereo_transpose(p));
    }
#ifdef ADX_TEST_ENABLE_ACTIVE_PAUSE
    if(!lane) {
        CHECK(!ADX_TEST_ENABLE_ACTIVE_PAUSE(p));
        if(RATE == 32000 || ADX_TEST_LIVE_TRANSPOSE)
            CHECK(!kosdec_adx_compat_player_enable_playend(p, 1000));
    }
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
    const uint64_t deadline = timer_ms_gettime64() + (ADX_TEST_LIVE_TRANSPOSE ? 4300 : 3000);
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

static bool device_pitch(uint32_t pitch) {
    for(unsigned ch = 0; ch < CHANNELS; ++ch) {
        uintptr_t reg = 0xa0700018u + 0x80u * ch;
        if((g2_read_32(reg) & 0x7fffu) != pitch)
            return false;
    }
    return true;
}

static int live_transpose(struct kosdec_adx_player *p) {
    snd_ordinary_pcm16_status_t before, after;
    const uint64_t deadline = timer_ms_gettime64() + 1000;
    CHECK(device_pitch(0));
    for(;;) {
        CHECK(!snd_ordinary_pcm16_status(p->ordinary_pcm, &before));
        if(!kosdec_adx_compat_player_set_live_transpose(p, -12, 0))
            break;
        CHECK(errno == EAGAIN && timer_ms_gettime64() < deadline);
        struct kosdec_adx_player_info info;
        CHECK(!kosdec_adx_player_get_info(p, &info) && info.output_rate == RATE &&
              info.state == KOSDEC_ADX_PLAYER_PLAYING && !info.first_error);
        CHECK(!service_title() && device_pitch(0));
        thd_sleep(1);
    }
    CHECK(device_pitch(0x7800));
    CHECK(!snd_ordinary_pcm16_status(p->ordinary_pcm, &after));
    CHECK(before.rate == RATE && after.rate == OUTPUT_RATE &&
          after.frames[0] >= before.frames[0] && after.frames[1] >= before.frames[1]);
    int32_t semitones, cents;
    CHECK(!kosdec_adx_compat_player_get_transpose(p, &semitones, &cents) &&
          semitones == -12 && cents == 0);
    struct title_snapshot transposed;
    CHECK(!snapshot(p, &transposed));
    printf("ADX-FIVE update before=%" PRIu64 "/%" PRIu64
           " after=%" PRIu64 "/%" PRIu64 " pitch=7800\n",
           before.frames[0], before.frames[1], after.frames[0], after.frames[1]);
    return 0;
}

static int stage_held_transpose(struct kosdec_adx_player *p, bool restore) {
    CHECK(!kosdec_adx_compat_player_set_live_transpose(p, 0, 0));
    int32_t semitones, cents;
    CHECK(!kosdec_adx_compat_player_get_transpose(p, &semitones, &cents) &&
          semitones == 0 && cents == 0);
    if(restore) {
        /* A rejected replacement changes the request cache, not the stage. */
        CHECK(kosdec_adx_compat_player_set_live_transpose(p, 12, 0) < 0 && errno == ENOTSUP);
    } else {
        CHECK(!kosdec_adx_compat_player_set_live_transpose(p, -12, 0));
    }
    CHECK(!kosdec_adx_compat_player_get_transpose(p, &semitones, &cents) &&
          semitones == (restore ? 12 : -12) && cents == 0);
    struct kosdec_adx_player_info info;
    CHECK(!kosdec_adx_player_get_info(p, &info) && info.output_rate == OUTPUT_RATE);
    CHECK(device_pitch(0x7800));
    return 0;
}

static int stage_wide_pitch(struct kosdec_adx_player *p, bool restore, uint32_t old_rate) {
    const int desired = restore ? 0 : FAST_SEMITONES;
    const int detune = restore ? 0 : FAST_CENTS;
    CHECK(!kosdec_adx_compat_player_set_live_transpose(p, desired, detune));
    int32_t semitones, cents;
    CHECK(!kosdec_adx_compat_player_get_transpose(p, &semitones, &cents) &&
          semitones == desired && cents == detune);
    struct kosdec_adx_player_info info;
    CHECK(!kosdec_adx_player_get_info(p, &info) && info.output_rate == old_rate);
    CHECK(device_pitch(restore ? FAST_PITCH : 0x7800));
    return 0;
}

static int cycle(struct kosdec_adx_player *p, struct cycle_result *result, uint64_t target,
                 int32_t request, uint32_t owned, unsigned index) {
    const bool restore = ADX_TEST_LIVE_TRANSPOSE && index == 1;
    const uint32_t old_rate = ADX_TEST_WIDE_HELD && restore ? FAST_RATE :
        (ADX_TEST_LIVE_TRANSPOSE && index >= 2 ? RATE : OUTPUT_RATE);
    const uint32_t new_rate = restore ? RATE : (ADX_TEST_WIDE_HELD ? FAST_RATE : old_rate);
    CHECK(!await_source(p, target, &result->before));
    struct kosdec_adx_player_info before;
    CHECK(!kosdec_adx_player_get_info(p, &before) && before.channels == CHANNELS && !before.starvations);
    CHECK(before.sample_rate == RATE && before.output_rate == old_rate);
    if(VIDEO_HZ) CHECK(!snapshot(p, &result->before));
    uint32_t pause_anchor_tick = p->compat_vsync_anchor_tick;
    if(lose_cut_once) {
        CHECK(kosdec_adx_compat_player_pause(p, true) < 0 && errno == ETIMEDOUT && cut_lost);
        int32_t rejected = -123;
        CHECK(kosdec_adx_compat_player_discard_samples(p, 128, &rejected) < 0 &&
              errno == EBUSY && rejected == -123);
        CHECK(kosdec_adx_compat_player_pause(p, false) < 0 && errno == EBUSY);
        if(restore) {
            CHECK(kosdec_adx_compat_player_set_live_transpose(p, 0, 0) < 0 && errno == EBUSY);
            int32_t semitones, cents;
            CHECK(!kosdec_adx_compat_player_get_transpose(p, &semitones, &cents) &&
                  semitones == (ADX_TEST_WIDE_HELD ? FAST_SEMITONES : -12) &&
                  cents == (ADX_TEST_WIDE_HELD ? FAST_CENTS : 0));
        }
        if(VIDEO_HZ) CHECK(!deliver_tick());
        else CHECK(!service_title());
        CHECK(!snapshot(p, &result->held) && !check_frozen(&result->before, &result->held));
        CHECK(!kosdec_adx_compat_player_pause(p, true) && retry_cut == lost_cut);
    } else {
        CHECK(!kosdec_adx_compat_player_pause(p, true));
    }
    CHECK(!snapshot(p, &result->cut));
    if(RATE != 44100 || ADX_TEST_LIVE_TRANSPOSE) {
        snd_ordinary_pcm16_status_t status;
        CHECK(!snd_ordinary_pcm16_status(p->ordinary_pcm, &status));
        result->committed = status.committed_frames;
    }
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
    if(ADX_TEST_LIVE_TRANSPOSE && index < 2) {
        if(ADX_TEST_WIDE_HELD) {
            CHECK(!stage_wide_pitch(p, restore, old_rate));
        } else {
            CHECK(!stage_held_transpose(p, restore));
        }
        CHECK(!snapshot(p, &result->held) && !check_frozen(&result->cut, &result->held));
        CHECK(result->held.clock.output_frames == result->cut.clock.output_frames &&
              result->held.clock.next_source_frame == result->cut.clock.next_source_frame);
    }
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
    if(ADX_TEST_LIVE_TRANSPOSE) {
        struct kosdec_adx_player_info after;
        CHECK(!kosdec_adx_player_get_info(p, &after) && after.output_rate == new_rate);
        CHECK(device_pitch(new_rate == FAST_RATE ? FAST_PITCH : (new_rate == RATE ? 0 : 0x7800)));
        if(ADX_TEST_WIDE_HELD && !restore)
            printf("ADX-FIVE held pitch applied at checked %" PRIu32 " Hz START\n", new_rate);
        if(restore) puts("ADX-FIVE held pitch staged at cut and applied at START");
    }
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
    if(ADX_TEST_LIVE_TRANSPOSE) CHECK(!live_transpose(&players[0]));
    uint32_t owned = snd_mem_available();
    CHECK(initial - owned == 4u * CHANNELS * RING);
    const int32_t requests[] = {ADX_TEST_WIDE_HELD ? 128 : 0, 128, 256, 0, 128};
    const struct snd_ordinary_pcm16 *lease = players[0].ordinary_pcm;
    CHECK(lease);
    uint64_t target = RING / 2 + 2048;
    const uint64_t late_targets[] = {
        (WIDE_ENDPOINT ? RING : 2*RING)+9000, 3*RING-1900, 3*RING+1200,
        4*RING-1900, 4*RING+9500
    };
    for(unsigned i = 0; i < CYCLES; ++i) {
        uint64_t base = i ? cycles[i-1].cut.clock.output_frames : 0;
        if(ADX_TEST_LATE_CUT) {
            uint64_t elapsed = i ? cycles[i-1].cut.clock.source_frames : 0;
            uint32_t preroll = i ? SND_FLYCAST_PCM16_PAUSE_PREROLL : 0;
            if(WIDE_ENDPOINT && i == 1) preroll = snd_flycast_pcm16_resume_preroll(FAST_RATE);
            target = elapsed + late_targets[i] - preroll;
        }
        lose_cut_once = i == 1;
        CHECK(!cycle(&players[0], &cycles[i], target, requests[i], owned, i));
        if(ADX_TEST_LATE_CUT) {
            uint64_t before = cycles[i].before.clock.output_frames - base;
            uint64_t cut = cycles[i].cut.clock.output_frames - base;
            CHECK(before >= late_targets[i] && cut > before &&
                  cut > ((WIDE_ENDPOINT && i == 0) ? RING : 2*RING));
            if(!ADX_TEST_WIDE_HELD)
                CHECK(cut/RING == before/RING + (i == 1 || i == 3));
        }
        struct kosdec_adx_player_info state;
        CHECK(players[0].ordinary_pcm == lease &&
              !kosdec_adx_player_get_info(&players[0], &state) && state.sources_started == 1);
        target = cycles[i].cut.clock.source_frames + RING / 2 + 2048 + 257 * (i % 2);
    }
    uint32_t video_events = service.vsync_count - (UINT32_MAX - 4);
    if(RATE == 32000 || ADX_TEST_LIVE_TRANSPOSE) {
        bool completed = false;
        const uint64_t deadline = timer_ms_gettime64() + (ADX_TEST_LIVE_TRANSPOSE ? 5700 : 4000);
        do {
            CHECK(timer_ms_gettime64() < deadline && !service_title());
            CHECK(!kosdec_adx_compat_player_is_completed(&players[0], &completed));
            if(!completed) thd_sleep(1);
        } while(!completed);

        enum kosdec_adx_compat_state terminal;
        struct kosdec_adx_time elapsed;
        int64_t real;
        CHECK(!kosdec_adx_compat_player_get_state(&players[0], &terminal) &&
              terminal == KOSDEC_ADX_COMPAT_PLAYEND);
        CHECK(!kosdec_adx_compat_player_get_time(&players[0], &elapsed) &&
              elapsed.count == FRAMES && elapsed.frequency == RATE);
        CHECK(!kosdec_adx_compat_player_get_time_real(&players[0], &real) &&
              real == (int64_t)(((float)FRAMES / RATE) * 100.0f));
        puts("ADX-FIVE terminal PLAYEND at nominal source duration");
    }
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
    CHECK(owned - fresh_owned == 2u * CHANNELS * RING && !checked_stop(1, fresh_owned));
    for(unsigned i = 0; i < CYCLES; ++i) {
        const struct cycle_result *r = &cycles[i];
        if(ADX_TEST_LATE_CUT) {
            uint64_t base = i ? cycles[i-1].cut.clock.output_frames : 0;
            printf("ADX-FIVE late=%u local-before=%" PRIu64 " local-cut=%" PRIu64 "\n",
                   i+1, r->before.clock.output_frames-base, r->cut.clock.output_frames-base);
        }
        if(RATE != 44100 || ADX_TEST_LIVE_TRANSPOSE)
            printf("ADX-FIVE rate=%u cycle=%u committed=%" PRIu64 "\n",
                   (unsigned)RATE, i+1, r->committed);
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
    if(ADX_TEST_WIDE_HELD)
        puts("ADX-FIVE PASS wide held pitch and checked stop");
    else
        puts("ADX-FIVE PASS five pauses, real PCM survivors, checked stop and fresh title");
    return 0;
}

int main(void) {
    (void)run();
    fflush(stdout);
    for(;;)
        thd_sleep(1000);
}
