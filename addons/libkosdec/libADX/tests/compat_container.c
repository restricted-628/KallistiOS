#include <kosdec/adx_compat_service.h>
#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <errno.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr, "compat_container:%d: %s\n", __LINE__, #x); exit(1); } } while(0)

enum format { WAVE, PACKED_WAVE, AIFF, AU, SPSD };
enum { RATE = 44100, FRAMES = 180, CHANNELS = 2 };
struct fixture {
    struct kosdec_adx_player player;
    union {
        struct kosdec_adx_wave wave;
        struct kosdec_adx_aiff aiff;
        struct kosdec_adx_au au;
        struct kosdec_adx_spsd spsd;
    } reader;
    alignas(32) int16_t pcm[1024], scratch[257];
    alignas(32) uint8_t stack[16384];
    uint8_t data[64 + FRAMES * 4];
    enum format format;
    unsigned size, payload, calls, filtered, fail_at, fragment, input_limit;
    enum kosdec_adx_input_status input_error;
    pthread_t owner;
    bool phased, file_phase, under_service, wait;
};

static void put(uint8_t *out, uint32_t value, unsigned n, bool be) {
    for(unsigned i = 0; i < n; ++i)
        out[be ? n - 1 - i : i] = (uint8_t)(value >> (8 * i));
}
static int16_t sample(const struct fixture *f, unsigned frame, unsigned ch) {
    int value = f->format == PACKED_WAVE ? (int)(frame + 1) * 47 :
                (int)((frame * 7919u + ch * 650u) % 65536u) - 32768;
    return (int16_t)(value / 2);
}
static void image(struct fixture *f, unsigned frames, bool invalid) {
    bool packed = f->format == PACKED_WAVE;
    unsigned bytes = frames * (packed ? 1 : 4);
    bool be = f->format == AIFF || f->format == AU;
    uint8_t *p = f->data;
    switch(f->format) {
    case WAVE:
    case PACKED_WAVE:
        f->payload = 56;
        memcpy(p, "RIFF", 4);
        put(p + 4, f->payload + bytes - 8, 4, false);
        memcpy(p + 8, "WAVEJUNK", 8);
        put(p + 16, 3, 4, false);
        memcpy(p + 24, "fmt ", 4);
        put(p + 28, 16, 4, false);
        put(p + 32, 1, 2, false);
        put(p + 34, CHANNELS, 2, false);
        put(p + 36, RATE, 4, false);
        put(p + 40, RATE * 4, 4, false);
        put(p + 44, 4, 2, false);
        put(p + 46, packed ? 4 : 16, 2, false);
        memcpy(p + 48, "data", 4);
        put(p + 52, bytes, 4, false);
        break;
    case AIFF:
        f->payload = 56; /* Two offset bytes between SSND and payload. */
        memcpy(p, "FORM", 4);
        put(p + 4, f->payload + bytes - 8, 4, true);
        memcpy(p + 8, "AIFFCOMM", 8);
        put(p + 16, 18, 4, true);
        put(p + 20, CHANNELS, 2, true);
        put(p + 22, frames, 4, true);
        put(p + 26, 16, 2, true);
        put(p + 28, 0x400e, 2, true);
        put(p + 30, (uint32_t)RATE << 16, 4, true);
        memcpy(p + 38, "SSND", 4);
        put(p + 42, bytes + 10, 4, true);
        put(p + 46, 2, 4, true);
        break;
    case AU:
        f->payload = 32;
        memcpy(p, ".snd", 4);
        put(p + 4, f->payload, 4, true);
        put(p + 8, bytes, 4, true);
        put(p + 12, 3, 4, true);
        put(p + 16, RATE, 4, true);
        put(p + 20, CHANNELS, 4, true);
        break;
    case SPSD:
        f->payload = 64;
        memcpy(p, "SPSD", 4);
        p[5] = p[9] = 1;
        p[7] = 4;
        put(p + 12, bytes / CHANNELS, 4, false);
        put(p + 42, RATE, 2, false);
        break;
    }
    f->size = f->payload + bytes;
    if(invalid)
        p[0] = 0;
    if(packed) {
        memset(p + f->payload, 0x11, bytes);
    } else {
        for(unsigned i = 0; i < frames; ++i)
            for(unsigned ch = 0; ch < CHANNELS; ++ch) {
                int value = (int)((i * 7919u + ch * 650u) % 65536u) - 32768;
                put(p + f->payload + 4 * i + ch * 2, (uint16_t)value, 2, be);
            }
    }
}

static struct kosdec_adx_input_result read_at(void *context, uint32_t at, uint8_t *out, size_t n) {
    struct fixture *f = context;
    if(f->under_service) {
        CHECK(f->phased == !!pthread_equal(f->owner, pthread_self()));
        if(f->phased) CHECK(f->file_phase);
    }
    CHECK(n && n <= 512 && at < f->size && n <= f->size - at);
    ++f->calls;
    if(f->input_error && at >= f->fail_at)
        return (struct kosdec_adx_input_result){f->input_error, 0};
    if(f->wait && f->calls % 4 == 1)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT, 0};
    if(f->input_limit) {
        if(at >= f->input_limit)
            return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT, 0};
        if(n > f->input_limit - at) n = f->input_limit - at;
    }
    if(n > f->fragment) n = f->fragment;
    memcpy(out, f->data + at, n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, n};
}
static void filter(void *context, unsigned ch, int16_t *out, size_t frames, unsigned stride) {
    struct fixture *f = context;
    CHECK((!f->phased || !f->file_phase) && stride == CHANNELS && ch < stride && frames <= 32);
    for(size_t i = 0; i < frames; ++i) out[i * stride] /= 2;
    f->filtered += (unsigned)frames;
}
static void init(struct fixture *f, enum format format, bool phased, unsigned frames, bool invalid, size_t capacity) {
    memset(f, 0, sizeof(*f));
    f->format = format;
    f->phased = phased;
    f->owner = pthread_self();
    f->fragment = 15;
    f->wait = true;
    image(f, frames, invalid);
    struct kosdec_adx_player_config config = {
        .pcm = f->pcm, .pcm_samples = 1024, .capacity = capacity, .scratch = f->scratch,
        .scratch_samples = 257, .stack = f->stack, .stack_bytes = sizeof(f->stack),
        .sound_bytes = capacity == 32 ? 64 : 512, .transfer_timeout_ms = 1000, .prefill_timeout_ms = 3000
    };
    switch(format) {
    case WAVE:
    case PACKED_WAVE:
        CHECK(kosdec_adx_wave_init(&f->reader.wave, read_at, f, f->size) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_wave_set_filter(&f->reader.wave, filter, f) == KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_player_init_wave(&f->player, &f->reader.wave, &config));
        break;
    case AIFF:
        CHECK(kosdec_adx_aiff_init(&f->reader.aiff, read_at, f, f->size) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_aiff_set_filter(&f->reader.aiff, filter, f) == KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_player_init_aiff(&f->player, &f->reader.aiff, &config));
        break;
    case AU:
        CHECK(kosdec_adx_au_init(&f->reader.au, read_at, f, f->size) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_au_set_filter(&f->reader.au, filter, f) == KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_player_init_au(&f->player, &f->reader.au, &config));
        break;
    case SPSD:
        CHECK(kosdec_adx_spsd_init(&f->reader.spsd, read_at, f, f->size) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_spsd_set_filter(&f->reader.spsd, filter, f) == KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_player_init_spsd(&f->player, &f->reader.spsd, &config));
        break;
    }
}
static uint64_t now_us(void *context) {
    return *(const uint64_t *)context;
}
static int enter_file(void *context) {
    struct fixture *f = context;
    CHECK(!f->file_phase);
    f->file_phase = f->phased;
    return 0;
}
static int leave_file(void *context) {
    struct fixture *f = context;
    CHECK(f->file_phase == f->phased);
    f->file_phase = false;
    return 0;
}
static struct kosdec_adx_player_info info(struct fixture *f) {
    struct kosdec_adx_player_info i;
    CHECK(!kosdec_adx_player_get_info(&f->player, &i));
    return i;
}
static void bind(struct fixture *f, struct kosdec_adx_compat_service *s,
                 struct kosdec_adx_compat_title *title, uint64_t *now) {
    struct kosdec_adx_compat_config config = {.clock_us = now_us, .clock_context = now};
    config.phases[KOSDEC_ADX_COMPAT_RENDERER] = (struct kosdec_adx_compat_phase_op){enter_file, f};
    config.phases[KOSDEC_ADX_COMPAT_FILES] = (struct kosdec_adx_compat_phase_op){leave_file, f};
    CHECK(!kosdec_adx_compat_service_init(s, title, 1, &config));
    if(f->phased)
        CHECK(!kosdec_adx_compat_bind_phased_player(s, 0, &f->player));
    else
        CHECK(!kosdec_adx_compat_bind_player(s, 0, &f->player));
    CHECK(!kosdec_adx_compat_set_frequency(s, 0, 1000));
    f->under_service = true;
    CHECK(!kosdec_adx_player_start(&f->player));
}
static void drain(struct fixture *f, struct kosdec_adx_compat_service *s,
                  uint64_t *now, unsigned skip, unsigned frames) {
    unsigned block = f->format == PACKED_WAVE ? 4 : 1;
    bool admitted = false, completed = false;
    for(unsigned tick = 0; tick < 2200 && !completed; ++tick) {
        *now += 1000;
        struct kosdec_adx_compat_result r;
        unsigned calls = f->phased ? f->calls : 0;
        CHECK(!kosdec_adx_compat_exec_server(s, &r) && r.visited == 1);
        if(f->phased) CHECK(f->calls - calls <= 16);
        struct kosdec_adx_decode_progress p;
        if(kosdec_adx_player_get_decode_progress(&f->player, &p) < 0) {
            CHECK(errno == EAGAIN && !f->phased);
        } else if(p.sample_rate) {
            CHECK(p.sample_rate == RATE && p.declared_frames == frames &&
                  p.block_frames == block && !p.loop_present && p.input_bytes <= 4096);
            CHECK(p.source_frame == skip + p.decoded_frames);
            admitted = true;
        } else {
            CHECK(!p.block_frames && !p.input_bytes);
        }
        completed = info(f).state == KOSDEC_ADX_PLAYER_SOURCE_END;
        if(!f->phased && !completed) mock_pause();
    }
    CHECK(admitted && completed && f->player.compat_prefill_frames == (block == 4 ? 72 : 68));
    struct kosdec_adx_decode_progress p;
    CHECK(!kosdec_adx_player_get_decode_progress(&f->player, &p));
    CHECK(p.samples_complete && p.status == KOSDEC_ADX_DONE &&
          p.source_frame == frames && p.decoded_frames == frames - skip);
}
static void playback(enum format format, bool phased) {
    struct fixture f;
    uint64_t now = 0;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_service service;
    mock_reset();
    init(&f, format, phased, FRAMES, false, 512);
    CHECK(!f.calls);
    bind(&f, &service, &title, &now);
    drain(&f, &service, &now, 0, FRAMES);
    CHECK(mock_frames == FRAMES && f.filtered == FRAMES * CHANNELS);
    for(unsigned i = 0; i < FRAMES; ++i)
        for(unsigned ch = 0; ch < CHANNELS; ++ch)
            CHECK(mock_pcm[i * CHANNELS + ch] == sample(&f, i, ch));
    if(format == PACKED_WAVE && phased) {
        /* Rebind in the same occupied slot with a seek that discards a
           partial four-frame group and keeps the cached fourth sample. */
        CHECK(!kosdec_adx_player_release_source(&f.player));
        CHECK(kosdec_adx_wave_seek(&f.reader.wave, 3) == KOSDEC_ADX_MORE);
        CHECK(!kosdec_adx_player_bind_wave(&f.player, &f.reader.wave));
        drain(&f, &service, &now, 3, FRAMES);
        CHECK(mock_frames == 2 * FRAMES - 3);
        for(unsigned i = 3; i < FRAMES; ++i)
            for(unsigned ch = 0; ch < CHANNELS; ++ch)
                CHECK(mock_pcm[(FRAMES + i - 3) * CHANNELS + ch] == sample(&f, i, ch));
    }
    CHECK(!kosdec_adx_player_stop(&f.player, 1000));
    CHECK(!kosdec_adx_compat_unbind(&service, 0));
}
static void ready_with_one_block(enum format format) {
    struct fixture f;
    uint64_t now = 0;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_service service;
    mock_reset();
    init(&f, format, true, FRAMES, false, 32);
    unsigned queued = format == PACKED_WAVE ? 28 : 30;
    f.input_limit = f.payload + (format == PACKED_WAVE ? queued : queued * 4);
    CHECK(!kosdec_adx_player_set_wait_start(&f.player, true));
    bind(&f, &service, &title, &now);
    for(unsigned tick = 0; tick < 100; ++tick) {
        struct kosdec_adx_compat_result r;
        now += 1000;
        CHECK(!kosdec_adx_compat_exec_server(&service, &r));
    }
    struct kosdec_adx_decode_progress progress;
    CHECK(!kosdec_adx_player_get_decode_progress(&f.player, &progress));
    CHECK(progress.decoded_frames == queued && !progress.samples_complete);
    CHECK(info(&f).ready_start == (format == PACKED_WAVE));
    CHECK(!mock_starts);
    if(format == WAVE) {
        f.input_limit += 4;
        for(unsigned tick = 0; tick < 20 && !info(&f).ready_start; ++tick) {
            struct kosdec_adx_compat_result r;
            now += 1000;
            CHECK(!kosdec_adx_compat_exec_server(&service, &r));
        }
        CHECK(!kosdec_adx_player_get_decode_progress(&f.player, &progress));
        CHECK(progress.decoded_frames == 31 && info(&f).ready_start && !mock_starts);
    }
    CHECK(!kosdec_adx_player_stop(&f.player, 1000));
    CHECK(!kosdec_adx_compat_unbind(&service, 0));
}

static void faults(enum kosdec_adx_input_status input_error, enum kosdec_adx_status expected, unsigned prefix) {
    struct fixture f;
    uint64_t now = 0;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_service service;
    mock_reset();
    init(&f, SPSD, true, FRAMES, !input_error, 512);
    f.input_error = input_error;
    f.fail_at = f.payload + prefix;
    bind(&f, &service, &title, &now);
    struct kosdec_adx_compat_result r;
    for(unsigned tick = 0; tick < 100; ++tick) {
        now += 1000;
        if(kosdec_adx_compat_exec_server(&service, &r) < 0) break;
    }
    CHECK(r.first_error == EIO && !mock_starts);
    CHECK(kosdec_adx_service_get_state(&f.player.producer).status == expected);
    CHECK(f.filtered == prefix / 4 * CHANNELS);
    enum kosdec_adx_compat_phase phase = input_error && !prefix ?
        KOSDEC_ADX_COMPAT_FILES : KOSDEC_ADX_COMPAT_DECODER;
    CHECK(r.failed_phases == (UINT32_C(1) << phase));
    uint32_t flags;
    CHECK(!kosdec_adx_compat_get_faults(&service, 0, &flags));
    CHECK(flags == (input_error == KOSDEC_ADX_INPUT_ERROR ? KOSDEC_ADX_COMPAT_FAULT_TRANSPORT : 0));
    CHECK(!kosdec_adx_player_stop(&f.player, 1000));
    CHECK(!kosdec_adx_compat_unbind(&service, 0));
}
enum { WAVE_RATE = 48000, FRAME_BYTES = 4, SOURCE_FRAMES = 48000, MEASURED_TICKS = 36 };
static alignas(32) uint8_t data[44 + SOURCE_FRAMES * FRAME_BYTES], stack[16384];
static alignas(32) int16_t pcm[8192], scratch[257], drained_pcm[1920];
static uint64_t cadence_now;
static unsigned cadence_fragment;
static unsigned transport_calls, transport_bytes;

static struct kosdec_adx_input_result cadence_read(void *context, uint32_t at, uint8_t *out, size_t n) {
    (void)context;
    CHECK(n && n <= 512 && at < sizeof(data) && n <= sizeof(data) - at);
    if(n > cadence_fragment) n = cadence_fragment;
    memcpy(out,data + at,n);
    ++transport_calls;
    transport_bytes += (unsigned)n;
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static struct kosdec_adx_player_config config = {
    .pcm = pcm, .pcm_samples = 8192, .capacity = 4096,
    .scratch = scratch, .scratch_samples = 257, .stack = stack, .stack_bytes = sizeof(stack),
    .sound_bytes = 512, .transfer_timeout_ms = 1000, .prefill_timeout_ms = 1000
};

static void cadence_test(unsigned hz, unsigned fragment) {
    struct kosdec_adx_player player;
    struct kosdec_adx_wave wave;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_service owner;
    struct kosdec_adx_compat_config clock = {.clock_us = now_us, .clock_context = &cadence_now};
    struct kosdec_adx_compat_result pass;
    unsigned need = WAVE_RATE / hz;
    cadence_now = transport_calls = transport_bytes = 0;
    cadence_fragment = fragment;
    mock_reset();
    CHECK(kosdec_adx_wave_init(&wave,cadence_read,NULL,sizeof(data)) == KOSDEC_ADX_MORE);
    CHECK(!kosdec_adx_player_init_wave(&player,&wave,&config));
    CHECK(!kosdec_adx_compat_service_init(&owner,&title,1,&clock));
    CHECK(!kosdec_adx_compat_bind_phased_player(&owner,0,&player));
    CHECK(!kosdec_adx_compat_set_frequency(&owner,0,(int32_t)hz));
    CHECK(!kosdec_adx_player_set_wait_start(&player,true));
    CHECK(!kosdec_adx_player_start(&player));
    for(unsigned tick = 0; tick < 64; ++tick) {
        cadence_now += 1000000 / hz;
        CHECK(!kosdec_adx_compat_exec_vsync(&owner,&pass));
        if(player.info.ready_start) break;
    }
    CHECK(player.info.ready_start && !mock_allocs && !mock_starts);

    /* The model is the sole consumer. The player's start gate keeps Audio off. */
    for(unsigned tick = 0; tick < 8; ++tick) {
        cadence_now += 1000000 / hz;
        CHECK(!kosdec_adx_compat_exec_vsync(&owner,&pass));
        CHECK(kosdec_adx_pipe_read(&player.producer.pipe,drained_pcm,need).producer == KOSDEC_ADX_MORE);
    }
    struct kosdec_adx_decode_progress pre;
    CHECK(!kosdec_adx_player_get_decode_progress(&player,&pre));
    unsigned initial_bytes = transport_bytes, initial_calls = transport_calls;
    unsigned moved = 0;
    for(unsigned tick = 0; tick < MEASURED_TICKS; ++tick) {
        unsigned calls = transport_calls;
        cadence_now += 1000000 / hz;
        CHECK(!kosdec_adx_compat_exec_vsync(&owner,&pass));
        CHECK(transport_calls - calls <= 16);
        moved += (unsigned)kosdec_adx_pipe_read(&player.producer.pipe,drained_pcm,need).frames;
    }
    struct kosdec_adx_decode_progress after;
    CHECK(!kosdec_adx_player_get_decode_progress(&player,&after));
    printf("VBlank=%u rate=%u required=%u bytes/visit observed=%u bytes/visit "
           "provider_calls=%u decoded=%u consumed=%u demand=%u\n",hz,WAVE_RATE,need * FRAME_BYTES,
           (transport_bytes - initial_bytes) / MEASURED_TICKS,transport_calls - initial_calls,
           (unsigned)(after.decoded_frames - pre.decoded_frames),moved,MEASURED_TICKS * need);
    CHECK(after.decoded_frames - pre.decoded_frames + 4096 / FRAME_BYTES >= moved);
    CHECK(moved == MEASURED_TICKS * need);
    CHECK(!kosdec_adx_player_stop(&player,1000));
    CHECK(!kosdec_adx_compat_unbind(&owner,0));
}
static void cadence(void) {
    memcpy(data,"RIFF",4);
    put(data + 4,sizeof(data) - 8,4,false);
    memcpy(data + 8,"WAVEfmt ",8);
    put(data + 16,16,4,false);
    put(data + 20,1,2,false);
    put(data + 22,CHANNELS,2, false);
    put(data + 24,WAVE_RATE,4,false);
    put(data + 28,WAVE_RATE * FRAME_BYTES,4,false);
    put(data + 32,FRAME_BYTES,2,false);
    put(data + 34,16,2,false);
    memcpy(data + 36,"data",4);
    put(data + 40,SOURCE_FRAMES * FRAME_BYTES,4,false);
    cadence_test(60, 512);
    cadence_test(50, 256);
}

int main(void) {
    for(enum format format = WAVE; format <= SPSD; ++format) {
        playback(format, false);
        playback(format, true);
    }
    ready_with_one_block(WAVE);
    ready_with_one_block(PACKED_WAVE);
    faults(0, KOSDEC_ADX_UNSUPPORTED, 0);
    faults(KOSDEC_ADX_INPUT_END, KOSDEC_ADX_TRUNCATED, 15);
    faults(KOSDEC_ADX_INPUT_ERROR, KOSDEC_ADX_IO_ERROR, 15);
    faults(KOSDEC_ADX_INPUT_ERROR, KOSDEC_ADX_IO_ERROR, 0);
    cadence();
    puts("compat containers: 10 playback cases, packed seek/rebind, PREP room, 4 faults and 50/60 Hz cadence passed");
    return 0;
}
