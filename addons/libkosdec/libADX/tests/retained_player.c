/* Exercise the adapter with Audio's exact private interface and no device. */
#define main adx_legacy_mock_tests
#include "player.c"
#undef main
#include "snd_retained_pcm.h"
#ifdef MEDIA_ADX_RETAINED_START_TL
#include "snd_retained_start_tl.h"
#endif
#ifdef MEDIA_ADX_RETAINED_GAIN
#include "snd_retained_gain.h"
#endif

struct snd_retained_pcm {
    snd_retained_pcm_fill_t fill;
    void *context;
    unsigned channels;
    bool in_fill, stopped, backpressure, no_refill, stop_fails, valid;
    unsigned expected_rate, opens, starts, fills, stops;
    int injected_stop_error;
    uint64_t admitted[17], sealed, credited, reported_epoch;
    bool independent_generations;
    uint64_t source_credit[17];
    uint64_t missing_generation, stale_generation, foreign_generation, shifted_observation;
    snd_retained_pcm_span_t spans[SND_RETAINED_PCM_FILL_SPANS];
    unsigned span_count;
    int16_t pcm[2 * 1024];
#ifdef MEDIA_ADX_RETAINED_START_TL
    unsigned initial_calls, initial_left, initial_right, closes;
    unsigned close_failures;
    int start_error, first_start_error;
    bool unsupported_preflight;
#endif
#ifdef MEDIA_ADX_RETAINED_GAIN
    bool gain_supported, gain_retry, gain_reject_right;
    unsigned gain_calls;
    snd_retained_gain_status_t gain;
#endif
};
static snd_retained_pcm_t backend;
static uint32_t request_frames;
static unsigned span_capacity;
static bool require_zero_pcm;

int snd_retained_pcm_open(snd_retained_pcm_t **out, unsigned channels, uint32_t rate,
    uint32_t ring_frames, snd_retained_pcm_fill_t fill, void *context) {
    CHECK(channels == backend.channels && rate == backend.expected_rate && ring_frames == 1024 && fill && context);
    CHECK(!backend.starts);
    ++backend.opens;
    backend.fill = fill;
    backend.context = context;
    *out = &backend;
    return 0;
}

static int fill_mock(snd_retained_pcm_t *pcm) {
    CHECK(pcm == &backend && !pcm->stopped && !pcm->in_fill);
    pcm->in_fill = true;
    int n = pcm->fill(pcm->context,pcm->pcm,request_frames,pcm->spans,span_capacity);
    pcm->in_fill = false;
    if(n < 0) return -1;
    CHECK(n > 0 && (unsigned)n <= span_capacity);
    pcm->span_count = (unsigned)n;
    uint32_t cursor = 0;
    for(unsigned i = 0; i < pcm->span_count; ++i) {
        snd_retained_pcm_span_t span = pcm->spans[i];
        CHECK(span.frames && span.frames <= request_frames - cursor);
        if(span.kind == SND_RETAINED_PCM_SOURCE) {
            CHECK(span.generation >= 1 && span.generation <= 16);
            CHECK(span.generation > pcm->sealed);
            pcm->admitted[span.generation] += span.frames;
            if(require_zero_pcm) {
                for(uint32_t j = 0; j < span.frames * pcm->channels; ++j)
                    CHECK(pcm->pcm[cursor * pcm->channels + j] == 0);
            }
        }
        else CHECK(!span.generation && !span.first_frame);
        cursor += span.frames;
    }
    CHECK(cursor == request_frames);
    ++pcm->fills;
    return 0;
}

int snd_retained_pcm_start(snd_retained_pcm_t *pcm) {
    CHECK(pcm == &backend && pcm->starts == 0);
    ++pcm->starts;
    return fill_mock(pcm);
}
#ifdef MEDIA_ADX_RETAINED_START_TL
int snd_retained_pcm_start_with_tl(snd_retained_pcm_t *pcm,unsigned left,unsigned right) {
    CHECK(pcm == &backend && !pcm->starts && !pcm->initial_calls && left <= 255 && right <= 255);
    ++pcm->initial_calls;
    pcm->initial_left = left;
    pcm->initial_right = right;
    if(pcm->unsupported_preflight) {
        errno = ENOTSUP;
        return -1;
    }
    if(snd_retained_pcm_start(pcm) < 0) return -1;
    if(pcm->start_error) {
        pcm->first_start_error = pcm->start_error;
        errno = pcm->start_error;
        return -1;
    }
    return 0;
}
#endif
#ifdef MEDIA_ADX_RETAINED_GAIN
static int service_gain(snd_retained_pcm_t *pcm) {
    snd_retained_gain_status_t *gain = &pcm->gain;
    if(!gain->request || gain->state != SND_RETAINED_GAIN_PENDING) return 0;
    if(pcm->gain_retry) {
        gain->admitted = gain->unknown = SND_RETAINED_GAIN_LEFT;
        gain->error = EAGAIN;
        return 0;
    }
    const bool stereo = pcm->channels == 2;
    gain->admitted = SND_RETAINED_GAIN_LEFT | (stereo ? SND_RETAINED_GAIN_RIGHT : 0);
    gain->applied = SND_RETAINED_GAIN_LEFT;
    gain->acknowledged |= SND_RETAINED_GAIN_LEFT;
    gain->acknowledged_tl[0] = gain->requested_tl[0];
    gain->unknown = 0;
    if(pcm->gain_reject_right) {
        CHECK(stereo);
        gain->state = SND_RETAINED_GAIN_FAILED;
        gain->rejected = SND_RETAINED_GAIN_RIGHT;
        errno = gain->error = EIO;
        return -1;
    }
    gain->state = SND_RETAINED_GAIN_APPLIED;
    gain->applied = gain->acknowledged = gain->admitted;
    if(stereo)
        gain->acknowledged_tl[1] = gain->requested_tl[1];
    gain->error = 0;
    return 0;
}

int snd_retained_pcm_gain_begin(snd_retained_pcm_t *pcm, unsigned left_tl,
    unsigned right_tl, uint64_t *request) {
    ++backend.gain_calls;
    CHECK(pcm == &backend && !pcm->stopped && pcm->starts && left_tl <= 255 && right_tl <= 255);
    if(!pcm->gain_supported) { errno = ENOTSUP; return -1; }
    snd_retained_gain_status_t previous = pcm->gain;
    if(previous.request && previous.state == SND_RETAINED_GAIN_PENDING) {
        errno = EBUSY;
        return -1;
    }
    pcm->gain = (snd_retained_gain_status_t){
        .epoch = 90, .session = 7, .request = previous.request + 1,
        .state = SND_RETAINED_GAIN_PENDING, .acknowledged = previous.acknowledged,
        .requested_tl = {(uint8_t)left_tl,pcm->channels == 2 ? (uint8_t)right_tl : 0},
        .acknowledged_tl = {previous.acknowledged_tl[0],previous.acknowledged_tl[1]}
    };
    *request = pcm->gain.request;
    return 0;
}

int snd_retained_pcm_gain_status(const snd_retained_pcm_t *pcm, uint64_t request,
    snd_retained_gain_status_t *out) {
    if(!request || request != pcm->gain.request) { errno = ENOENT; return -1; }
    *out = pcm->gain;
    return 0;
}
#endif

int snd_retained_pcm_poll(snd_retained_pcm_t *pcm) {
#ifdef MEDIA_ADX_RETAINED_GAIN
    if(service_gain(pcm) < 0) return -1;
#endif
    if(pcm->backpressure) { errno = EAGAIN; return -1; }
    if(pcm->no_refill) return 0;
    return fill_mock(pcm);
}
int snd_retained_pcm_seal(snd_retained_pcm_t *pcm, uint64_t generation, uint64_t total) {
    CHECK(!pcm->in_fill && generation > pcm->sealed && generation <= 16);
    CHECK(total == pcm->admitted[generation]);
    pcm->sealed = generation;
    return 0;
}
int snd_retained_pcm_query(const snd_retained_pcm_t *pcm, uint64_t generation,
    snd_retained_pcm_snapshot_t *out) {
    if(generation > 16 || (generation && generation == pcm->missing_generation) ||
       (generation > pcm->sealed && !pcm->admitted[generation])) {
        errno = ENOENT;
        return -1;
    }
    uint64_t epoch = pcm->reported_epoch ? pcm->reported_epoch : 90;
    uint64_t source_credit = pcm->independent_generations ? pcm->source_credit[generation] : pcm->credited;
    *out = (snd_retained_pcm_snapshot_t){
        .epoch = epoch + (generation && generation == pcm->foreign_generation),
        .session = 7, .generation = generation,
        .output_frames = pcm->credited + 18, .source_frames = source_credit,
        .observation_end_ms = generation && generation == pcm->shifted_observation ? 1 : 0,
        .next_source_frame = 19, .valid = pcm->valid && !pcm->stopped &&
                                        (!generation || generation != pcm->stale_generation),
        .source_output_end = generation && generation <= pcm->sealed &&
                             source_credit == pcm->admitted[generation],
        .clock_error = pcm->stopped ? ECANCELED : pcm->valid ? 0 : ESTALE,
#ifdef MEDIA_ADX_RETAINED_START_TL
        .first_error = pcm->first_start_error,
#endif
        .software_stopped = pcm->stopped
    };
    return 0;
}
int snd_retained_pcm_stop(snd_retained_pcm_t *pcm, uint32_t timeout_ms) {
    CHECK(pcm == &backend && timeout_ms && pcm->starts == 1);
    ++pcm->stops;
    if(pcm->injected_stop_error) { errno = pcm->injected_stop_error; return -1; }
    if(pcm->stop_fails) { errno = ETIMEDOUT; return -1; }
    pcm->stopped = true;
    return 0;
}
int snd_retained_pcm_close(snd_retained_pcm_t *pcm) {
#ifdef MEDIA_ADX_RETAINED_START_TL
    CHECK(pcm == &backend && pcm->initial_calls == 1 && !pcm->starts && pcm->unsupported_preflight);
    ++pcm->closes;
    if(pcm->close_failures) {
        --pcm->close_failures;
        errno = pcm->closes == 1 ? EIO : EBUSY;
        return -1;
    }
    return 0;
#else
    (void)pcm;
    CHECK(false);
    return -1;
#endif
}

struct retained_fixture {
    struct fixture f;
    uint32_t offsets[2048];
    alignas(32) int16_t pcm[4096], scratch[1025];
};
static void retained_init_channels(struct retained_fixture *t, bool sequence, unsigned channels) {
    memset(&backend,0,sizeof(backend));
    backend.expected_rate = 44100;
    backend.channels = channels;
    request_frames = 64;
    span_capacity = SND_RETAINED_PCM_FILL_SPANS;
    require_zero_pcm = false;
    struct fixture *f = &t->f;
    init(f,channels,sequence);
    f->config.pcm = t->pcm;
    f->config.capacity = 2048;
    f->config.pcm_samples = 4096;
    f->config.scratch = t->scratch;
    f->config.scratch_samples = 1025;
    f->config.sound_bytes = 2048;
    CHECK(!kosdec_adx_player_init(&f->player,sequence ? NULL : &f->stream,
                                  sequence ? &f->sequence : NULL,&f->config));
    CHECK(kosdec_adx_player_enable_retained_clock(&f->player,t->offsets,2047) < 0);
    CHECK(!kosdec_adx_player_enable_retained_clock(&f->player,t->offsets,2048));
}
static void retained_init(struct retained_fixture *t, bool sequence) {
    retained_init_channels(t,sequence,2);
}
static void finish_retained(struct fixture *f) {
    for(unsigned i = 0; i < 2000; ++i) {
        CHECK(!kosdec_adx_player_poll(&f->player));
        if(info(f).state == KOSDEC_ADX_PLAYER_SOURCE_END) return;
        mock_pause();
    }
    CHECK(false);
}
static void zero_and_rebind(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct kosdec_adx_audio_time time = {91,92,93};
    CHECK(kosdec_adx_player_get_audio_time(&f->player,0,&time) < 0 && errno == EINVAL);
    CHECK(kosdec_adx_player_get_audio_time(&f->player,1,&time) < 0 && errno == ENODATA);
    CHECK(time.frames == 91 && time.centiseconds == 92 && time.sample_rate == 93);
    require_zero_pcm = true;
    memset(f->data + 50,0,3 * 36);
    f->player.producer.pipe.write_cursor = UINT32_MAX - 31;
    f->player.producer.pipe.read_cursor = UINT32_MAX - 31;
    CHECK(!kosdec_adx_player_start(&f->player));
    CHECK(!until_sound(f));
    CHECK(backend.fills == 1 && backend.admitted[1] == 64 && !backend.sealed);
    CHECK(backend.span_count == 1 && backend.spans[0].first_frame == 0);
    CHECK(kosdec_adx_player_set_rate(&f->player,22050) < 0 && errno == ENOTSUP);
    CHECK(kosdec_adx_player_set_pan(&f->player,0,0) < 0 && errno == ENOTSUP);
    backend.backpressure = true;
    CHECK(!kosdec_adx_player_poll(&f->player));
    CHECK(info(f).source_frames == 64 && backend.fills == 1 && !backend.sealed);
    backend.backpressure = false;
    finish_retained(f);
    CHECK(backend.admitted[1] == 65 && backend.sealed == 1 && backend.span_count == 2);
    CHECK(backend.spans[0].first_frame == 64 && backend.spans[0].frames == 1);
    CHECK(backend.spans[1].kind == SND_RETAINED_PCM_TERMINAL_SILENCE);
    struct kosdec_adx_source_output out;
    backend.credited = 65;
    CHECK(!kosdec_adx_player_get_source_output(&f->player,1,&out));
    CHECK(!out.valid && out.clock_error == ESTALE && !out.source_output_end && out.epoch == 90);
    CHECK(kosdec_adx_player_get_audio_time(&f->player,1,&time) < 0 && errno == ESTALE);
    CHECK(time.frames == 91 && time.centiseconds == 92 && time.sample_rate == 93);
    backend.valid = true;
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == 65 && !time.centiseconds && time.sample_rate == 44100);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,376));
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == 441 && time.centiseconds == 1);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,-505));
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == -440 && !time.centiseconds);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,-506));
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == -441 && time.centiseconds == -1);
    CHECK(!kosdec_adx_player_get_source_output(&f->player,1,&out));
    CHECK(out.valid && out.source_output_end && out.source_frames == 65 && out.output_frames == 83);
    CHECK(!kosdec_adx_player_release_source(&f->player));
    require_zero_pcm = false;
    for(uint64_t generation = 2; generation <= 16; ++generation) {
        reinitialize_source(f,2,44100,false);
        if(generation == 2) __atomic_store_n(&f->wait,true,__ATOMIC_RELEASE);
        CHECK(!kosdec_adx_player_bind_source(&f->player,&f->stream,NULL));
        if(generation == 2) {
            CHECK(!kosdec_adx_player_poll(&f->player));
            CHECK(backend.span_count == 1 && backend.spans[0].kind == SND_RETAINED_PCM_STARVATION);
            CHECK(!kosdec_adx_player_set_wait_start(&f->player,true));
            CHECK(!kosdec_adx_player_poll(&f->player));
            CHECK(backend.span_count == 1 && backend.spans[0].kind == SND_RETAINED_PCM_PAUSE_SILENCE);
            CHECK(!backend.admitted[2] && backend.sealed == 1);
            CHECK(!kosdec_adx_player_set_wait_start(&f->player,false));
            __atomic_store_n(&f->wait,false,__ATOMIC_RELEASE);
        }
        finish_retained(f);
        CHECK(backend.admitted[generation] == 65 && backend.sealed == generation);
        CHECK(info(f).source_generation == generation && backend.starts == 1);
        CHECK(!kosdec_adx_player_release_source(&f->player));
    }
    CHECK(kosdec_adx_player_bind_source(&f->player,&f->stream,NULL) < 0 && errno == EOVERFLOW);
    CHECK(info(f).source_generation == 16 && info(f).state == KOSDEC_ADX_PLAYER_IDLE);
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,16,&time));
    CHECK(time.frames == -441 && time.centiseconds == -1);
    backend.stop_fails = true;
    CHECK(kosdec_adx_player_stop(&f->player,10) < 0 && errno == ETIMEDOUT && !backend.stopped);
    backend.stop_fails = false;
    CHECK(!kosdec_adx_player_stop(&f->player,10) && backend.stopped && backend.stops == 2);
    CHECK(!kosdec_adx_player_get_source_output(&f->player,16,&out) && out.software_stopped);
    CHECK(kosdec_adx_player_get_audio_time(&f->player,16,&time) < 0 && errno == ECANCELED);
    CHECK(time.frames == -441 && time.centiseconds == -1 && time.sample_rate == 44100);
    free(f->data);
}

static void audio_time_range(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    put32(f->data + 8,1);
    f->data[16] = f->data[17] = 0;
    CHECK(!kosdec_adx_player_set_rate(&f->player,44100));
    CHECK(!kosdec_adx_player_start(&f->player));
    finish_retained(f);
    backend.valid = true;
    backend.credited = 65;
    struct kosdec_adx_audio_time time;
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == 65 && time.centiseconds == 6500 && time.sample_rate == 1);
    backend.credited = (uint64_t)INT64_MAX / 100 + 1;
    CHECK(kosdec_adx_player_get_audio_time(&f->player,1,&time) < 0 && errno == EOVERFLOW);
    CHECK(time.frames == 65 && time.centiseconds == 6500 && time.sample_rate == 1);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    free(f->data);

    retained_init(&t,false);
    CHECK(!kosdec_adx_player_start(&f->player));
    finish_retained(f);
    backend.valid = true;
    backend.credited = (uint64_t)INT64_MAX + 1;
    CHECK(kosdec_adx_player_get_audio_time(&f->player,1,&time) < 0 && errno == EOVERFLOW);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,-1));
    CHECK(!kosdec_adx_player_get_audio_time(&f->player,1,&time));
    CHECK(time.frames == INT64_MAX && time.centiseconds == INT64_MAX / 441);
    backend.credited = INT64_MAX;
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,1));
    CHECK(kosdec_adx_player_get_audio_time(&f->player,1,&time) < 0 && errno == EOVERFLOW);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    free(f->data);
}

struct tick_reader {
    uint32_t ticks;
    unsigned reads;
};

static uint32_t read_ticks(void *context) {
    struct tick_reader *clock = context;
    ++clock->reads;
    return clock->ticks;
}

static void vsync_time(enum kosdec_adx_vsync_mode mode, uint32_t frequency) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct tick_reader clock = {.ticks = UINT32_MAX};
    struct kosdec_adx_vsync_time out = {91,93};
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,1,&out) < 0 && errno == ENOTSUP);
    CHECK(kosdec_adx_player_enable_vsync_clock(&f->player,0,read_ticks,&clock) < 0 &&
          errno == EINVAL);
    CHECK(!kosdec_adx_player_enable_vsync_clock(&f->player,mode,read_ticks,&clock));
    CHECK(!kosdec_adx_player_set_wait_start(&f->player,true));
    CHECK(!kosdec_adx_player_start(&f->player));
    for(unsigned i = 0; !info(f).ready_start && i < 2000; ++i) {
        CHECK(!kosdec_adx_player_poll(&f->player));
        mock_pause();
    }
    CHECK(info(f).ready_start && !clock.reads && !backend.starts);
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,1,&out) < 0 && errno == ENODATA);
    CHECK(out.count == 91 && out.frequency == 93);

    CHECK(!kosdec_adx_player_set_wait_start(&f->player,false));
    CHECK(!until_sound(f));
    CHECK(clock.reads == 1);
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,1,&out) < 0 && errno == ESTALE);
    CHECK(out.count == 91 && out.frequency == 93 && clock.reads == 1);
    backend.valid = true;
    clock.ticks = 0;
    backend.credited = 64;
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out));
    CHECK(out.count == 100 && out.frequency == frequency);

    CHECK(!kosdec_adx_player_set_time_offset(&f->player,-9));
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out) && out.count == 91);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,0));
    backend.credited = 44100; /* Inject the SDK reference's 1 s clock sample. */
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out) && out.count == 100);
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out) && out.count == frequency);
    clock.ticks = 20;
    backend.credited = 882; /* Inject a 20 ms cursor to force reverse correction. */
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out));
    CHECK(out.count == frequency + 2000);
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,1,&out));
    CHECK(out.count == (int64_t)(frequency / 50));

    finish_retained(f);
    backend.credited = 65;
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,1,&out) < 0 && errno == ENOTSUP);
    CHECK(out.count == (int64_t)(frequency / 50));
    CHECK(!kosdec_adx_player_release_source(&f->player));
    reinitialize_source(f,2,44100,false);
    CHECK(!kosdec_adx_player_bind_source(&f->player,&f->stream,NULL));
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,1,&out) < 0 && errno == ENOENT);
    unsigned reads = clock.reads;
    backend.backpressure = true;
    CHECK(!kosdec_adx_player_poll(&f->player) && clock.reads == reads);
    backend.backpressure = false;
    backend.no_refill = true;
    backend.credited = 0;
    CHECK(!until_sound(f) && !backend.admitted[2] && clock.reads == reads);
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,2,&out) < 0 && errno == ENODATA);
    clock.ticks = 500;
    backend.no_refill = false;
    CHECK(!kosdec_adx_player_poll(&f->player));
    CHECK(!kosdec_adx_player_get_vsync_time(&f->player,2,&out));
    CHECK(out.count == 0 && out.frequency == frequency);
    CHECK(!kosdec_adx_player_cancel_source(&f->player));
    CHECK(kosdec_adx_player_get_vsync_time(&f->player,2,&out) < 0 && errno == ENODATA);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    free(f->data);
}

static void live_time_mode(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct tick_reader clock = {.ticks = 10};
    struct kosdec_adx_time out = {91,93};
    int64_t real = 97;
    CHECK(kosdec_adx_player_set_time_mode(&f->player,KOSDEC_ADX_TIME_NTSC) < 0 &&
          errno == ENOTSUP);
    CHECK(!kosdec_adx_player_enable_vsync_clock(&f->player,KOSDEC_ADX_VSYNC_NTSC,read_ticks,&clock));
    CHECK(!kosdec_adx_player_start(&f->player));
    CHECK(!until_sound(f) && clock.reads == 1);
    CHECK(kosdec_adx_player_get_time(&f->player,1,&out) < 0 && errno == ESTALE);
    CHECK(out.count == 91 && out.frequency == 93);
    CHECK(kosdec_adx_player_get_time_real(&f->player,1,&real) < 0 && errno == ESTALE && real == 97);
    backend.valid = true;
    backend.credited = 441;
    CHECK(!kosdec_adx_player_get_time(&f->player,1,&out));
    CHECK(out.count == 441 && out.frequency == 44100 && clock.reads == 1);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,-882));
    CHECK(!kosdec_adx_player_get_time_real(&f->player,1,&real) && real == -1);
    CHECK(!kosdec_adx_player_set_time_offset(&f->player,0));

    /* One handle, nine queries from the linked 2.00J active-mode witness. */
    static const struct {
        enum kosdec_adx_time_mode mode;
        unsigned tick, frames, count, frequency, real;
    } trace[] = {
        {KOSDEC_ADX_TIME_NTSC,11,735,100,5994,1},
        {KOSDEC_ADX_TIME_PAL,12,1764,200,5000,4},
        {KOSDEC_ADX_TIME_SOURCE,28,13230,13230,44100,30},
        {KOSDEC_ADX_TIME_PAL,30,17640,2000,5000,40},
        {KOSDEC_ADX_TIME_NTSC,36,35280,2600,5994,79},
        {KOSDEC_ADX_TIME_NTSC,36,35280,4795,5994,79},
        {KOSDEC_ADX_TIME_SOURCE,38,36750,36750,44100,83},
        {KOSDEC_ADX_TIME_PAL,40,38808,5195,5000,88},
        {KOSDEC_ADX_TIME_PAL,40,38808,4400,5000,88},
    };
    for(unsigned i = 0; i < sizeof(trace) / sizeof(trace[0]); ++i) {
        backend.credited = trace[i].frames;
        clock.ticks = trace[i].tick;
        unsigned reads = clock.reads;
        CHECK(!kosdec_adx_player_set_time_mode(&f->player,trace[i].mode));
        CHECK(kosdec_adx_player_set_time_mode(&f->player,3) < 0 && errno == EINVAL);
        CHECK(clock.reads == reads);
        CHECK(!kosdec_adx_player_get_time(&f->player,1,&out));
        CHECK(out.count == trace[i].count && out.frequency == trace[i].frequency);
        CHECK(!kosdec_adx_player_get_time_real(&f->player,1,&real) && real == trace[i].real);
        CHECK(clock.reads == reads + (trace[i].mode == KOSDEC_ADX_TIME_SOURCE ? 0 : 2));
    }

    finish_retained(f);
    backend.credited = 65;
    CHECK(kosdec_adx_player_get_time(&f->player,1,&out) < 0 && errno == ENOTSUP);
    CHECK(out.count == 4400 && out.frequency == 5000);
    CHECK(!kosdec_adx_player_set_time_mode(&f->player,KOSDEC_ADX_TIME_SOURCE));
    CHECK(!kosdec_adx_player_get_time(&f->player,1,&out) && out.count == 65);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    CHECK(kosdec_adx_player_set_time_mode(&f->player,KOSDEC_ADX_TIME_PAL) < 0 && errno == EINVAL);
    free(f->data);
}

static void compat_decode_tail(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock, .clock_context = &now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    enum kosdec_adx_compat_state state;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f->player));
    request_frames = 49;
    CHECK(!kosdec_adx_player_start(&f->player));
    for(unsigned pass = 0; pass < 16 && info(f).state == KOSDEC_ADX_PLAYER_PREFILL; ++pass)
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_PLAYING && backend.admitted[1] == 49);
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&result));
    CHECK(!kosdec_adx_compat_player_get_state(&f->player,&state));
    CHECK(state == KOSDEC_ADX_COMPAT_PLAYING && backend.fills == 1 && !backend.sealed);

    request_frames = 1;
    CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(!kosdec_adx_compat_player_get_state(&f->player,&state));
    CHECK(state == KOSDEC_ADX_COMPAT_DECEND && backend.admitted[1] == 50);
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_PLAYING && backend.fills == 2 && !backend.sealed);

    request_frames = 64;
    CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_SOURCE_END && backend.sealed == 1);
    CHECK(backend.admitted[1] == 65 && backend.spans[0].first_frame == 50 &&
          backend.spans[0].frames == 15);
    CHECK(!kosdec_adx_player_release_source(&f->player));
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
    CHECK(!kosdec_adx_player_bind_source(&f->player,&f->stream,NULL));
    CHECK(!kosdec_adx_compat_player_get_state(&f->player,&state));
    CHECK(state == KOSDEC_ADX_COMPAT_DECINFO);
    CHECK(!kosdec_adx_player_stop(&f->player,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
}

static void compat_live_time_qualification(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock, .clock_context = &now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,&f->player));
    CHECK(!kosdec_adx_player_start(&f->player));
    for(unsigned pass = 0; pass < 16 && info(f).state == KOSDEC_ADX_PLAYER_PREFILL; ++pass)
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_PLAYING && backend.admitted[1]);
    struct kosdec_adx_time out = {7, 9};
    int64_t centiseconds = 13;
    CHECK(kosdec_adx_compat_player_get_time(&f->player,&out) < 0 && errno == ESTALE);
    CHECK(out.count == 7 && out.frequency == 9);
    backend.valid = true;
    backend.credited = 32;
    CHECK(!kosdec_adx_compat_player_get_time(&f->player,&out));
    CHECK(out.count == 32 && out.frequency == 44100);
    CHECK(!kosdec_adx_compat_player_get_time_real(&f->player,&centiseconds) && centiseconds == 0);
    CHECK(!kosdec_adx_compat_player_set_time_mode(&f->player,KOSDEC_ADX_TIME_PAL));
    CHECK(kosdec_adx_compat_player_get_time(&f->player,&out) < 0 && errno == ENOTSUP);
    CHECK(out.count == 32 && out.frequency == 44100);
    CHECK(!kosdec_adx_player_stop(&f->player,1000));
    CHECK(!kosdec_adx_compat_player_get_time(&f->player,&out) && !out.count && out.frequency == 5000);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
}

static void compat_vsync_anchor(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct kosdec_adx_player *p = &f->player;
    struct tick_reader clock = {.ticks = UINT32_MAX - 1};
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock, .clock_context = &now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    CHECK(!kosdec_adx_player_enable_vsync_clock(p,KOSDEC_ADX_VSYNC_NTSC,read_ticks,&clock));
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_player_start(p));
    for(unsigned pass = 0; pass < 16 && info(f).state == KOSDEC_ADX_PLAYER_PREFILL; ++pass)
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_PLAYING && clock.reads == 1);
    CHECK(kosdec_adx_compat_player_set_live_transpose(p,-12,0)<0 && errno==ENOTSUP && !mock_updates);
    CHECK(p->compat_vsync_anchor == 0 && p->compat_vsync_anchor_tick == UINT32_MAX - 1);

    CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_NTSC));
    struct kosdec_adx_time out = {7,9};
    CHECK(kosdec_adx_compat_player_get_time(p,&out) < 0 && errno == ESTALE);
    CHECK(out.count == 7 && out.frequency == 9 && clock.reads == 1);

    backend.valid = true;
    backend.credited = 735;
    clock.ticks = UINT32_MAX;
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == 100 && out.frequency == 5994);
    CHECK(p->compat_vsync_anchor == 0 && p->compat_vsync_anchor_tick == UINT32_MAX - 1);
    CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_PAL));
    clock.ticks = 0;
    backend.credited = 1764;
    CHECK(!kosdec_adx_player_get_time(p,1,&out) && out.count == 200 && out.frequency == 5000);
    CHECK(p->vsync_count == 200 && p->vsync_last_tick == 0);
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == 200 && out.frequency == 5000);
    CHECK(p->compat_vsync_anchor == 0 && p->compat_vsync_anchor_tick == UINT32_MAX - 1);

    backend.credited = 44100;
    clock.ticks = 1;
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == 300);
    CHECK(p->compat_vsync_anchor == 5000 && p->compat_vsync_anchor_tick == 1);
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == 5000);
    CHECK(p->vsync_count == 200 && p->vsync_last_tick == 0);
    backend.valid = false;
    clock.ticks = 2;
    CHECK(kosdec_adx_compat_player_get_time(p,&out) < 0 && errno == ESTALE);
    CHECK(out.count == 5000 && p->compat_vsync_anchor_tick == 1);

    backend.valid = true;
    clock.ticks = 21474837;
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == INT32_MIN + 4952);
    CHECK(p->compat_vsync_anchor == 5000 && p->compat_vsync_anchor_tick == 21474837);

    CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_SOURCE));
    CHECK(!kosdec_adx_player_set_time_offset(p,1));
    backend.credited = INT32_MAX;
    CHECK(!kosdec_adx_compat_player_get_time(p,&out) && out.count == INT32_MIN && out.frequency == 44100);
    CHECK(!kosdec_adx_player_get_time(p,1,&out) && out.count == (int64_t)INT32_MAX + 1);
    CHECK(!kosdec_adx_player_stop(p,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
    puts("ADX_COMPAT_VSYNC_ANCHOR_PASS wrap, correction, qualification and native independence");
}

static void compat_collection_clock(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct kosdec_adx_player *p = &f->player;
    struct tick_reader native = {.ticks = 5000};
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock, .clock_context = &now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    CHECK(!kosdec_adx_player_enable_vsync_clock(p,KOSDEC_ADX_VSYNC_NTSC,read_ticks,&native));
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_enable_vsync_clock(&service));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_NTSC));
    CHECK(!kosdec_adx_player_start(p));
    for(unsigned pass = 0; pass < 16 && info(f).state == KOSDEC_ADX_PLAYER_PREFILL; ++pass)
        CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_PLAYING && native.reads == 1);
    uint32_t start;
    CHECK(!kosdec_adx_compat_get_vsync_count(&service,&start) && start > 0);
    CHECK(p->compat_vsync_anchor_tick == start && p->vsync_last_tick == native.ticks);

    struct kosdec_adx_time time = {7,9};
    CHECK(kosdec_adx_compat_player_get_time(p,&time)<0 && errno == ESTALE);
    CHECK(time.count == 7 && time.frequency == 9);
    backend.credited = 32;
    backend.valid = true;
    CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(!kosdec_adx_compat_exec_title(&service,0,&result));
    CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 0 && time.frequency == 5994);
    CHECK(!kosdec_adx_compat_exec_vsync(&service,&result));
    CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 100 && time.frequency == 5994);
    CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_PAL));
    CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 100 && time.frequency == 5000);
    CHECK(native.reads == 1);
    CHECK(!kosdec_adx_player_get_time(p,1,&time) && time.count == 0 && time.frequency == 5000);
    CHECK(native.reads == 2);
    CHECK(!kosdec_adx_player_stop(p,1000));
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
    puts("ADX_COMPAT_COLLECTION_CLOCK_PASS admission, shared tick and independent native reader");
}

static void compat_initial_attenuation(void) {
    const struct {
        bool enabled;
        int32_t requested;
        unsigned expected_tl;
        int error;
        bool unsupported_preflight;
        unsigned close_failures;
    } cases[] = {
        {false,-188,0,0,false,0},
        {true,-188,60,0,false,0},
        {true,-65724,60,0,false,0},
        {true,-65537,0,0,false,0},
        {true,-188,60,ENOTSUP,true,0},
        {true,-188,60,ENOTSUP,true,1},
        {true,-188,60,ENOTSUP,true,3},
        {true,-188,60,ENOTSUP,false,0},
        {true,-188,60,EIO,false,0}
    };
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        struct retained_fixture t;
        retained_init(&t,false);
        struct fixture *f=&t.f;
        struct kosdec_adx_player *p=&f->player;
        uint64_t now=0;
        const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
        uint64_t request=UINT64_MAX;
        CHECK(!kosdec_adx_compat_player_set_attenuation(p,cases[k].requested,&request) && !request);
#ifdef MEDIA_ADX_RETAINED_START_TL
        if(cases[k].enabled)
            CHECK(!kosdec_adx_compat_player_enable_initial_attenuation(p));
        backend.start_error=cases[k].error;
        backend.unsupported_preflight=cases[k].unsupported_preflight;
        backend.close_failures=cases[k].close_failures;
#else
        if(cases[k].enabled) {
            CHECK(kosdec_adx_compat_player_enable_initial_attenuation(p)<0 && errno==ENOTSUP);
            CHECK(!kosdec_adx_compat_unbind(&service,0));
            free(f->data);
            continue;
        }
#endif
        CHECK(!backend.starts && !kosdec_adx_player_start(p));
        for(unsigned step=0;step<32;++step) {
            struct kosdec_adx_compat_result result;
            now+=13332;
            if(kosdec_adx_compat_exec_server(&service,&result)<0) {
                CHECK(errno==cases[k].error);
                break;
            }
            if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)
                break;
        }
#ifdef MEDIA_ADX_RETAINED_START_TL
        CHECK(backend.initial_calls==cases[k].enabled);
        CHECK(backend.initial_left==cases[k].expected_tl && backend.initial_right==cases[k].expected_tl);
        if(cases[k].unsupported_preflight) {
            CHECK(backend.closes==1 && !backend.starts && !backend.fills && !backend.stops);
            if(cases[k].close_failures) {
                CHECK(p->retained_pcm==&backend && info(f).state==KOSDEC_ADX_PLAYER_FAILED &&
                      info(f).first_error==ENOTSUP && info(f).stop_error==EIO);
                for(unsigned n=1;n<cases[k].close_failures;++n) {
                    CHECK(kosdec_adx_player_stop(p,1000)<0 && errno==EBUSY);
                    CHECK(backend.closes==n+1 && !backend.stops && p->retained_pcm==&backend &&
                          info(f).state==KOSDEC_ADX_PLAYER_STOPPING &&
                          info(f).first_error==ENOTSUP && info(f).stop_error==EBUSY);
                }
            } else {
                CHECK(!p->retained_pcm && !info(f).stop_error);
            }
        } else {
            CHECK(!backend.closes && backend.starts==1 && p->retained_pcm==&backend);
        }
        if(cases[k].error && !cases[k].unsupported_preflight) {
            backend.stop_fails=true;
            CHECK(kosdec_adx_player_stop(p,1000)<0 && errno==ETIMEDOUT && p->retained_pcm==&backend);
            CHECK(info(f).first_error==cases[k].error && info(f).stop_error==ETIMEDOUT &&
                  info(f).state==KOSDEC_ADX_PLAYER_STOPPING && !backend.closes);
            backend.stop_fails=false;
        }
#endif
        if(cases[k].error)
            CHECK(info(f).first_error==cases[k].error && !p->compat_initial_tl);
        else
            CHECK(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END && info(f).source_frames==65);
        CHECK(!kosdec_adx_player_stop(p,1000));
#ifdef MEDIA_ADX_RETAINED_START_TL
        CHECK(info(f).state==KOSDEC_ADX_PLAYER_STOPPED && !info(f).stop_error);
        if(cases[k].unsupported_preflight) {
            CHECK(!p->retained_pcm && !backend.stops && backend.closes==cases[k].close_failures+1 &&
                  info(f).first_error==ENOTSUP);
        } else {
            CHECK(p->retained_pcm==&backend && !backend.closes && backend.stopped);
        }
#endif
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f->data);
    }
    puts("ADX_COMPAT_INITIAL_ATTENUATION_PASS opt-in, readback scale and checked stop");
}

#ifdef MEDIA_ADX_RETAINED_START_TL
static void prep_gain_request(struct kosdec_adx_player *p, int32_t gain) {
    uint64_t id=UINT64_MAX;
    int16_t cached;
    CHECK(!kosdec_adx_compat_player_set_attenuation(p,gain,&id) && !id);
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-188);
    CHECK(!mock_updates);
#ifdef MEDIA_ADX_RETAINED_GAIN
    CHECK(!backend.gain_calls);
#endif
}

static void prep_gain_dispatch(struct kosdec_adx_compat_service *service, struct fixture *f,
                               uint64_t *now, enum kosdec_adx_player_state target, bool hold) {
    bool ready=false;
    for(unsigned n=0;n<32;++n) {
        struct kosdec_adx_compat_result result;
        *now+=13332;
        CHECK(!kosdec_adx_compat_exec_server(service,&result));
        CHECK(!kosdec_adx_compat_player_is_ready_start(&f->player,&ready));
        if(hold?ready:info(f).state==target) return;
    }
    CHECK(false);
}

static void compat_prepared_attenuation(void) {
    const struct {
        bool enabled, paused, cancel, rebind;
        unsigned writes, tl;
        int32_t late[2];
    } cases[] = {
        {true,false,false,false,0,60,{0}},
        {true,false,false,false,1,60,{-188}},
        {true,false,false,true,1,255,{-65724}},
        {true,true,false,false,1,0,{65348}},
        {true,false,false,false,2,0,{-65724,65348}},
        {true,false,false,false,2,255,{65348,-65724}},
        {true,false,true,false,1,255,{-65724}},
        {false,false,false,false,1,0,{-65724}}
    };
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        struct retained_fixture t;
        retained_init(&t,false);
        struct fixture *f=&t.f;
        struct kosdec_adx_player *p=&f->player;
        uint64_t now=0;
        const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        struct kosdec_adx_compat_result result;
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
        if(cases[k].enabled)
            CHECK(!kosdec_adx_compat_player_enable_initial_attenuation(p));
        if(cases[k].paused)
            CHECK(!kosdec_adx_compat_player_pause(p,true));
        else
            CHECK(!kosdec_adx_player_set_wait_start(p,true));

        f->wait=true;
        CHECK(!kosdec_adx_player_start(p));
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
        enum kosdec_adx_compat_state state;
        CHECK(!kosdec_adx_compat_player_get_state(p,&state) && state==KOSDEC_ADX_COMPAT_DECINFO);
        prep_gain_request(p,-65724);
        CHECK(!p->compat_initial_tl);
        f->wait=false;
        prep_gain_dispatch(&service,f,&now,KOSDEC_ADX_PLAYER_PREFILL,true);
        CHECK(!kosdec_adx_compat_player_get_state(p,&state) &&
              state==(cases[k].paused?KOSDEC_ADX_COMPAT_PLAYING:KOSDEC_ADX_COMPAT_PREP));
        CHECK(!backend.opens && !backend.starts);
        CHECK(p->compat_initial_tl==(cases[k].enabled?60:0));
        for(unsigned n=0;n<cases[k].writes;++n)
            prep_gain_request(p,cases[k].late[n]);
        if(cases[k].cancel) {
            CHECK(p->compat_initial_tl==cases[k].tl);
            CHECK(!kosdec_adx_player_cancel_source(p) && !backend.opens && !p->compat_initial_tl);
            int16_t cached;
            CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-188);
            kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
            CHECK(!kosdec_adx_player_bind_source(p,&f->stream,NULL));
        }
        if(cases[k].paused)
            CHECK(!kosdec_adx_compat_player_pause(p,false));
        else
            CHECK(!kosdec_adx_player_set_wait_start(p,false));
        prep_gain_dispatch(&service,f,&now,KOSDEC_ADX_PLAYER_SOURCE_END,false);
        unsigned expected=cases[k].cancel?60:cases[k].tl;
        CHECK(backend.opens==1 && backend.starts==1 && backend.initial_calls==cases[k].enabled);
        CHECK(backend.initial_left==expected && backend.initial_right==expected);
        if(cases[k].rebind) {
            CHECK(!kosdec_adx_player_release_source(p));
            CHECK(!kosdec_adx_player_set_wait_start(p,true));
            kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
            CHECK(!kosdec_adx_player_bind_source(p,&f->stream,NULL));
            prep_gain_dispatch(&service,f,&now,KOSDEC_ADX_PLAYER_PREFILL,true);
            prep_gain_request(p,65348);
            CHECK(!p->compat_initial_tl);
            CHECK(!kosdec_adx_player_set_wait_start(p,false));
            prep_gain_dispatch(&service,f,&now,KOSDEC_ADX_PLAYER_SOURCE_END,false);
            CHECK(backend.opens==1 && backend.starts==1 && backend.initial_calls==1 &&
                  backend.initial_left==expected);
        }
        CHECK(!kosdec_adx_player_stop(p,1000) && !kosdec_adx_compat_unbind(&service,0));
        free(f->data);
    }
    puts("ADX_COMPAT_PREP_GAIN_PASS DECINFO boundary, last request, held starts, cancellation and reuse");
}
#endif

static void compat_routing_excludes_retained(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f=&t.f;
    struct kosdec_adx_player *p=&f->player;
    uint64_t now=0;
    const struct kosdec_adx_compat_config config = {.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(kosdec_adx_compat_player_set_direct_level(p,-15)<0 && errno==ENOTSUP);
    CHECK(kosdec_adx_compat_player_set_fx(p,7,-12)<0 && errno==ENOTSUP);
    CHECK(kosdec_adx_compat_player_set_fx_level(p,-20)<0 && errno==ENOTSUP);
    int32_t bus, db, transpose, cents;
    CHECK(!kosdec_adx_compat_player_get_direct_level(p,&db) && !db);
    CHECK(!kosdec_adx_compat_player_get_fx(p,&bus,&db) && !bus && db==-45);
    CHECK(!kosdec_adx_compat_player_cache_transpose(p,12,-50));
    CHECK(!kosdec_adx_compat_player_get_transpose(p,&transpose,&cents) && transpose==12 && cents==-50);
    CHECK(!p->compat_route_fields && p->output.channel[0].routing.direct_level==15);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    CHECK(!kosdec_adx_player_init(p,&f->stream,NULL,&f->config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(kosdec_adx_compat_player_set_direct_level(p,-15)==KOSDEC_ADX_ROUTING_STAGED);
    CHECK(kosdec_adx_player_enable_retained_clock(p,t.offsets,2048)<0 && errno==ENOTSUP);
    CHECK(!p->retained_offsets && p->output.channel[0].routing.direct_level==12);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
    puts("ADX_COMPAT_ROUTE_RETAINED_GUARD_PASS");
}

static void compat_initial_retained_pitch(void) {
    const struct {
        int semitones, cents;
        unsigned source_rate, override_rate, output_rate;
        bool pitch_first, skip_pitch, cancel, rebind;
    } cases[] = {
        {-12,0,44100,0,22050,false,false,false,true},
        {0,3,44100,0,44144,true,false,false,false},
        {0,0,17616,0,17604,false,false,false,false},
        {0,1,8000,0,8000,false,false,false,false},
        {0,0,48000,0,47976,false,false,false,false},
        {0,1,48000,0,0,false,false,false,false},
        {0,0,8000,0,0,false,false,false,false},
        {-12,0,11023,0,0,false,false,false,false},
        {12,0,44100,0,0,false,false,false,false},
        {12,0,44100,32000,32000,false,false,false,false},
        {0,0,8000,0,8000,false,true,false,false},
        {0,0,8000,0,8000,false,false,true,false}
    };
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        struct retained_fixture t;
        retained_init(&t,false);
        struct fixture *f=&t.f;
        struct kosdec_adx_player *p=&f->player;
        unsigned rate=cases[k].source_rate;
        for(unsigned byte=0;byte<4;++byte)
            f->data[8+byte]=(uint8_t)(rate>>(24-8*byte));
        if(cases[k].pitch_first)
            CHECK(!kosdec_adx_player_init(p,&f->stream,NULL,&f->config));
        backend.expected_rate=cases[k].output_rate;
        uint64_t now=0;
        const struct kosdec_adx_compat_config config={.clock_us=compat_clock,.clock_context=&now};
        struct kosdec_adx_compat_service service;
        struct kosdec_adx_compat_title title;
        CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
        if(!cases[k].skip_pitch)
            CHECK(!kosdec_adx_compat_player_set_initial_transpose(p,cases[k].semitones,cases[k].cents));
        if(cases[k].pitch_first)
            CHECK(!kosdec_adx_player_enable_retained_clock(p,t.offsets,2048));
        if(cases[k].override_rate)
            CHECK(!kosdec_adx_player_set_rate(p,cases[k].override_rate));
        CHECK(!kosdec_adx_compat_player_cache_transpose(p,3,21));
        CHECK(!kosdec_adx_player_start(p));
        if(cases[k].cancel) {
            CHECK(!kosdec_adx_player_cancel_source(p) && !backend.opens);
            kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
            CHECK(!kosdec_adx_player_bind_source(p,&f->stream,NULL));
        }
        for(unsigned source=0;source<(cases[k].rebind ? 2u : 1u);++source) {
            for(unsigned step=0;step<32;++step) {
                struct kosdec_adx_compat_result result;
                now+=13332;
                if(kosdec_adx_compat_exec_server(&service,&result)<0) {
                    CHECK(!cases[k].output_rate && errno==ENOTSUP);
                    break;
                }
                if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)
                    break;
            }
            CHECK(!mock_allocs && !mock_updates);
            if(!cases[k].output_rate) {
                CHECK(info(f).state==KOSDEC_ADX_PLAYER_FAILED && !backend.opens && !p->retained_pcm);
                break;
            }
            struct kosdec_adx_player_info state=info(f);
            CHECK(state.state==KOSDEC_ADX_PLAYER_SOURCE_END && state.sample_rate==rate &&
                  state.output_rate==cases[k].output_rate && state.source_frames==65);
            CHECK(backend.opens==1 && backend.starts==1 && p->retained_pcm==&backend);
            CHECK(backend.admitted[state.source_generation]==65);
            CHECK(kosdec_adx_compat_player_set_initial_transpose(p,0,0)<0 && errno==ENOTSUP);
            CHECK(kosdec_adx_player_set_rate(p,32000)<0 && errno==ENOTSUP);
            int32_t semitones, cents;
            CHECK(!kosdec_adx_compat_player_get_transpose(p,&semitones,&cents) &&
                  semitones==3 && cents==21);
            if(cases[k].rebind && source==0) {
                CHECK(!kosdec_adx_player_release_source(p));
                kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
                CHECK(!kosdec_adx_player_bind_source(p,&f->stream,NULL));
            }
        }
        CHECK(!kosdec_adx_player_stop(p,1000));
        CHECK(backend.stops==(cases[k].output_rate!=0));
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f->data);
    }
    puts("ADX_COMPAT_RETAINED_PITCH_PASS staged rate, exact admission, override, cancellation and rebind");
}

enum playend_case {
    PLAYEND_RETRY, PLAYEND_EXPLICIT, PLAYEND_LOST_EPOCH, PLAYEND_CANCEL,
    PLAYEND_EMPTY, PLAYEND_REBOUND_EMPTY, PLAYEND_FINAL_EMPTY, PLAYEND_FINAL_PCM,
    PLAYEND_FATAL_STOP, PLAYEND_FIBER
};

static void make_empty_source(struct fixture *f) {
    memset(f->data+12,0,4);
    memmove(f->data+50,f->data+50+3*36,18);
    f->bytes = 68;
    kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
}

static void compat_playend_case(enum playend_case scenario) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    struct kosdec_adx_player *p = &f->player;
    struct tick_reader clock = {0};
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us = compat_clock, .clock_context = &now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    enum kosdec_adx_compat_state state;
    bool completed = true;
    unsigned frames = 65;
    if(scenario == PLAYEND_EMPTY) {
        frames = 0;
        make_empty_source(f);
    }
    CHECK(!kosdec_adx_player_enable_vsync_clock(p,KOSDEC_ADX_VSYNC_NTSC,read_ticks,&clock));
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    if(scenario == PLAYEND_FIBER)
        CHECK(!kosdec_adx_compat_bind_player(&service,0,p));
    else
        CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_compat_player_enable_playend(p,1000));
    CHECK(!kosdec_adx_player_start(p));
    for(unsigned pass = 0; pass < 2000 && info(f).state != KOSDEC_ADX_PLAYER_SOURCE_END; ++pass) {
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
        if(scenario == PLAYEND_FIBER)
            mock_pause();
    }
    CHECK(info(f).state == KOSDEC_ADX_PLAYER_SOURCE_END && backend.sealed == 1);
    CHECK(!kosdec_adx_compat_player_get_state(p,&state) && state == KOSDEC_ADX_COMPAT_DECEND);
    CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);

    struct kosdec_adx_time time = {7,9};
    if(frames) {
        backend.valid = true;
        backend.credited = 32;
        clock.ticks = 80;
        CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_NTSC));
        CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 8000);
        CHECK(p->compat_vsync_anchor == 4);
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
        CHECK(!backend.stops && backend.span_count == 1 &&
              backend.spans[0].kind == SND_RETAINED_PCM_TERMINAL_SILENCE);
    }
    if(scenario == PLAYEND_REBOUND_EMPTY || scenario == PLAYEND_FINAL_EMPTY ||
       scenario == PLAYEND_FINAL_PCM) {
        /* Seal the successor while the first generation still has an
           unplayed tail. Keep their credited counts independent. */
        bool armed = scenario != PLAYEND_REBOUND_EMPTY;
        bool empty = scenario != PLAYEND_FINAL_PCM;
        CHECK(backend.credited == 32 && backend.admitted[1] == 65);
        CHECK(kosdec_adx_compat_player_enable_playend(p,1000)<0 && errno==EINVAL);
        CHECK(!kosdec_adx_player_release_source(p));
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
        if(armed)
            CHECK(!kosdec_adx_compat_player_enable_playend(p,1000));
        if(empty)
            make_empty_source(f);
        else
            kosdec_adx_stream_init(&f->stream,input,f,(uint32_t)f->bytes,false);
        CHECK(!kosdec_adx_player_bind_source(p,&f->stream,NULL));
        for(unsigned pass = 0; pass < 32 && info(f).state != KOSDEC_ADX_PLAYER_SOURCE_END; ++pass)
            CHECK(!kosdec_adx_compat_exec_server(&service,&result));
        CHECK(info(f).state == KOSDEC_ADX_PLAYER_SOURCE_END && backend.sealed == 2);
        backend.independent_generations = true;
        backend.source_credit[1] = 32;
        backend.source_credit[2] = empty ? 0 : 65;
        CHECK(!kosdec_adx_compat_exec_server(&service,&result) && !backend.stops);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
        backend.source_credit[1] = 65;
        if(armed) {
            uint64_t *bad_receipts[] = {&backend.missing_generation, &backend.stale_generation,
                                       &backend.foreign_generation, &backend.shifted_observation};
            for(unsigned i=0;i<sizeof(bad_receipts)/sizeof(*bad_receipts);++i) {
                *bad_receipts[i] = 1;
                CHECK(!kosdec_adx_compat_exec_server(&service,&result) && !backend.stops);
                CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
                *bad_receipts[i] = 0;
            }
        }
        CHECK(!kosdec_adx_compat_exec_server(&service,&result) && backend.stops == (unsigned)armed);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && completed == armed);
        if(armed) {
            CHECK(!kosdec_adx_compat_player_get_state(p,&state) && state == KOSDEC_ADX_COMPAT_PLAYEND);
            CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_SOURCE));
            CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == (empty ? 0 : 65) &&
                  time.frequency == 44100);
        }
        CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 1);
        CHECK(!kosdec_adx_compat_unbind(&service,0));
        free(f->data);
        return;
    }
    backend.credited = frames;
    backend.valid = false;
    CHECK(!kosdec_adx_compat_exec_server(&service,&result) && !backend.stops);
    backend.valid = true;
    backend.reported_epoch = 91;
    CHECK(!kosdec_adx_compat_exec_server(&service,&result) && !backend.stops);
    backend.reported_epoch = 0;

    if(scenario == PLAYEND_EXPLICIT) {
        CHECK(!kosdec_adx_player_stop(p,1000));
    }
    else if(scenario == PLAYEND_CANCEL) {
        CHECK(!kosdec_adx_player_cancel_source(p));
        CHECK(!kosdec_adx_compat_exec_server(&service,&result) && !backend.stops);
        CHECK(!kosdec_adx_player_stop(p,1000));
    }
    else if(scenario == PLAYEND_EMPTY) {
        CHECK(!kosdec_adx_compat_exec_server(&service,&result) && backend.stops == 1);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && completed);
        CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 0 && time.frequency == 44100);
        CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 1);
    }
    else if(scenario == PLAYEND_FIBER) {
        CHECK(!mock_joins);
        CHECK(!kosdec_adx_compat_exec_server(&service,&result) && backend.stops == 1 && mock_joins == 1);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && completed);
        CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 1 && mock_joins == 1);
    }
    else if(scenario == PLAYEND_FATAL_STOP) {
        backend.injected_stop_error = EIO;
        CHECK(kosdec_adx_compat_exec_server(&service,&result)<0 && errno==EIO);
        CHECK(info(f).state == KOSDEC_ADX_PLAYER_STOPPING && backend.stops == 1);
        backend.injected_stop_error = 0;
        CHECK(kosdec_adx_compat_exec_server(&service,&result)<0 && errno==EIO && backend.stops==1);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
        CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 2);
    }
    else {
        CHECK(!kosdec_adx_player_set_time_offset(p,-15));
        backend.stop_fails = true;
        CHECK(kosdec_adx_compat_exec_server(&service,&result)<0 && errno==ETIMEDOUT);
        CHECK(info(f).state == KOSDEC_ADX_PLAYER_STOPPING && backend.stops == 1);
        CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
        CHECK(kosdec_adx_compat_player_get_time(p,&time)<0 && errno==ENODATA && time.count==8000);
        backend.stop_fails = false;
        backend.valid = false;
        if(scenario == PLAYEND_LOST_EPOCH) {
            backend.reported_epoch = 91;
            CHECK(kosdec_adx_compat_exec_server(&service,&result)<0 && errno==ESTALE);
            CHECK(info(f).state == KOSDEC_ADX_PLAYER_STOPPING && backend.stops == 1);
            CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 2);
        }
        else {
            CHECK(!kosdec_adx_compat_exec_server(&service,&result) && backend.stops == 2);
            CHECK(info(f).state == KOSDEC_ADX_PLAYER_STOPPED && backend.stopped);
            CHECK(!kosdec_adx_compat_player_get_state(p,&state) && state == KOSDEC_ADX_COMPAT_PLAYEND);
            CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && completed);
            unsigned reads = clock.reads;
            CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == -2 && time.frequency == 5994);
            CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_SOURCE));
            CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == 50 && time.frequency == 44100);
            CHECK(!kosdec_adx_compat_player_set_time_mode(p,KOSDEC_ADX_TIME_PAL));
            CHECK(!kosdec_adx_compat_player_get_time(p,&time) && time.count == -3 && time.frequency == 5000);
            int64_t cents;
            CHECK(!kosdec_adx_compat_player_get_time_real(p,&cents) && cents == 0);
            CHECK(clock.reads == reads);
            CHECK(!kosdec_adx_compat_exec_server(&service,&result) && backend.stops == 2);
            CHECK(!kosdec_adx_player_stop(p,1000) && backend.stops == 2);
        }
    }
    CHECK(!kosdec_adx_compat_player_get_state(p,&state) && state == KOSDEC_ADX_COMPAT_STOP);
    CHECK(!kosdec_adx_compat_player_is_completed(p,&completed) && !completed);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
}

static void compat_playend(void) {
    compat_playend_case(PLAYEND_RETRY);
    compat_playend_case(PLAYEND_EXPLICIT);
    compat_playend_case(PLAYEND_LOST_EPOCH);
    compat_playend_case(PLAYEND_CANCEL);
    compat_playend_case(PLAYEND_EMPTY);
    compat_playend_case(PLAYEND_REBOUND_EMPTY);
    compat_playend_case(PLAYEND_FINAL_EMPTY);
    compat_playend_case(PLAYEND_FINAL_PCM);
    compat_playend_case(PLAYEND_FATAL_STOP);
    compat_playend_case(PLAYEND_FIBER);
    puts("ADX_COMPAT_PLAYEND_PASS sealed drain, stale/wrong epoch, timeout retry, stop/join, cancel, empty and rebind");
}

static void split_records(unsigned budget) {
    struct retained_fixture t;
    retained_init(&t,true);
    struct fixture *f = &t.f;
    request_frames = 150;
    span_capacity = budget;
    CHECK(!kosdec_adx_player_start(&f->player));
    if(budget < 3) {
        CHECK(until_sound(f) < 0 && info(f).first_error == ENOBUFS);
        CHECK(f->player.producer.pipe.read_cursor == 0 && !backend.fills && !backend.sealed);
    }
    else {
        CHECK(!until_sound(f) && backend.span_count == 3);
        CHECK(backend.spans[0].first_frame == 0 && backend.spans[0].frames == 65);
        CHECK(backend.spans[1].first_frame == 0 && backend.spans[1].frames == 65);
        CHECK(backend.spans[2].first_frame == 0 && backend.spans[2].frames == 20);
        finish_retained(f);
        CHECK(backend.sealed == 1 && backend.admitted[1] == 195);
    }
    CHECK(!kosdec_adx_player_stop(&f->player,10) && backend.stopped);
    free(f->data);
}

static void cancel_rebind(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    CHECK(!kosdec_adx_player_start(&f->player));
    CHECK(!until_sound(f));
    CHECK(backend.admitted[1] == 64 && backend.sealed == 0);
    CHECK(!kosdec_adx_player_cancel_source(&f->player));
    CHECK(!kosdec_adx_player_poll(&f->player) && backend.sealed == 0);
    CHECK(backend.span_count == 1 && backend.spans[0].kind == SND_RETAINED_PCM_TERMINAL_SILENCE);
    reinitialize_source(f,2,44100,false);
    CHECK(!kosdec_adx_player_bind_source(&f->player,&f->stream,NULL));
    finish_retained(f);
    CHECK(backend.admitted[1] == 64 && backend.admitted[2] == 65 && backend.sealed == 2);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    free(f->data);
}

#ifdef MEDIA_ADX_RETAINED_GAIN
static void compat_live_attenuation_cache(unsigned channels) {
    struct retained_fixture t;
    retained_init_channels(&t,false,channels);
    struct fixture *f = &t.f;
    struct kosdec_adx_player *p = &f->player;
    uint64_t now = 0;
    const struct kosdec_adx_compat_config config = {.clock_us=compat_clock,.clock_context=&now};
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_result result;
    CHECK(!kosdec_adx_compat_service_init(&service,&title,1,&config));
    CHECK(!kosdec_adx_compat_bind_phased_player(&service,0,p));
    CHECK(!kosdec_adx_player_start(p));
    for(unsigned pass=0; pass<16 && info(f).state==KOSDEC_ADX_PLAYER_PREFILL; ++pass)
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
    CHECK(info(f).state==KOSDEC_ADX_PLAYER_PLAYING);

    int16_t cached;
    uint64_t request=91;
    CHECK(kosdec_adx_compat_player_set_attenuation(p,-60,&request)<0 && errno==ENOTSUP && request==91);
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-60);
    CHECK(!kosdec_adx_player_get_requested_attenuation(p,&cached) && !cached);

    backend.gain_supported=true;
    const int32_t argument=channels==1 ? -188 : -65537;
    const int16_t expected_cache=channels==1 ? -188 : -1;
    CHECK(!kosdec_adx_compat_player_set_attenuation(p,argument,&request) && request==1);
    CHECK(backend.gain.requested_tl[0]==(channels==1 ? 60 : 255) &&
          backend.gain.requested_tl[1]==(channels==1 ? 0 : 255));
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==expected_cache);
    CHECK(!kosdec_adx_player_get_requested_attenuation(p,&cached) && cached==expected_cache);
    request=91;
    CHECK(kosdec_adx_compat_player_set_attenuation(p,-90,&request)<0 && errno==EBUSY && request==91);
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-90);
    CHECK(!kosdec_adx_player_get_requested_attenuation(p,&cached) && cached==expected_cache);
    if(channels==1) {
        struct kosdec_adx_attenuation_status status;
        CHECK(!kosdec_adx_compat_exec_server(&service,&result));
        CHECK(!kosdec_adx_player_get_attenuation_status(p,1,&status));
        CHECK(status.state==KOSDEC_ADX_ATTENUATION_APPLIED && !status.error &&
              status.admitted==KOSDEC_ADX_ATTENUATION_LEFT &&
              status.applied==KOSDEC_ADX_ATTENUATION_LEFT &&
              status.acknowledged==KOSDEC_ADX_ATTENUATION_LEFT &&
              !status.unknown && !status.rejected);
        CHECK(status.requested_tl[0]==60 && status.acknowledged_tl[0]==60 &&
              !status.requested_tl[1] && !status.acknowledged_tl[1]);
    }
    CHECK(!kosdec_adx_player_stop(p,1000));
    CHECK(!kosdec_adx_compat_player_get_requested_attenuation(p,&cached) && cached==-90);
    CHECK(!kosdec_adx_compat_unbind(&service,0));
    free(f->data);
    printf("ADX_COMPAT_LIVE_ATTENUATION_CACHE_PASS channels=%u full request and independent caches\n",channels);
}

static void legacy_attenuation(void) {
    struct retained_fixture t;
    retained_init(&t,false);
    struct fixture *f = &t.f;
    uint64_t request = 12;
    int16_t cached = -16;
    struct kosdec_adx_attenuation_status status = {.session = 18};
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && !cached);
    CHECK(kosdec_adx_player_set_attenuation(&f->player,-60,&request) < 0 && errno == ENODATA);
    CHECK(request == 12);
    CHECK(!kosdec_adx_player_start(&f->player));
    CHECK(!until_sound(f));
    CHECK(kosdec_adx_player_set_attenuation(&f->player,-60,&request) < 0 && errno == ENOTSUP);
    CHECK(request == 12);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && !cached);

    backend.gain_supported = true;
    CHECK(!kosdec_adx_player_set_attenuation(&f->player,-60,&request) && request == 1);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && cached == -60);
    CHECK(!kosdec_adx_player_get_attenuation_status(&f->player,request,&status));
    CHECK(status.epoch == 90 && status.session == 7 && status.request == request);
    CHECK(status.state == KOSDEC_ADX_ATTENUATION_PENDING && !status.applied);
    CHECK(status.requested_tl[0] == 15 && status.requested_tl[1] == 15);
    CHECK(!kosdec_adx_player_poll(&f->player));
    CHECK(!kosdec_adx_player_get_attenuation_status(&f->player,request,&status));
    CHECK(status.state == KOSDEC_ADX_ATTENUATION_APPLIED && status.applied == 3);
    CHECK(status.acknowledged == 3 && status.acknowledged_tl[0] == 15 && status.acknowledged_tl[1] == 15);

    CHECK(!kosdec_adx_player_set_attenuation(&f->player,INT32_MIN + 17,&request) && request == 2);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && cached == 17);
    CHECK(kosdec_adx_player_set_attenuation(&f->player,-30,&request) < 0 && errno == EBUSY);
    CHECK(request == 2);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && cached == 17);
    backend.gain_retry = true;
    CHECK(!kosdec_adx_player_poll(&f->player));
    CHECK(kosdec_adx_player_get_attenuation_status(&f->player,1,&status) < 0 && errno == ENOENT);
    CHECK(status.request == 1 && status.applied == 3);
    CHECK(!kosdec_adx_player_get_attenuation_status(&f->player,request,&status));
    CHECK(status.state == KOSDEC_ADX_ATTENUATION_PENDING && status.error == EAGAIN);
    CHECK(status.admitted == KOSDEC_ADX_ATTENUATION_LEFT && status.unknown == status.admitted);
    CHECK(status.acknowledged == 3 && !status.applied && status.acknowledged_tl[0] == 15);
    CHECK(status.requested_tl[0] == 255 && status.requested_tl[1] == 255);

    backend.gain_retry = false;
    backend.gain_reject_right = true;
    CHECK(kosdec_adx_player_poll(&f->player) < 0 && errno == EIO);
    CHECK(!kosdec_adx_player_get_attenuation_status(&f->player,request,&status));
    CHECK(status.state == KOSDEC_ADX_ATTENUATION_FAILED && status.error == EIO);
    CHECK(status.applied == KOSDEC_ADX_ATTENUATION_LEFT && status.rejected == KOSDEC_ADX_ATTENUATION_RIGHT);
    CHECK(!status.unknown && status.acknowledged_tl[0] == 255 && status.acknowledged_tl[1] == 15);
    CHECK(!kosdec_adx_player_stop(&f->player,10));
    CHECK(!kosdec_adx_player_get_attenuation_status(&f->player,request,&status) && status.error == EIO);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && cached == 17);
    free(f->data);
}
#endif

int main(void) {
    zero_and_rebind();
    audio_time_range();
    vsync_time(KOSDEC_ADX_VSYNC_NTSC,5994);
    vsync_time(KOSDEC_ADX_VSYNC_PAL,5000);
    live_time_mode();
    compat_decode_tail();
    compat_live_time_qualification();
    compat_vsync_anchor();
    compat_collection_clock();
    compat_routing_excludes_retained();
    compat_initial_retained_pitch();
    compat_initial_attenuation();
#ifdef MEDIA_ADX_RETAINED_START_TL
    compat_prepared_attenuation();
#endif
    compat_playend();
    split_records(16);
    split_records(2);
    cancel_rebind();
#ifdef MEDIA_ADX_RETAINED_GAIN
    compat_live_attenuation_cache(1);
    compat_live_attenuation_cache(2);
    legacy_attenuation();
#endif
    puts("ADX_RETAINED_MOCK_PASS generation, exact spans, seal, backpressure, sample/VSync time and retained stop");
    return 0;
}
