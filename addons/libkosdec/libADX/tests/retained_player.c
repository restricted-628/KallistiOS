/* Exercise the adapter with Audio's exact private interface and no device. */
#define main adx_legacy_mock_tests
#include "player.c"
#undef main
#include "snd_retained_pcm.h"
#ifdef MEDIA_ADX_RETAINED_GAIN
#include "snd_retained_gain.h"
#endif

struct snd_retained_pcm {
    snd_retained_pcm_fill_t fill;
    void *context;
    unsigned channels;
    bool in_fill, stopped, backpressure, no_refill, stop_fails, valid;
    unsigned starts, fills, stops;
    uint64_t admitted[17], sealed, credited;
    snd_retained_pcm_span_t spans[SND_RETAINED_PCM_FILL_SPANS];
    unsigned span_count;
    int16_t pcm[2 * 1024];
#ifdef MEDIA_ADX_RETAINED_GAIN
    bool gain_supported, gain_retry, gain_reject_right;
    snd_retained_gain_status_t gain;
#endif
};
static snd_retained_pcm_t backend;
static uint32_t request_frames;
static unsigned span_capacity;
static bool require_zero_pcm;

int snd_retained_pcm_open(snd_retained_pcm_t **out, unsigned channels, uint32_t rate,
    uint32_t ring_frames, snd_retained_pcm_fill_t fill, void *context) {
    CHECK(channels == 2 && rate == 44100 && ring_frames == 1024 && fill && context);
    CHECK(!backend.starts);
    backend.fill = fill;
    backend.context = context;
    backend.channels = channels;
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
#ifdef MEDIA_ADX_RETAINED_GAIN
static int service_gain(snd_retained_pcm_t *pcm) {
    snd_retained_gain_status_t *gain = &pcm->gain;
    if(!gain->request || gain->state != SND_RETAINED_GAIN_PENDING) return 0;
    if(pcm->gain_retry) {
        gain->admitted = gain->unknown = SND_RETAINED_GAIN_LEFT;
        gain->error = EAGAIN;
        return 0;
    }
    gain->admitted = SND_RETAINED_GAIN_LEFT | SND_RETAINED_GAIN_RIGHT;
    gain->applied = SND_RETAINED_GAIN_LEFT;
    gain->acknowledged |= SND_RETAINED_GAIN_LEFT;
    gain->acknowledged_tl[0] = gain->requested_tl[0];
    gain->unknown = 0;
    if(pcm->gain_reject_right) {
        gain->state = SND_RETAINED_GAIN_FAILED;
        gain->rejected = SND_RETAINED_GAIN_RIGHT;
        errno = gain->error = EIO;
        return -1;
    }
    gain->state = SND_RETAINED_GAIN_APPLIED;
    gain->applied = gain->acknowledged = gain->admitted;
    gain->acknowledged_tl[1] = gain->requested_tl[1];
    gain->error = 0;
    return 0;
}

int snd_retained_pcm_gain_begin(snd_retained_pcm_t *pcm, unsigned left_tl,
    unsigned right_tl, uint64_t *request) {
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
        .requested_tl = {(uint8_t)left_tl,(uint8_t)right_tl},
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
    if(generation > 16 || (generation > pcm->sealed && !pcm->admitted[generation])) {
        errno = ENOENT;
        return -1;
    }
    *out = (snd_retained_pcm_snapshot_t){
        .epoch = 90, .session = 7, .generation = generation,
        .output_frames = pcm->credited + 18, .source_frames = pcm->credited,
        .next_source_frame = 19, .valid = pcm->valid && !pcm->stopped,
        .source_output_end = generation && generation <= pcm->sealed &&
                             pcm->credited == pcm->admitted[generation],
        .clock_error = pcm->stopped ? ECANCELED : pcm->valid ? 0 : ESTALE,
        .software_stopped = pcm->stopped
    };
    return 0;
}
int snd_retained_pcm_stop(snd_retained_pcm_t *pcm, uint32_t timeout_ms) {
    CHECK(pcm == &backend && timeout_ms && pcm->starts == 1);
    ++pcm->stops;
    if(pcm->stop_fails) { errno = ETIMEDOUT; return -1; }
    pcm->stopped = true;
    return 0;
}
int snd_retained_pcm_close(snd_retained_pcm_t *pcm) {
    (void)pcm;
    CHECK(false);
    return -1;
}

struct retained_fixture {
    struct fixture f;
    uint32_t offsets[2048];
    alignas(32) int16_t pcm[4096], scratch[1025];
};
static void retained_init(struct retained_fixture *t, bool sequence) {
    memset(&backend,0,sizeof(backend));
    request_frames = 64;
    span_capacity = SND_RETAINED_PCM_FILL_SPANS;
    require_zero_pcm = false;
    struct fixture *f = &t->f;
    init(f,2,sequence);
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

    CHECK(!kosdec_adx_player_set_attenuation(&f->player,INT32_MIN,&request) && request == 2);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && !cached);
    CHECK(kosdec_adx_player_set_attenuation(&f->player,-30,&request) < 0 && errno == EBUSY);
    CHECK(request == 2);
    CHECK(!kosdec_adx_player_get_requested_attenuation(&f->player,&cached) && !cached);
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
    free(f->data);
}
#endif

int main(void) {
    zero_and_rebind();
    audio_time_range();
    vsync_time(KOSDEC_ADX_VSYNC_NTSC,5994);
    vsync_time(KOSDEC_ADX_VSYNC_PAL,5000);
    live_time_mode();
    split_records(16);
    split_records(2);
    cancel_rebind();
#ifdef MEDIA_ADX_RETAINED_GAIN
    legacy_attenuation();
#endif
    puts("ADX_RETAINED_MOCK_PASS generation, exact spans, seal, backpressure, sample/VSync time and retained stop");
    return 0;
}
