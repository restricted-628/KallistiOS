/* Original application-owned PCM stream adapter; shared Audio stays in KOS. */
#include <kosdec/adx_player.h>
#include <kosdec/adx_compat_service.h>
#include "adx_loader_internal.h"
#include "adx_recovery_internal.h"
#include "adx_service_internal.h"
#include "adx_stream_internal.h"
#include "adx_container_internal.h"
#ifdef MEDIA_ADX_RETAINED_CLOCK
#include "snd_retained_pcm.h"
#endif
#ifdef MEDIA_ADX_ORDINARY_STEREO
#include "snd_ordinary_pcm16.h"
static int ordinary_mono_snapshot(const struct kosdec_adx_player *, uint64_t,
    struct kosdec_adx_source_output *);
#endif
#ifdef MEDIA_ADX_FLYCAST_PAUSE
#include "snd_flycast_pcm16_pause.h"
#include <stdlib.h>
enum { FLYCAST_PREROLL = SND_FLYCAST_PCM16_PAUSE_PREROLL };
struct kosdec_adx_flycast_pause {
    uint64_t elapsed, source_first, output_first, written, real_end, cut;
    uint64_t epoch;
    uint32_t ring, preroll, discarded, pending_vsync_count, resume_rate;
    bool pending, exhausted;
    int16_t *staged;
    int16_t history[];
};
static int flycast_snapshot(const struct kosdec_adx_player *, uint64_t,
    struct kosdec_adx_source_output *);
static int flycast_player_pause(struct kosdec_adx_player *, bool);
static int flycast_discard(struct kosdec_adx_player *, int32_t, int32_t *);
#endif
#ifdef MEDIA_ADX_RETAINED_GAIN
#include "snd_retained_gain.h"
#endif
#ifdef MEDIA_ADX_RETAINED_START_TL
#include "snd_retained_start_tl.h"
#endif
#if defined(MEDIA_ADX_RETAINED_GAIN) || defined(MEDIA_ADX_RETAINED_START_TL)
#include "adx_legacy_gain_table.h"
static uint8_t legacy_gain_tl(int32_t requested);
#endif
#include "adx_pitch.h"
#include <kos/timer.h>
#include <errno.h>
#include <string.h>
#define PLAYER_MAGIC UINT32_C(0x41445841)
static bool valid(const struct kosdec_adx_player *p) {
    return p && p->magic == PLAYER_MAGIC;
}
static int bad(void) { errno = EINVAL; return -1; }
static int unsupported(void) { errno = ENOTSUP; return -1; }
static int busy(void) { errno = EBUSY; return -1; }
static int stop_player(struct kosdec_adx_player *, uint32_t timeout_ms);
static bool compat_unrecognized_header(const struct kosdec_adx_player *);
static bool has_output(const struct kosdec_adx_player *p) {
    return p->retained_pcm || p->ordinary_pcm ||
        (p->sound != SND_STREAM_INVALID && !p->ordinary_reprime_pending);
}
static bool start_held(const struct kosdec_adx_player *p) {
    return p->compat_paused || (p->info.wait_start && !p->compat_preoutput_playing);
}

static void released_start_hold(struct kosdec_adx_player *p, bool was_held) {
    if(p->compat_service && p->info.state == KOSDEC_ADX_PLAYER_PREFILL &&
       p->info.ready_start && was_held && !start_held(p))
        p->prefill_deadline = timer_ms_gettime64() + p->memory.prefill_timeout_ms;
}

static void report_error(struct kosdec_adx_player *p, int error) {
    if(p->diagnostic.code) return;
    p->diagnostic = (struct kosdec_adx_player_error){
        .code = error ? error : EIO,
        .decoder = p->source_bound ? kosdec_adx_service_get_state(&p->producer).status : p->info.decoder,
        .state = p->info.state
    };
    p->error_pending = true;
}
static int failed(struct kosdec_adx_player *p, int error) {
    report_error(p,error);
    if(!p->info.first_error) p->info.first_error = error ? error : EIO;
    p->info.state = KOSDEC_ADX_PLAYER_FAILED;
    p->info.ready_start = false;
    p->compat_initial_tl = 0;
    errno = error ? error : EIO;
    return -1;
}
/* Representatives of the existing KOS pan encoding, not amplitude math. */
static uint8_t output_pan(int pan, unsigned channel, unsigned channels) {
    if(pan == KOSDEC_ADX_PAN_AUTO)
        return channel ? 255 : channels == 2 ? 0 : 128;
    return (uint8_t)(pan < 0 ? 127 + pan * 8 : 128 + pan * 8);
}
static int signed_pan(unsigned pan) {
    return pan < 128 ? -(int)((127 - pan) / 8) : (int)((pan - 128) / 8);
}
static int wake_producer(struct kosdec_adx_player *p) {
    if(p->compat_phased || kosdec_adx_service_get_state(&p->producer).status >= KOSDEC_ADX_DONE)
        return 0;
    if(fiber_service_wake(p->worker) == 0) return 0;
    /* Natural decoder exit may race this wake while published PCM or a closed
       command receipt remains available. Executor FINISHED rejects wake. */
    return kosdec_adx_service_get_state(&p->producer).status >= KOSDEC_ADX_DONE ? 0 : -1;
}
static int init(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    struct kosdec_adx_joint_reader *joint, struct kosdec_adx_wave *wave, struct kosdec_adx_au *au, struct kosdec_adx_aiff *aiff, struct kosdec_adx_spsd *spsd,
    const struct kosdec_adx_player_config *c) {
    if(!p || !c || !c->scratch || (uintptr_t)c->scratch % _Alignof(int16_t) ||
       c->sound_bytes < 64 || c->sound_bytes > SND_STREAM_BUFFER_MAX_PCM16 ||
       c->sound_bytes % 64 || c->scratch_samples < c->sound_bytes / 2 + 1 ||
       c->capacity < c->sound_bytes / 2 || !c->stack ||
       (uintptr_t)c->stack % 32 || c->stack_bytes < 16384 || c->stack_bytes % 32 ||
       c->stack_bytes > UINTPTR_MAX - (uintptr_t)c->stack ||
       !c->transfer_timeout_ms || !c->prefill_timeout_ms) return bad();
    struct kosdec_adx_player initial = {.magic = PLAYER_MAGIC, .memory = *c,
        .source_bound = true, .requested_pan = {KOSDEC_ADX_PAN_AUTO,KOSDEC_ADX_PAN_AUTO},
        .compat_controls = {.effect_db = -45},
        .linked_reload_sectors = MEDIA_ADX_RELOAD_DEFAULT,
        .sound = SND_STREAM_INVALID, .info = {.retained_sound = SND_STREAM_INVALID}};
    enum kosdec_adx_status status = spsd ?
        kosdec_adx_service_init_spsd(&initial.producer,spsd,c->pcm,c->pcm_samples,c->capacity) : aiff ?
        kosdec_adx_service_init_aiff(&initial.producer,aiff,c->pcm,c->pcm_samples,c->capacity) : au ?
        kosdec_adx_service_init_au(&initial.producer,au,c->pcm,c->pcm_samples,c->capacity) : wave ?
        kosdec_adx_service_init_wave(&initial.producer,wave,c->pcm,c->pcm_samples,c->capacity) : joint ?
        kosdec_adx_service_init_joint(&initial.producer,joint,c->pcm,c->pcm_samples,c->capacity) :
        kosdec_adx_service_init(&initial.producer,stream,sequence,c->pcm,c->pcm_samples,c->capacity);
    if(status != KOSDEC_ADX_MORE) return bad();
    if(snd_stream_config_init(&initial.output) < 0) return -1;
    initial.output.transfer_timeout_ms = c->transfer_timeout_ms;
    /* Demonstrated aligned splitter bug: select existing exact split once at
       binding. Source/output samples are never permuted by this adapter. */
    initial.scratch = c->scratch + ((uintptr_t)c->scratch % 32 == 0);
    *p = initial;
    return 0;
}
int kosdec_adx_player_init(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    const struct kosdec_adx_player_config *c) {
    return init(p,stream,sequence,NULL,NULL,NULL,NULL,NULL,c);
}
int kosdec_adx_player_init_joint(struct kosdec_adx_player *p,
    struct kosdec_adx_joint_reader *joint, const struct kosdec_adx_player_config *c) {
    if(!joint) return bad();
    return init(p,NULL,NULL,joint,NULL,NULL,NULL,NULL,c);
}
int kosdec_adx_player_init_wave(struct kosdec_adx_player *p,
    struct kosdec_adx_wave *wave, const struct kosdec_adx_player_config *c) {
    if(!wave) return bad();
    return init(p,NULL,NULL,NULL,wave,NULL,NULL,NULL,c);
}
int kosdec_adx_player_init_au(struct kosdec_adx_player *p,
    struct kosdec_adx_au *au, const struct kosdec_adx_player_config *c) {
    if(!au) return bad();
    return init(p,NULL,NULL,NULL,NULL,au,NULL,NULL,c);
}
int kosdec_adx_player_init_aiff(struct kosdec_adx_player *p,
    struct kosdec_adx_aiff *aiff, const struct kosdec_adx_player_config *c) {
    if(!aiff) return bad();
    return init(p,NULL,NULL,NULL,NULL,NULL,aiff,NULL,c);
}
int kosdec_adx_player_init_spsd(struct kosdec_adx_player *p,
    struct kosdec_adx_spsd *spsd, const struct kosdec_adx_player_config *c) {
    if(!spsd) return bad();
    return init(p,NULL,NULL,NULL,NULL,NULL,NULL,spsd,c);
}
/* Partial parse steps can consume input and still ask for more. Container
   readers return NEED_INPUT only on a provider wait. */
static uint64_t input_progress(const struct kosdec_adx_pipe *pipe) {
    if(pipe->stream) return pipe->stream->byte_position;
    if(pipe->joint) return pipe->joint->consumed_bytes;
    if(pipe->sequence) return pipe->sequence->reader.byte_position;
    return 0;
}

static void decode(fiber_service_t *self, void *data) {
    struct kosdec_adx_player *p = data;
    const bool compat = p->compat_service != NULL;
    const bool detect_stalls = !compat && p->memory.input_stall_timeout_ms != 0;
    uint32_t generation = 0;
    for(;;) {
        if(fiber_service_stop_requested(self))
            kosdec_adx_service_request_cancel(&p->producer);
        uint64_t before = detect_stalls ? input_progress(&p->producer.pipe) : 0;
        uint32_t limit = compat ?
            __atomic_load_n(&p->compat_prefill_frames,__ATOMIC_ACQUIRE) : 0;
        struct kosdec_adx_stream_result r =
            media_adx_service_step_limited(&p->producer,limit,compat && !limit,false);
        bool paused = kosdec_adx_service_get_state(&p->producer).paused;
        if(detect_stalls) {
            bool waiting = !paused && r.status == KOSDEC_ADX_NEED_INPUT && !r.frames &&
                           before == input_progress(&p->producer.pipe);
            if(waiting && p->producer.pipe.joint && p->producer.pipe.joint->linked.loader) {
                const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
                uint32_t head = __atomic_load_n(&pipe->write_cursor, __ATOMIC_RELAXED);
                uint32_t tail = __atomic_load_n(&pipe->read_cursor, __ATOMIC_ACQUIRE);
                /* A prefetch retry while output is blocked is not starvation. */
                uint32_t room = pipe->capacity - (uint32_t)(head - tail);
                waiting = room >= (pipe->joint->job.pcm ? 32u : 1u);
            }
            if(waiting != (bool)(generation & 1u)) {
                ++generation;
                __atomic_store_n(&p->input_wait_generation,generation,__ATOMIC_RELEASE);
            }
        }
        if(r.status >= KOSDEC_ADX_DONE) return;
        int rc;
        if(paused || r.status == KOSDEC_ADX_NEED_OUTPUT)
            rc = fiber_service_wait(self,0);
        else if(r.status == KOSDEC_ADX_NEED_INPUT || r.status == KOSDEC_ADX_NEED_END)
            rc = fiber_service_wait(self,timer_ms_gettime64() + 1);
        else rc = fiber_service_yield(self);
        if(rc < 0) kosdec_adx_service_request_cancel(&p->producer);
    }
}
static int start_source(struct kosdec_adx_player *p) {
    if(p->compat_service) {
        media_adx_compat_reset_title_error(&p->compat_service->titles[p->compat_slot]);
        const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
        p->compat_prefill_frames = 0;
        p->compat_block_frames = 0;
        p->compat_renderer_ready = false;
        p->compat_ready_start = false;
        p->compat_decode_ended = false;
        p->compat_renderer_failed_playing = false;
        p->compat_preoutput_playing = false;
        p->compat_discarded_all = false;
        p->compat_initial_tl = 0;
        if(p->compat_phased && (pipe->stream || pipe->sequence))
            p->producer.input_capacity = sizeof(pipe->stream->input);
        p->compat_chained = pipe->sequence || (pipe->stream && pipe->stream->linked) ||
                            (pipe->joint && pipe->joint->linked.enabled);
    }
#ifdef MEDIA_ADX_RETAINED_CLOCK
    p->vsync_generation = 0;
    if(p->retained_offsets) {
        ++p->info.source_generation;
        p->retained_sealed = false;
        p->producer.pipe.source_offsets = p->retained_offsets;
    }
#endif
    p->info.ready_start = false;
    p->info.input_stalled = false;
    p->input_wait_generation = 0;
    p->input_stall_generation = 0;
    /* No concurrent producer yet; apply/collect the initial resume without I/O. */
    struct kosdec_adx_command resume = {.kind = KOSDEC_ADX_PRODUCTION_PAUSE};
    enum kosdec_adx_status receipt;
    if(kosdec_adx_service_post(&p->producer,&resume) != KOSDEC_ADX_MORE)
        return failed(p,EINVAL);
    kosdec_adx_service_step(&p->producer);
    if(!kosdec_adx_service_take(&p->producer,&receipt) || receipt != KOSDEC_ADX_MORE)
        return failed(p,EINVAL);
    if(!p->compat_phased) {
        p->executor = fiber_service_executor_create();
        if(!p->executor) return failed(p,errno);
        p->worker = fiber_service_add(p->executor,p->memory.stack,p->memory.stack_bytes,decode,p);
        if(!p->worker || fiber_service_executor_start(p->executor,NULL) < 0)
            return failed(p,errno);
    }
    p->info.state = KOSDEC_ADX_PLAYER_PREFILL;
    p->prefill_deadline = timer_ms_gettime64() + p->memory.prefill_timeout_ms;
    if(wake_producer(p) < 0) return failed(p,errno);
    return 0;
}
static int start_linked_loader(const struct kosdec_adx_player *p,
                               struct kosdec_adx_joint_reader *joint) {
    if(!p->memory.use_lsc_reload || !joint || !joint->linked.loader) return 0;
    enum kosdec_adx_loader_status status = media_adx_loader_start_linked(
        joint->linked.loader,p->linked_reload_sectors,p->linked_reload_clamp);
    if(status == KOSDEC_ADX_LOADER_OK) return 0;
    if(status == KOSDEC_ADX_LOADER_BUSY) {
        errno = EBUSY;
        return -1;
    }
    return bad();
}
int kosdec_adx_player_start(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound) return bad();
    if(p->compat_service && p->compat_service->servicing) return busy();
    if(start_linked_loader(p,p->producer.pipe.joint) < 0) return -1;
    return start_source(p);
}
int kosdec_adx_player_set_linked_reload_sectors(struct kosdec_adx_player *p, uint32_t sectors) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING || sectors > INT16_MAX)
        return bad();
    if(!p->memory.use_lsc_reload) return unsupported();
    p->linked_reload_sectors = sectors;
    p->linked_reload_clamp = false;
    return 0;
}
int kosdec_adx_player_set_linked_reload_time(struct kosdec_adx_player *p,
    float seconds, unsigned channels, unsigned rate) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) return bad();
    if(!p->memory.use_lsc_reload) return unsupported();
    uint32_t sectors;
    if(!media_adx_reload_time_sectors(seconds,channels,rate,INT16_MAX,&sectors)) return bad();
    p->linked_reload_sectors = sectors;
    p->linked_reload_clamp = true;
    return 0;
}
int kosdec_adx_player_get_input(const struct kosdec_adx_player *p,
    struct kosdec_adx_player_input *input) {
    if(!valid(p) || !input) return bad();
    *input = (struct kosdec_adx_player_input){0};
    const struct kosdec_adx_service *s = &p->producer;
    if(!p->source_bound || p->info.state == KOSDEC_ADX_PLAYER_READY ||
       p->info.state == KOSDEC_ADX_PLAYER_STOPPED || !s->input_capacity ||
       media_adx_pipe_has_container(&s->pipe)) return 0;

    const uint32_t sector = KOSDEC_ADX_REQUEST_SECTOR_BYTES;
    uint32_t data = __atomic_load_n(&s->input_data,__ATOMIC_ACQUIRE);
    uint32_t bytes = data & ~MEDIA_ADX_INPUT_FORMAT_READY;
    uint32_t limit = p->linked_reload_sectors;
    uint32_t capacity = s->input_capacity / sector;
    if(limit == MEDIA_ADX_RELOAD_DEFAULT)
        limit = media_adx_default_reload_sectors(s->input_capacity);
    else if(p->linked_reload_clamp && limit > capacity)
        limit = capacity;
    *input = (struct kosdec_adx_player_input){
        .joint = s->pipe.joint ? s->pipe.joint->joint : NULL,
        .queued_bytes = bytes, .sectors = bytes / sector,
        .safety_limit_bytes = limit * sector, .safe = bytes >= limit * sector
    };
    uint32_t rate;
    unsigned channels;
    if((data & MEDIA_ADX_INPUT_FORMAT_READY) &&
       kosdec_adx_pipe_get_format(&s->pipe,&rate,&channels))
        input->seconds = media_adx_input_seconds(bytes,channels,rate);
    return 0;
}
int kosdec_adx_player_enable_decode_progress(struct kosdec_adx_player *p) {
    if(!valid(p) || (p->info.state != KOSDEC_ADX_PLAYER_READY &&
                     p->info.state != KOSDEC_ADX_PLAYER_IDLE))
        return bad();
    if(p->source_bound && kosdec_adx_service_enable_decode_progress(&p->producer) < 0)
        return -1;
    p->decode_progress = true;
    return 0;
}
int kosdec_adx_player_get_decode_progress(const struct kosdec_adx_player *p,
                                         struct kosdec_adx_decode_progress *out) {
    if(!valid(p) || !out) return bad();
    if(!p->source_bound || p->info.state == KOSDEC_ADX_PLAYER_STOPPED) {
        errno = ENODATA;
        return -1;
    }
    return kosdec_adx_service_get_decode_progress(&p->producer,out);
}
int kosdec_adx_player_enable_retained_clock(struct kosdec_adx_player *p,
    uint32_t *offsets, size_t count) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_READY ||
       !offsets || (uintptr_t)offsets % _Alignof(uint32_t) ||
       count < p->memory.capacity || p->memory.sound_bytes < 2048 ||
       p->memory.sound_bytes > 65536)
        return bad();
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->compat_route_fields || p->ordinary_stereo || p->ordinary_mono_clock)
        return unsupported();
    if(p->output.channel[0].volume != 255 || p->output.channel[1].volume != 255 ||
       p->requested_pan[0] != KOSDEC_ADX_PAN_AUTO ||
       p->requested_pan[1] != KOSDEC_ADX_PAN_AUTO)
        return bad();
    p->retained_offsets = offsets;
    return 0;
#else
    return unsupported();
#endif
}
int kosdec_adx_player_get_source_output(const struct kosdec_adx_player *p,
    uint64_t generation, struct kosdec_adx_source_output *out) {
    if(!valid(p) || !out) return bad();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause_enabled) return flycast_snapshot(p,generation,out);
#endif
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(p->ordinary_mono_clock) return ordinary_mono_snapshot(p,generation,out);
#endif
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(!p->retained_offsets) return unsupported();
    if(!p->retained_pcm) { errno = ENODATA; return -1; }
    snd_retained_pcm_snapshot_t status;
    if(snd_retained_pcm_query(p->retained_pcm,generation,&status) < 0) return -1;
    *out = (struct kosdec_adx_source_output){
        .epoch = status.epoch, .generation = status.generation,
        .output_frames = status.output_frames, .source_frames = status.source_frames,
        .next_source_frame = status.next_source_frame, .session = status.session,
        .observation_start_ms = status.observation_start_ms,
        .observation_end_ms = status.observation_end_ms,
        .clock_error = status.clock_error, .output_error = status.first_error,
        .stop_error = status.stop_error, .valid = status.valid != 0,
        .source_output_end = status.valid && status.source_output_end,
        .software_stopped = status.software_stopped != 0
    };
    return 0;
#else
    (void)generation;
    return unsupported();
#endif
}
int kosdec_adx_player_set_time_offset(struct kosdec_adx_player *p, int32_t offset) {
    if(!valid(p)) return bad();
    p->time_offset = offset;
    return 0;
}

static int display_frames(uint64_t count, int32_t bias, int64_t *frames) {
    int64_t offset = bias;
    if(offset < 0) {
        /* Apply the offset before narrowing, including counts above INT64_MAX. */
        uint64_t magnitude = (uint64_t)-offset;
        uint64_t removed = count < magnitude ? count : magnitude;
        count -= removed;
        offset += (int64_t)removed;
    }
    if(count > INT64_MAX || __builtin_add_overflow((int64_t)count,offset,frames)) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static int qualified_clock(const struct kosdec_adx_player *p, uint64_t generation,
    struct kosdec_adx_source_output *clock) {
    if(kosdec_adx_player_get_source_output(p,generation,clock) < 0) return -1;
    if(!clock->valid || !p->info.sample_rate) {
        errno = clock->clock_error ? clock->clock_error : ENODATA;
        return -1;
    }
    return 0;
}

static int sample_time(const struct kosdec_adx_player *p, uint64_t generation,
    struct kosdec_adx_time *out) {
    struct kosdec_adx_source_output clock;
    if(qualified_clock(p,generation,&clock) < 0) return -1;
    int64_t frames;
    if(display_frames(clock.source_frames,p->time_offset,&frames) < 0) return -1;
    *out = (struct kosdec_adx_time){.count = frames, .frequency = p->info.sample_rate};
    return 0;
}

static int time_centiseconds(const struct kosdec_adx_time *time, int64_t *out) {
    int64_t count = time->count, rate = time->frequency;
    int64_t centiseconds;
    if(__builtin_mul_overflow(count / rate,100,&centiseconds) ||
       __builtin_add_overflow(centiseconds,(count % rate) * 100 / rate,&centiseconds)) {
        errno = EOVERFLOW;
        return -1;
    }
    *out = centiseconds;
    return 0;
}

int kosdec_adx_player_get_audio_time(const struct kosdec_adx_player *p,
    uint64_t generation, struct kosdec_adx_audio_time *out) {
    if(!out || !generation) return bad();
    struct kosdec_adx_time time;
    int64_t centiseconds;
    if(sample_time(p,generation,&time) < 0 || time_centiseconds(&time,&centiseconds) < 0)
        return -1;
    *out = (struct kosdec_adx_audio_time){
        .frames = time.count, .centiseconds = centiseconds, .sample_rate = time.frequency
    };
    return 0;
}

int kosdec_adx_player_enable_vsync_clock(struct kosdec_adx_player *p,
    enum kosdec_adx_vsync_mode mode, kosdec_adx_vsync_ticks ticks, void *context) {
    if(!valid(p) || !ticks || p->info.state != KOSDEC_ADX_PLAYER_READY ||
       p->info.source_generation ||
       (mode != KOSDEC_ADX_VSYNC_NTSC && mode != KOSDEC_ADX_VSYNC_PAL))
        return bad();
    if(!p->retained_offsets) return unsupported();
    p->vsync_ticks = ticks;
    p->vsync_context = context;
    p->vsync_frequency = mode == KOSDEC_ADX_VSYNC_NTSC ? 5994 : 5000;
    return 0;
}

int kosdec_adx_player_get_vsync_time(struct kosdec_adx_player *p,
    uint64_t generation, struct kosdec_adx_vsync_time *out) {
    if(!valid(p) || !generation || !out) return bad();
    if(!p->vsync_ticks) return unsupported();
    if(generation != p->info.source_generation) { errno = ENOENT; return -1; }
    if(p->vsync_generation != generation || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) {
        errno = ENODATA;
        return -1;
    }
    struct kosdec_adx_source_output clock;
    if(qualified_clock(p,generation,&clock) < 0) return -1;
    if(clock.source_output_end) return unsupported();

    int64_t frames;
    if(display_frames(clock.source_frames,p->time_offset,&frames) < 0) return -1;
    uint32_t tick = p->vsync_ticks(p->vsync_context);
    uint32_t delta = tick - p->vsync_last_tick;
    int64_t estimate, count;
    if(__builtin_add_overflow(p->vsync_count,(int64_t)delta * 100,&estimate) ||
       __builtin_add_overflow(estimate,p->time_offset,&count)) {
        errno = EOVERFLOW;
        return -1;
    }
    float rate = (float)p->info.sample_rate;
    float frequency = (float)p->vsync_frequency;
    float drift_ms = ((float)frames / rate - (float)estimate / frequency) * 1000.0f;
    int64_t next = estimate;
    if(drift_ms > 60.0f || drift_ms < -60.0f) {
        float corrected = ((float)clock.source_frames / rate) * frequency;
        if(corrected >= 0x1p63f) { errno = EOVERFLOW; return -1; }
        next = (int64_t)corrected;
    }
    p->vsync_count = next;
    p->vsync_last_tick = tick;
    *out = (struct kosdec_adx_vsync_time){.count = count, .frequency = p->vsync_frequency};
    return 0;
}

static bool valid_time_mode(enum kosdec_adx_time_mode mode) {
    return (unsigned)mode <= KOSDEC_ADX_TIME_PAL;
}

static void select_time_mode(struct kosdec_adx_player *p, enum kosdec_adx_time_mode mode) {
    p->time_uses_vsync = mode != KOSDEC_ADX_TIME_SOURCE;
    if(p->time_uses_vsync)
        p->vsync_frequency = mode == KOSDEC_ADX_TIME_NTSC ? 5994 : 5000;
}

int kosdec_adx_player_set_time_mode(struct kosdec_adx_player *p,
    enum kosdec_adx_time_mode mode) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING || !valid_time_mode(mode))
        return bad();
    if(mode != KOSDEC_ADX_TIME_SOURCE && !p->vsync_ticks) return unsupported();
    select_time_mode(p,mode);
    return 0;
}

int kosdec_adx_player_get_time(struct kosdec_adx_player *p, uint64_t generation,
    struct kosdec_adx_time *out) {
    if(!valid(p) || !generation || !out) return bad();
    if(!p->time_uses_vsync) return sample_time(p,generation,out);
    struct kosdec_adx_vsync_time video;
    if(kosdec_adx_player_get_vsync_time(p,generation,&video) < 0) return -1;
    *out = (struct kosdec_adx_time){.count = video.count, .frequency = video.frequency};
    return 0;
}

int kosdec_adx_player_get_time_real(struct kosdec_adx_player *p, uint64_t generation,
    int64_t *centiseconds) {
    if(!centiseconds) return bad();
    struct kosdec_adx_time time;
    if(kosdec_adx_player_get_time(p,generation,&time) < 0) return -1;
    return time_centiseconds(&time,centiseconds);
}

int kosdec_adx_compat_player_set_time_mode(struct kosdec_adx_player *p,
    enum kosdec_adx_time_mode mode) {
    if(!valid(p) || !valid_time_mode(mode) ||
       (p->info.state >= KOSDEC_ADX_PLAYER_RELEASING && p->info.state != KOSDEC_ADX_PLAYER_STOPPED))
        return bad();
    if(!p->compat_service || (p->flycast_pause_enabled && mode != KOSDEC_ADX_TIME_SOURCE &&
                              !p->compat_service->use_vsync_clock))
        return unsupported();
    select_time_mode(p,mode);
    return 0;
}

static int32_t compat_signed_count(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value :
        (int32_t)((int64_t)value - INT64_C(4294967296));
}

static int32_t compat_truncate(float value) {
    /* SH-4 FTRC with invalid-operation traps masked returns the signed
       endpoint on overflow. Clamp before converting to keep C defined. */
    if(value >= 0x1p31f) return INT32_MAX;
    if(value <= -0x1p31f) return INT32_MIN;
    return (int32_t)value;
}

static bool compat_collection_vsync(const struct kosdec_adx_player *p) {
    return p->compat_service && p->compat_service->use_vsync_clock;
}

static bool compat_single_title(const struct kosdec_adx_player *p) {
    const struct kosdec_adx_compat_service *service = p->compat_service;
    for(unsigned slot = 0; slot < service->capacity; ++slot)
        if(slot != p->compat_slot && service->titles[slot].ops.state)
            return false;
    return true;
}

#ifdef MEDIA_ADX_ORDINARY_STEREO
static int ordinary_source_position(const struct kosdec_adx_player *p,
    const snd_ordinary_pcm16_status_t *status, uint64_t *frames) {
    const uint32_t half = p->memory.sound_bytes / 4;
    if(status->committed_frames != p->info.requested_frames ||
       status->frames[0] >= status->committed_frames ||
       status->committed_frames - status->frames[0] > 2u * half) {
        errno = EPROTO;
        return -1;
    }
    unsigned index = (unsigned)(status->frames[0] / half) % 2;
    uint32_t within = status->frames[0] % half;
    uint32_t available = p->ordinary_halves[index].source_frames;
    *frames = p->ordinary_halves[index].source_first +
        (within < available ? within : available);
    return 0;
}

static int ordinary_source_frames(const struct kosdec_adx_player *p, uint64_t *frames) {
    snd_ordinary_pcm16_status_t status;
    if(snd_ordinary_pcm16_status(p->ordinary_pcm,&status) < 0) return -1;
    if(status.error) { errno = status.error; return -1; }
    return ordinary_source_position(p,&status,frames);
}

static int ordinary_mono_snapshot(const struct kosdec_adx_player *p,
    uint64_t generation, struct kosdec_adx_source_output *out) {
    if(generation > 1) { errno = ENOENT; return -1; }
    if(!p->ordinary_pcm) { errno = ENODATA; return -1; }
    snd_ordinary_pcm16_status_t status;
    if(snd_ordinary_pcm16_status(p->ordinary_pcm,&status) < 0) return -1;
    if(status.frames[1]) { errno = EPROTO; return -1; }
    bool stopped = p->info.state == KOSDEC_ADX_PLAYER_STOPPED;
    bool live = p->info.state == KOSDEC_ADX_PLAYER_PLAYING ||
                p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END;
    uint64_t source = 0;
    if(live && !status.error && ordinary_source_position(p,&status,&source) < 0)
        return -1;
    *out = (struct kosdec_adx_source_output){
        .epoch = 1, .generation = generation, .output_frames = status.frames[0],
        .source_frames = source, .next_source_frame = source,
        .clock_error = status.error, .output_error = status.error,
        .stop_error = p->info.stop_error, .valid = live && !status.error,
        .source_output_end = !status.error && p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END &&
            source == p->info.source_frames,
        .software_stopped = stopped
    };
    return 0;
}
#endif

static int compat_live_time(struct kosdec_adx_player *p, struct kosdec_adx_time *out) {
    bool collection_clock = compat_collection_vsync(p);
    if(p->time_uses_vsync && !collection_clock && !p->vsync_ticks)
        return unsupported();
    uint32_t frames;
    bool at_end = false;
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(p->ordinary_pcm && !p->flycast_pause_enabled) {
        uint64_t played;
        if(ordinary_source_frames(p,&played) < 0) return -1;
        frames = (uint32_t)played;
    } else
#endif
    {
        if(!p->info.source_generation) return unsupported();
        struct kosdec_adx_source_output clock;
        if(qualified_clock(p,p->info.source_generation,&clock) < 0) return -1;
        frames = (uint32_t)clock.source_frames;
        at_end = clock.source_output_end;
    }
    uint32_t bias = (uint32_t)p->time_offset;
    uint32_t rate = p->info.sample_rate;
    if(!p->time_uses_vsync) {
        *out = (struct kosdec_adx_time){.count = compat_signed_count(frames + bias), .frequency = rate};
        return 0;
    }
    if(at_end) return unsupported();

    uint32_t tick = collection_clock ? p->compat_service->vsync_count : p->vsync_ticks(p->vsync_context);
    uint32_t estimate = (uint32_t)p->compat_vsync_anchor;
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause && p->flycast_pause->pending) {
        *out = (struct kosdec_adx_time){
            .count = compat_signed_count(p->flycast_pause->pending_vsync_count + bias),
            .frequency = p->vsync_frequency
        };
        return 0;
    }
    if(!p->flycast_pause || !p->compat_paused)
#endif
        estimate += (tick - p->compat_vsync_anchor_tick) * 100u;
    float frequency = (float)p->vsync_frequency;
    float sample_seconds = (float)compat_signed_count(frames + bias) / (float)rate;
    float vsync_seconds = (float)compat_signed_count(estimate) / frequency;
    float drift_ms = (sample_seconds - vsync_seconds) * 1000.0f;
    if(drift_ms > 60.0f || drift_ms < -60.0f) {
        float raw_seconds = (float)compat_signed_count(frames) / (float)rate;
        p->compat_vsync_anchor = compat_truncate(raw_seconds * frequency);
        p->compat_vsync_anchor_tick = tick;
    }
    *out = (struct kosdec_adx_time){
        .count = compat_signed_count(estimate + bias), .frequency = p->vsync_frequency
    };
    return 0;
}

int kosdec_adx_compat_player_get_time(struct kosdec_adx_player *p,
    struct kosdec_adx_time *out) {
    if(!valid(p) || !out) return bad();
    if(!p->compat_service) return unsupported();
    if(p->compat_playend.completed) {
        const struct kosdec_adx_playend *end = &p->compat_playend;
        uint32_t frequency = p->time_uses_vsync ? p->vsync_frequency : end->rate;
        uint32_t count = end->frames;
        if(p->time_uses_vsync) {
            float seconds = (float)compat_signed_count(count) / (float)end->rate;
            count = (uint32_t)compat_truncate(seconds * (float)frequency) +
                (uint32_t)end->anchor + 1u;
        }
        *out = (struct kosdec_adx_time){
            .count = compat_signed_count(count + (uint32_t)p->time_offset),
            .frequency = frequency
        };
        return 0;
    }
    switch(p->info.state) {
    case KOSDEC_ADX_PLAYER_FAILED:
        if(!compat_unrecognized_header(p))
            break;
        /* fall through */
    case KOSDEC_ADX_PLAYER_READY:
    case KOSDEC_ADX_PLAYER_PREFILL:
    case KOSDEC_ADX_PLAYER_STOPPED: {
        uint32_t rate = p->info.state == KOSDEC_ADX_PLAYER_PREFILL && p->compat_preoutput_playing ?
            p->info.sample_rate : 1;
        *out = (struct kosdec_adx_time){
            .count = p->time_offset, .frequency = p->time_uses_vsync ? p->vsync_frequency : rate
        };
        return 0;
    }
    case KOSDEC_ADX_PLAYER_PLAYING:
        return compat_live_time(p,out);
    case KOSDEC_ADX_PLAYER_SOURCE_END:
        if(!p->compat_discarded_all)
            return compat_live_time(p,out);
        *out = (struct kosdec_adx_time){
            .count = p->time_offset,
            .frequency = p->time_uses_vsync ? p->vsync_frequency : p->info.sample_rate
        };
        return 0;
    default:
        break;
    }
    errno = ENODATA;
    return -1;
}

int kosdec_adx_compat_player_get_time_real(struct kosdec_adx_player *p, int64_t *centiseconds) {
    if(!centiseconds) return bad();
    struct kosdec_adx_time time;
    if(kosdec_adx_compat_player_get_time(p,&time) < 0) return -1;
    float seconds = (float)time.count / (float)time.frequency;
    *centiseconds = compat_truncate(seconds * 100.0f);
    return 0;
}

int kosdec_adx_player_set_wait_start(struct kosdec_adx_player *p, bool wait) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) return bad();
    bool was_held = start_held(p);
    p->info.wait_start = wait;
    released_start_hold(p,was_held);
    return 0;
}

int kosdec_adx_compat_player_pause(struct kosdec_adx_player *p, bool paused) {
    if(!valid(p)) return bad();
    if(!p->compat_service) return unsupported();
    if(p->compat_service->servicing) return busy();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause_enabled && p->ordinary_pcm && p->info.state != KOSDEC_ADX_PLAYER_STOPPED)
        return flycast_player_pause(p,paused);
#endif
    if(p->info.state != KOSDEC_ADX_PLAYER_STOPPED &&
       (p->info.state > KOSDEC_ADX_PLAYER_PREFILL || has_output(p))) return unsupported();
    bool was_held = start_held(p);
    p->compat_paused = paused;
    released_start_hold(p,was_held);
    return 0;
}

int kosdec_adx_compat_player_discard_samples(struct kosdec_adx_player *p,
    int32_t requested, int32_t *discarded) {
    if(!valid(p) || !discarded) return bad();
    if(!p->compat_service) return unsupported();
    if(p->compat_service->servicing) return busy();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause && (p->compat_paused || p->flycast_pause->pending))
        return flycast_discard(p,requested,discarded);
#endif
    uint32_t removed = 0;
    if(p->compat_paused) {
        if(!p->source_bound || p->info.state != KOSDEC_ADX_PLAYER_PREFILL ||
           has_output(p) || p->info.copied_frames || !compat_single_title(p))
            return unsupported();
        struct kosdec_adx_pipe *pipe = &p->producer.pipe;
        const struct kosdec_adx_stream *stream = pipe->stream;
        if(requested > 0 && (!p->compat_prefill_frames || !stream || stream->linked))
            return unsupported();
        uint32_t read = 0, write = 0;
        if(requested >= 16) {
            if(p->retained_offsets || kosdec_adx_service_get_state(&p->producer).status != KOSDEC_ADX_DONE)
                return unsupported();
            /* PCM16 requests round down to 32 bytes per channel. After DONE
               the producer needs no freed space during this service pass. */
            removed = (uint32_t)requested & ~UINT32_C(15);
            read = __atomic_load_n(&pipe->read_cursor,__ATOMIC_RELAXED);
            write = __atomic_load_n(&pipe->write_cursor,__ATOMIC_ACQUIRE);
            if(removed > write-read) removed = write-read;
        }
        struct kosdec_adx_compat_result result;
        if(kosdec_adx_compat_exec_server(p->compat_service,&result) < 0)
            return -1;
        if(p->info.state != KOSDEC_ADX_PLAYER_PREFILL || has_output(p))
            return unsupported();
        if(requested >= 16) {
            if(!p->compat_paused || !p->source_bound || pipe->stream != stream ||
               !media_adx_pipe_discard_done(pipe,read,write,removed))
                return busy();
            p->compat_discarded_all = removed == write-read;
        }

        /* No frames have reached a renderer. Re-anchor to raw position zero
           only after the ordinary service pass, which delivers no VBlank. */
        p->compat_vsync_anchor = 0;
        p->compat_vsync_anchor_tick = compat_collection_vsync(p) || !p->vsync_ticks ?
            p->compat_service->vsync_count : p->vsync_ticks(p->vsync_context);
    }
    *discarded = (int32_t)removed;
    return 0;
}

int kosdec_adx_player_is_ready_start(const struct kosdec_adx_player *p, bool *ready) {
    if(!valid(p) || !ready) return bad();
    *ready = p->info.ready_start;
    return 0;
}
static bool in_phased_dispatch(const struct kosdec_adx_player *p) {
    return p->compat_phased && p->compat_service->servicing;
}

static bool phased_source(const struct kosdec_adx_pipe *pipe) {
    const struct kosdec_adx_joint_reader *joint = pipe->joint;
    return pipe->stream || pipe->sequence || media_adx_pipe_has_container(pipe) ||
           (joint && joint->job.pcm && !joint->feed &&
           ((joint->joint->kind == KOSDEC_ADX_JOINT_MEMORY && joint->joint->sealed) ||
            joint->sector.request.read_at || (joint->linked.enabled && joint->linked.loader)));
}

static void cancel_producer(struct kosdec_adx_player *p) {
    kosdec_adx_service_request_cancel(&p->producer);
    if(p->compat_phased)
        (void)kosdec_adx_service_step(&p->producer);
}

int kosdec_adx_player_release_source(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END) return bad();
    if(in_phased_dispatch(p)) return busy();
    if(p->ordinary_pcm) return unsupported();
    p->compat_playend = (struct kosdec_adx_playend){0};
    /* No poll/callback can race this serialized owner call. Future callbacks
       cannot touch the retired producer, even if join needs a retry. */
    p->pcm_enabled = false;
    p->info.ready_start = false;
    if(p->executor && fiber_service_executor_destroy(p->executor) < 0) {
        report_error(p,errno);
        return -1;
    }
    p->executor = NULL; p->worker = NULL;
    p->source_bound = false;
    p->info.decoder = KOSDEC_ADX_DONE;
    p->info.state = KOSDEC_ADX_PLAYER_IDLE;
    return 0;
}
int kosdec_adx_player_cancel_source(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    if(in_phased_dispatch(p)) return busy();
    if(p->ordinary_reprime_pending) return busy();
    if(p->ordinary_pcm) return unsupported();
    if(!p->source_bound) return 0;
    p->compat_playend = (struct kosdec_adx_playend){0};
    p->pcm_enabled = false;
    p->info.ready_start = false;
    p->info.state = KOSDEC_ADX_PLAYER_RELEASING;
    p->compat_initial_tl = 0;
    p->vsync_generation = 0;
    cancel_producer(p);
    /* Executor destruction requests stop/wakes and joins its carrier. A failed
       join retains all source users and may be retried while output polls zero. */
    if(p->executor && fiber_service_executor_destroy(p->executor) < 0) {
        report_error(p,errno);
        return -1;
    }
    p->executor = NULL; p->worker = NULL;
    enum kosdec_adx_status status = kosdec_adx_service_get_state(&p->producer).status;
    p->source_bound = false;
    p->info.decoder = status < KOSDEC_ADX_DONE ? KOSDEC_ADX_CANCELLED : status;
    if(status >= KOSDEC_ADX_INVALID && status != KOSDEC_ADX_CANCELLED) return failed(p,EIO);
    if(!has_output(p)) {
        p->compat_start_pitch = false;
        p->compat_start_rate = 0;
    }
    p->info.state = has_output(p) ? KOSDEC_ADX_PLAYER_IDLE : KOSDEC_ADX_PLAYER_READY;
    return 0;
}
static int bind_source(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    struct kosdec_adx_joint_reader *joint, struct kosdec_adx_wave *wave, struct kosdec_adx_au *au, struct kosdec_adx_aiff *aiff, struct kosdec_adx_spsd *spsd) {
    if(!valid(p) || p->source_bound ||
       (p->info.state != KOSDEC_ADX_PLAYER_IDLE && p->info.state != KOSDEC_ADX_PLAYER_READY)) return bad();
    if(p->flycast_pause_enabled || p->ordinary_mono_clock ||
       (p->ordinary_stereo && (!stream || stream->linked)))
        return unsupported();
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_offsets && (p->retained_admissions >= SND_RETAINED_PCM_GENERATIONS ||
                              p->info.source_generation == UINT64_MAX)) {
        errno = EOVERFLOW;
        return -1;
    }
#endif
    if(p->compat_service && p->compat_service->servicing) return busy();
    struct kosdec_adx_service producer;
    enum kosdec_adx_status status = spsd ?
        kosdec_adx_service_init_spsd(&producer,spsd,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : aiff ?
        kosdec_adx_service_init_aiff(&producer,aiff,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : au ?
        kosdec_adx_service_init_au(&producer,au,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : wave ?
        kosdec_adx_service_init_wave(&producer,wave,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : joint ?
        kosdec_adx_service_init_joint(&producer,joint,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) :
        kosdec_adx_service_init(&producer,stream,sequence,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity);
    if(p->compat_phased && (status != KOSDEC_ADX_MORE || !phased_source(&producer.pipe)))
        return unsupported();
    if(status != KOSDEC_ADX_MORE) return bad();
    if((p->decode_progress || p->compat_service) &&
       kosdec_adx_service_enable_decode_progress(&producer) < 0)
        return -1;
    if(start_linked_loader(p,joint) < 0) return -1;
    p->producer = producer;
    p->source_bound = true;
    p->info.source_frames = 0;
    return start_source(p);
}
int kosdec_adx_player_bind_source(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence) {
    return bind_source(p,stream,sequence,NULL,NULL,NULL,NULL,NULL);
}
static bool compat_reprime_port(const struct kosdec_adx_player *p) {
    return p->compat_service && !p->compat_phased && !p->retained_offsets &&
        p->info.sources_started && p->sound != SND_STREAM_INVALID &&
        p->info.channels == 2 && !p->requested_rate && compat_single_title(p);
}

int kosdec_adx_compat_player_reprime(struct kosdec_adx_player *p, struct kosdec_adx_stream *stream) {
    if(!valid(p) || !stream) return bad();
    if(p->info.state != KOSDEC_ADX_PLAYER_STOPPED) return busy();
    if(!compat_reprime_port(p)) return unsupported();
    if(p->compat_service->servicing) return busy();
    if(stream->linked || stream->prepared || stream->seeking || stream->terminal ||
       stream->staged_error || stream->byte_position || stream->input_count || stream->returned_frames)
        return bad();

    struct kosdec_adx_service producer;
    if(kosdec_adx_service_init(&producer,stream,NULL,p->memory.pcm,p->memory.pcm_samples,
                               p->memory.capacity) != KOSDEC_ADX_MORE)
        return bad();
    if(kosdec_adx_service_enable_decode_progress(&producer) < 0)
        return -1;

    p->producer = producer;
    p->source_bound = true;
    p->ordinary_reprime_pending = true;
    p->callback_failed = false;
    p->pcm_enabled = false;
    p->info.retained_sound = SND_STREAM_INVALID;
    p->info.source_frames = 0;
    return start_source(p);
}
int kosdec_adx_player_bind_joint(struct kosdec_adx_player *p,
    struct kosdec_adx_joint_reader *joint) {
    if(!joint) return bad();
    return bind_source(p,NULL,NULL,joint,NULL,NULL,NULL,NULL);
}
int kosdec_adx_player_bind_wave(struct kosdec_adx_player *p, struct kosdec_adx_wave *wave) {
    if(!wave) return bad();
    return bind_source(p,NULL,NULL,NULL,wave,NULL,NULL,NULL);
}
int kosdec_adx_player_bind_au(struct kosdec_adx_player *p, struct kosdec_adx_au *au) {
    if(!au) return bad();
    return bind_source(p,NULL,NULL,NULL,NULL,au,NULL,NULL);
}
int kosdec_adx_player_bind_aiff(struct kosdec_adx_player *p, struct kosdec_adx_aiff *aiff) {
    if(!aiff) return bad();
    return bind_source(p,NULL,NULL,NULL,NULL,NULL,aiff,NULL);
}
int kosdec_adx_player_bind_spsd(struct kosdec_adx_player *p, struct kosdec_adx_spsd *spsd) {
    if(!spsd) return bad();
    return bind_source(p,NULL,NULL,NULL,NULL,NULL,NULL,spsd);
}
static void *refill(snd_stream_hnd_t handle, int requested, int *received) {
    struct kosdec_adx_player *p = snd_stream_get_userdata(handle);
    *received = 0;
    if(!p) return NULL; /* Disconnected after checked stop. */
    unsigned channels = p->output.channels;
    if(requested < 0 || (size_t)requested > p->memory.sound_bytes ||
       (unsigned)requested % (channels * 2)) {
        p->callback_failed = true;
        if(!p->info.first_error) p->info.first_error = EOVERFLOW;
        report_error(p,EOVERFLOW);
        return NULL;
    }
    ++p->info.callbacks;
    p->info.requested_frames += (unsigned)requested / (channels * 2);
    if(!p->pcm_enabled) return NULL;
    struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(
        &p->producer.pipe,p->scratch,(unsigned)requested / (channels * 2));
    p->info.copied_frames += r.frames;
    p->info.source_frames += r.frames;
    if(r.starved) ++p->info.starvations;
    if(r.producer >= KOSDEC_ADX_INVALID) {
        p->callback_failed = true;
        if(!p->info.first_error) p->info.first_error = EIO;
        report_error(p,EIO);
    }
    *received = (int)(r.frames * channels * 2);
    if(r.frames && wake_producer(p) < 0) {
        p->callback_failed = true;
        if(!p->info.first_error) p->info.first_error = errno;
        report_error(p,errno);
    }
    return r.frames ? p->scratch : NULL;
}
#ifdef MEDIA_ADX_RETAINED_CLOCK
static int retained_fill(void *context, int16_t *pcm, uint32_t frames,
    snd_retained_pcm_span_t *spans, unsigned capacity) {
    struct kosdec_adx_player *p = context;
    if(!frames || frames > p->memory.sound_bytes / 2) { errno = EOVERFLOW; return -1; }
    struct kosdec_adx_pipe *pipe = &p->producer.pipe;
    uint32_t copied = 0;
    unsigned count = 0;
    enum snd_retained_pcm_kind silence = SND_RETAINED_PCM_TERMINAL_SILENCE;
    if(p->pcm_enabled) {
        struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(pipe,NULL,0);
        if(r.producer >= KOSDEC_ADX_INVALID) { errno = EIO; return -1; }
        copied = r.queued_frames < frames ? (uint32_t)r.queued_frames : frames;
        if(UINT64_MAX - p->info.source_frames < copied) { errno = EOVERFLOW; return -1; }
        if(r.producer == KOSDEC_ADX_MORE)
            silence = kosdec_adx_service_get_state(&p->producer).paused ?
                SND_RETAINED_PCM_PAUSE_SILENCE : SND_RETAINED_PCM_STARVATION;
        /* The status acquire published the offsets. Only this caller advances
           read_cursor, so this prefix stays intact until the subsequent copy. */
        uint32_t tail = __atomic_load_n(&pipe->read_cursor,__ATOMIC_RELAXED);
        uint32_t mask = pipe->capacity - 1;
        for(uint32_t i = 0; i < copied; ++i) {
            uint64_t offset = pipe->source_offsets[(tail + i) & mask];
            if(count && offset == spans[count - 1].first_frame + spans[count - 1].frames) {
                ++spans[count - 1].frames;
                continue;
            }
            if(count == capacity) { errno = ENOBUFS; return -1; }
            spans[count++] = (snd_retained_pcm_span_t){
                p->info.source_generation, offset, 1, SND_RETAINED_PCM_SOURCE
            };
        }
    }
    else if(p->source_bound && p->info.state == KOSDEC_ADX_PLAYER_PREFILL)
        silence = p->info.wait_start ? SND_RETAINED_PCM_PAUSE_SILENCE : SND_RETAINED_PCM_STARVATION;
    if(copied < frames) {
        if(count == capacity) { errno = ENOBUFS; return -1; }
        spans[count++] = (snd_retained_pcm_span_t){0, 0, frames - copied, silence};
    }
    if(copied) {
        struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(pipe,pcm,copied);
        if(r.frames != copied || r.producer >= KOSDEC_ADX_INVALID) {
            errno = EIO;
            return -1;
        }
        if(wake_producer(p) < 0) return -1;
    }
    ++p->info.callbacks;
    p->info.requested_frames += frames;
    p->info.copied_frames += copied;
    p->info.source_frames += copied;
    if(copied < frames && silence == SND_RETAINED_PCM_STARVATION)
        ++p->info.starvations;
    return (int)count;
}
#endif

#ifdef MEDIA_ADX_ORDINARY_STEREO
static int ordinary_pcm16_fill(void *context, int16_t *pcm, uint32_t frames) {
    struct kosdec_adx_player *p = context;
    if(frames != p->memory.sound_bytes / 4 || !p->pcm_enabled) {
        errno = EPROTO;
        return -1;
    }
    struct kosdec_adx_pipe_result read = kosdec_adx_pipe_read(&p->producer.pipe,pcm,frames);
    if(read.producer >= KOSDEC_ADX_INVALID) {
        errno = EIO;
        return -1;
    }
    if(read.frames && wake_producer(p) < 0) return -1;
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause) {
        struct kosdec_adx_flycast_pause *f = p->flycast_pause;
        if(read.starved) { errno = EPIPE; return -1; }
        uint32_t index = (uint32_t)(f->written % f->ring);
        unsigned channels = p->info.channels;
        memcpy(f->history + channels*index,pcm,channels*read.frames*sizeof(*pcm));
        if(read.frames) f->real_end = f->written + read.frames;
        f->written += frames;
    }
#endif

    /* The callback stages one metadata half. Audio publishes the matching
       committed frontier after both DMA legs. A failed poll blocks time. */
    unsigned index = (unsigned)(p->info.requested_frames / frames) % 2;
    p->ordinary_halves[index].source_first = p->info.source_frames;
    p->ordinary_halves[index].source_frames = (uint32_t)read.frames;
    ++p->info.callbacks;
    p->info.requested_frames += frames;
    p->info.copied_frames += read.frames;
    p->info.source_frames += read.frames;
    if(read.starved) ++p->info.starvations;
    return (int)read.frames;
}
#endif

#ifdef MEDIA_ADX_FLYCAST_PAUSE
static int flycast_snapshot(const struct kosdec_adx_player *p, uint64_t generation,
    struct kosdec_adx_source_output *out) {
    if(generation > 1) { errno = ENOENT; return -1; }
    snd_ordinary_pcm16_status_t status = {0};
    if(p->ordinary_pcm && snd_ordinary_pcm16_status(p->ordinary_pcm,&status) < 0)
        return -1;
    if(p->info.state == KOSDEC_ADX_PLAYER_STOPPED) {
        *out = (struct kosdec_adx_source_output){.generation = generation,
            .epoch = p->flycast_stopped_epoch, .software_stopped = true,
            .output_error = status.error, .stop_error = p->info.stop_error};
        return 0;
    }
    const struct kosdec_adx_flycast_pause *f = p->flycast_pause;
    if(!f || p->flycast_pause_unstarted) { errno = ENODATA; return -1; }
    if(!status.error && status.committed_frames != f->written) { errno = EPROTO; return -1; }

    /* Running time uses the ordinary slot-zero sample clock. Only Pause
       success establishes a common cut. While waiting, keep prior credit. */
    uint64_t local = p->compat_paused || f->pending || f->exhausted ? f->cut : status.frames[0];
    uint64_t limit = local < f->real_end ? local : f->real_end;
    const unsigned lead = f->preroll;
    const uint64_t played = limit > lead ? limit - lead : 0;
    *out = (struct kosdec_adx_source_output){
        .epoch = f->epoch, .generation = generation,
        .output_frames = f->output_first + local, .source_frames = f->elapsed + played,
        .next_source_frame = f->source_first + played + f->discarded,
        .clock_error = status.error, .output_error = status.error,
        .stop_error = p->info.stop_error,
        .valid = !status.error && (p->info.state == KOSDEC_ADX_PLAYER_PLAYING ||
                                   p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END),
        .source_output_end = !status.error && !f->pending && !p->compat_paused &&
            p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END &&
            (f->exhausted || (status.frames[0] >= f->real_end &&
                              (p->info.channels == 1 || status.frames[1] >= f->real_end)))
    };
    return 0;
}

static int flycast_reanchor(struct kosdec_adx_player *p, bool include_tick) {
    if(!p->time_uses_vsync) return 0;
    struct kosdec_adx_source_output clock;
    if(qualified_clock(p,p->info.source_generation,&clock) < 0) return failed(p,errno);
    float seconds = (float)compat_signed_count((uint32_t)clock.source_frames) /
        (float)p->info.sample_rate;
    p->compat_vsync_anchor = compat_truncate(seconds * (float)p->vsync_frequency);
    if(include_tick) p->compat_vsync_anchor_tick = p->compat_service->vsync_count;
    return 0;
}

static uint32_t flycast_available(const struct kosdec_adx_pipe *pipe, uint32_t *tail) {
    *tail = __atomic_load_n(&pipe->read_cursor,__ATOMIC_RELAXED);
    uint32_t front = __atomic_load_n(&pipe->write_cursor,__ATOMIC_ACQUIRE);
    return front - *tail;
}

static void flycast_copy_pcm(int16_t *out, const int16_t *ring, uint32_t capacity,
    uint64_t at, uint32_t frames, unsigned channels) {
    uint32_t index = (uint32_t)(at % capacity);
    uint32_t first = capacity-index;
    if(first > frames) first = frames;
    memcpy(out,ring+channels*index,channels*first*sizeof(*out));
    if(first < frames)
        memcpy(out+channels*first,ring,channels*(frames-first)*sizeof(*out));
}

static int flycast_resume(struct kosdec_adx_player *p) {
    struct kosdec_adx_flycast_pause *f = p->flycast_pause;
    struct kosdec_adx_pipe *pipe = &p->producer.pipe;
    uint64_t survivor = f->cut + f->discarded;
    uint32_t captured = survivor < f->real_end ? (uint32_t)(f->real_end-survivor) : 0;
    const uint32_t rate = f->resume_rate ? f->resume_rate : p->info.output_rate;
    const uint32_t preroll = snd_flycast_pcm16_resume_preroll(rate);
    if(preroll < FLYCAST_PREROLL || preroll >= f->ring) return failed(p,EPROTO);
    const uint32_t prefill = f->ring-preroll;
    if(captured > prefill) return failed(p,EPROTO);
    uint32_t continuation = prefill-captured, tail;
    struct kosdec_adx_pipe_result head = kosdec_adx_pipe_read(pipe,NULL,0);
    if(head.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
    uint32_t available = flycast_available(pipe,&tail);
    if(continuation > available) {
        if(head.producer != KOSDEC_ADX_DONE) { errno = EAGAIN; return -1; }
        continuation = available;
    }
    uint32_t real = captured+continuation;
    if(!real && head.producer == KOSDEC_ADX_DONE) {
        f->resume_rate = 0;
        f->exhausted = true;
        p->compat_paused = false;
        p->info.state = KOSDEC_ADX_PLAYER_SOURCE_END;
        return flycast_reanchor(p,true);
    }

    /* No source consumption before the synchronous, checked resume. The
       sole pipe consumer is this owner and the producer only appends. */
    unsigned channels = p->info.channels;
    flycast_copy_pcm(f->staged,f->history,f->ring,survivor,captured,channels);
    flycast_copy_pcm(f->staged+channels*captured,pipe->pcm,pipe->capacity,tail,
                     continuation,channels);
    memset(f->staged+channels*real,0,channels*(prefill-real)*sizeof(*f->staged));
    if(snd_flycast_pcm16_resume(p->ordinary_pcm,f->staged,prefill) < 0)
        return failed(p,errno);
    if(f->resume_rate) {
        p->output.sample_rate = p->info.output_rate = f->resume_rate;
        f->resume_rate = 0;
    }

    __atomic_store_n(&pipe->read_cursor,tail+continuation,__ATOMIC_RELEASE);
    memcpy(f->history+channels*preroll,f->staged,
           channels*prefill*sizeof(*f->staged));
    unsigned lead = f->preroll;
    uint64_t source_cut = f->cut < f->real_end ? f->cut : f->real_end;
    f->elapsed += source_cut-lead;
    f->source_first += source_cut-lead+f->discarded;
    f->output_first += f->cut;
    ++f->epoch;
    f->preroll = preroll;
    f->real_end = preroll+real;
    f->written = f->ring;
    f->discarded = 0;
    p->compat_paused = false;
    p->info.copied_frames += continuation;
    p->info.source_frames += continuation;
    if(continuation && wake_producer(p) < 0) return failed(p,errno);
    return flycast_reanchor(p,true);
}

static int flycast_player_pause(struct kosdec_adx_player *p, bool paused) {
    struct kosdec_adx_flycast_pause *f = p->flycast_pause;
    if(!f || (p->info.state != KOSDEC_ADX_PLAYER_PLAYING &&
              p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END))
        return unsupported();
    if(p->compat_paused == paused && !f->pending) return 0;
    if(f->exhausted) return unsupported();
    if(!paused) return f->pending ? busy() : flycast_resume(p);

    const uint32_t prefill = f->ring-FLYCAST_PREROLL;
    if(!f->pending) {
        snd_ordinary_pcm16_status_t status;
        if(snd_ordinary_pcm16_status(p->ordinary_pcm,&status) < 0) return -1;
        if(status.error) return failed(p,status.error);
        if(status.committed_frames != f->written) return failed(p,EPROTO);
        const unsigned lead = f->preroll;
        if(status.frames[0] < lead ||
           status.frames[0] >= f->real_end ||
           f->real_end - status.frames[0] > prefill) {
            errno = EAGAIN;
            return -1;
        }
        f->cut = status.frames[0];
        f->pending_vsync_count = (uint32_t)p->compat_vsync_anchor +
            (p->compat_service->vsync_count - p->compat_vsync_anchor_tick) * 100u;
    }
    uint64_t cut;
    if(snd_flycast_pcm16_pause(p->ordinary_pcm,p->memory.transfer_timeout_ms,&cut) < 0) {
        if(errno == ETIMEDOUT) f->pending = true;
        return errno == ETIMEDOUT || errno == EAGAIN ? -1 : failed(p,errno);
    }
    unsigned lead = f->preroll;
    if(cut < lead || cut < f->cut || cut > f->written ||
       f->written - cut > f->ring ||
       (cut < f->real_end && f->real_end - cut > prefill))
        return failed(p,EPROTO);
    f->cut = cut;
    f->pending = false;
    p->compat_paused = true;
    return flycast_reanchor(p,false);
}

static int flycast_discard(struct kosdec_adx_player *p, int32_t requested, int32_t *discarded) {
    struct kosdec_adx_flycast_pause *f = p->flycast_pause;
    if(f->pending) return busy();
    if(!p->source_bound || (p->info.state != KOSDEC_ADX_PLAYER_PLAYING &&
                            p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END))
        return unsupported();

    struct kosdec_adx_compat_result pass;
    if(kosdec_adx_compat_exec_server(p->compat_service,&pass) < 0) return -1;
    if(pass.failed_titles || pass.failed_phases) {
        errno = p->info.first_error ? p->info.first_error : EIO;
        return -1;
    }
    if(!p->compat_paused || p->flycast_pause != f || !p->source_bound ||
       (p->info.state != KOSDEC_ADX_PLAYER_PLAYING && p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END))
        return unsupported();
    if(flycast_reanchor(p,true) < 0) return -1;
    uint32_t removed = 0;
    if(requested > 0) {
        uint64_t head = f->cut+f->discarded;
        uint32_t submitted = head < f->real_end ? (uint32_t)(f->real_end-head) : 0;
        uint32_t frames = (uint32_t)requested & ~UINT32_C(15);
        uint32_t decoded = 0, tail;
        if(frames <= submitted) {
            removed = frames & ~UINT32_C(127);
        } else {
            decoded = frames-submitted;
            uint32_t available = flycast_available(&p->producer.pipe,&tail);
            if(decoded > available) decoded = available;
            removed = submitted+decoded;
        }
        if(removed > UINT32_MAX-f->discarded) { errno = EOVERFLOW; return -1; }
        if(decoded) __atomic_store_n(&p->producer.pipe.read_cursor,tail+decoded,__ATOMIC_RELEASE);
        f->discarded += removed;
    }
    *discarded = (int32_t)removed;
    return 0;
}
#endif

int kosdec_adx_compat_player_enable_flycast_pause(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    const uint32_t ring = p->memory.sound_bytes / 2;
    if((!p->ordinary_stereo && !p->ordinary_mono_clock) ||
       !p->compat_service || p->compat_service->servicing ||
       p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound ||
       p->compat_phased || (p->time_uses_vsync && !p->compat_service->use_vsync_clock) ||
       p->requested_rate || p->compat_start_pitch ||
       !p->producer.pipe.stream || p->producer.pipe.stream->linked || p->producer.pipe.stream->repeat ||
       !snd_flycast_pcm16_pause_geometry_supported(44100,ring) || p->memory.capacity < ring)
        return unsupported();
    p->flycast_pause_enabled = true;
    return 0;
#else
    return unsupported();
#endif
}

/* Return one on service, zero when a retained output reports backpressure
   before fill, and minus one on failure. */
static int poll_output(struct kosdec_adx_player *p) {
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(p->ordinary_pcm) {
#ifdef MEDIA_ADX_FLYCAST_PAUSE
        if(p->flycast_pause && (p->compat_paused || p->flycast_pause->pending ||
                                p->flycast_pause->exhausted)) return 1;
#endif
        return snd_ordinary_pcm16_poll(p->ordinary_pcm) < 0 ? -1 : 1;
    }
#endif
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_pcm) {
        if(!snd_retained_pcm_poll(p->retained_pcm)) return 1;
        return errno == EAGAIN ? 0 : -1;
    }
#endif
    return has_output(p) &&
           snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA ? -1 : 1;
}

static int poll_title_output(struct kosdec_adx_player *p) {
    if(p->compat_phased)
        return !has_output(p) || p->compat_renderer_ready;
    return poll_output(p);
}

#ifdef MEDIA_ADX_RETAINED_START_TL
static int close_unstarted_output(struct kosdec_adx_player *p) {
    if(snd_retained_pcm_close(p->retained_pcm) < 0) return -1;
    p->retained_pcm = NULL;
    p->retained_close_pending = false;
    return 0;
}
#endif

static int start_output(struct kosdec_adx_player *p) {
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(p->ordinary_stereo || p->ordinary_mono_clock) {
        unsigned channels = p->ordinary_mono_clock ? 1 : 2;
        const uint32_t ring = p->memory.sound_bytes / 2;
        if(p->output.channels != channels || p->info.sample_rate < 8000 ||
           p->info.sample_rate > 48000)
            return unsupported();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
        if(p->flycast_pause_enabled &&
           (p->info.sample_rate != p->output.sample_rate ||
            !snd_flycast_pcm16_pause_geometry_supported(p->info.sample_rate,ring)))
            return unsupported();
#endif
        if(snd_ordinary_pcm16_open(&p->ordinary_pcm,&p->output,ring,
                                   ordinary_pcm16_fill,p) < 0)
            return -1;
#ifdef MEDIA_ADX_FLYCAST_PAUSE
        if(p->flycast_pause_enabled) {
            p->flycast_pause_unstarted = true;
            const size_t samples = channels*(2u*ring-FLYCAST_PREROLL);
            struct kosdec_adx_flycast_pause *f = calloc(1,sizeof(*f)+samples*sizeof(*f->history));
            p->flycast_pause = f;
            if(!f || snd_flycast_pcm16_pause_enable(p->ordinary_pcm) < 0)
                return -1;
            f->ring = ring;
            f->staged = f->history+channels*ring;
            f->epoch = 1;
            p->info.source_generation = 1;
            p->flycast_pause_unstarted = false;
        }
#endif
        if(snd_ordinary_pcm16_start(p->ordinary_pcm) < 0)
            return -1;
        if(p->ordinary_mono_clock) p->info.source_generation = 1;
#ifndef MEDIA_ADX_RETAINED_CLOCK
        bool collection_clock = compat_collection_vsync(p);
        if(collection_clock || p->vsync_ticks) {
            p->compat_vsync_anchor = 0;
            p->compat_vsync_anchor_tick = collection_clock ? p->compat_service->vsync_count :
                p->vsync_ticks(p->vsync_context);
        }
#endif
        return 1;
    }
#endif
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_offsets) {
        if(snd_retained_pcm_open(&p->retained_pcm,p->output.channels,p->output.sample_rate,
                                p->memory.sound_bytes / 2,retained_fill,p) < 0)
            return -1;
#ifdef MEDIA_ADX_RETAINED_START_TL
        if(p->compat_initial_attenuation) {
            uint8_t tl = p->compat_initial_tl;
            if(!snd_retained_pcm_start_with_tl(p->retained_pcm,tl,tl))
                return 1;
            int error = errno;
            snd_retained_pcm_snapshot_t snapshot;
            /* The backend latches first_error on every attempted failure.
               Only the unsupported preflight can take the close path. */
            if(error == ENOTSUP &&
               !snd_retained_pcm_query(p->retained_pcm,0,&snapshot) && !snapshot.first_error) {
                p->retained_close_pending = true;
                if(close_unstarted_output(p) < 0)
                    p->info.stop_error = errno ? errno : EIO;
            }
            errno = error;
            return -1;
        }
#endif
        return snd_retained_pcm_start(p->retained_pcm) < 0 ? -1 : 1;
    }
#endif
    if(p->ordinary_reprime_pending) {
        snd_stream_config_t config = p->output;
        config.sample_rate = p->info.sample_rate;
        if(p->compat_start_pitch) {
            config.sample_rate = media_adx_initial_pitch_rate(p->info.sample_rate,
                                                             p->compat_start_pitch_units);
            if(!config.sample_rate) return unsupported();
        }

        /* Checked stop and join preceded binding. From the first START
           attempt onward, cleanup must use checked stop again. */
        p->ordinary_reprime_pending = false;
        snd_stream_set_callback(p->sound,refill);
        snd_stream_set_userdata(p->sound,p);
        if(snd_stream_start_ex(p->sound,&config) < 0)
            return -1;
        if(p->info.source_frames != p->memory.sound_bytes / 2) {
            errno = p->callback_failed ? EOVERFLOW : EIO;
            return -1;
        }
        p->output = config;
        p->info.output_rate = config.sample_rate;
        return 1;
    }
    p->sound = snd_stream_alloc(refill,(int)p->memory.sound_bytes);
    if(p->sound == SND_STREAM_INVALID) return -1;
    snd_stream_set_userdata(p->sound,p);
    return snd_stream_start_ex(p->sound,&p->output) < 0 ? -1 : 1;
}
static bool input_stalled(struct kosdec_adx_player *p, uint64_t callbacks,
                          uint64_t starvations) {
    if(!p->memory.input_stall_timeout_ms) return false;
    uint32_t generation = __atomic_load_n(&p->input_wait_generation,__ATOMIC_ACQUIRE);
    struct kosdec_adx_service_state service = kosdec_adx_service_get_state(&p->producer);
    if(!(generation & 1u) || service.paused || service.status != KOSDEC_ADX_MORE) {
        p->input_stall_generation = 0;
        return false;
    }
    if(p->info.callbacks == callbacks) return false;
    if(p->info.starvations == starvations) {
        p->input_stall_generation = 0;
        return false;
    }

    uint64_t now = timer_ms_gettime64();
    if(p->input_stall_generation != generation) {
        p->input_stall_since = now;
        p->input_stall_generation = generation;
        return false;
    }
    return now - p->input_stall_since >= p->memory.input_stall_timeout_ms &&
           __atomic_load_n(&p->input_wait_generation,__ATOMIC_ACQUIRE) == generation;
}

static int prefill_wait(struct kosdec_adx_player *p) {
    if(timer_ms_gettime64() >= p->prefill_deadline) return failed(p,ETIMEDOUT);
    if(poll_title_output(p) < 0) return failed(p,errno);
    return p->callback_failed ? failed(p,p->info.first_error) : 0;
}

static int poll_player(struct kosdec_adx_player *p) {
    if(p->info.state == KOSDEC_ADX_PLAYER_FAILED) { errno = p->info.first_error; return -1; }
    if(p->info.state != KOSDEC_ADX_PLAYER_PREFILL &&
       p->info.state != KOSDEC_ADX_PLAYER_PLAYING &&
       p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END &&
       p->info.state != KOSDEC_ADX_PLAYER_IDLE &&
       p->info.state != KOSDEC_ADX_PLAYER_RELEASING) return bad();
    if(p->info.state == KOSDEC_ADX_PLAYER_IDLE || p->info.state == KOSDEC_ADX_PLAYER_RELEASING) {
        if(poll_title_output(p) < 0) return failed(p,errno);
        return p->callback_failed ? failed(p,p->info.first_error) : 0;
    }
    struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
    bool starting = p->info.state == KOSDEC_ADX_PLAYER_PREFILL;
    bool running = p->info.state == KOSDEC_ADX_PLAYER_PLAYING;
    uint64_t callbacks = p->info.callbacks, starvations = p->info.starvations;
    if(starting) {
        const size_t sound_frames = p->memory.sound_bytes / 2;
        const bool source_done = r.producer == KOSDEC_ADX_DONE;
        const size_t target = p->compat_service ? p->compat_prefill_frames : sound_frames;
        const bool little_room = p->compat_service &&
            p->memory.capacity - r.queued_frames <= p->compat_block_frames;
        if(r.queued_frames < target && !little_room && !source_done)
            return prefill_wait(p);
        uint32_t rate; unsigned channels;
        if(!kosdec_adx_pipe_get_format(&p->producer.pipe,&rate,&channels)) return failed(p,EPROTO);
        if((has_output(p) || p->ordinary_reprime_pending) &&
           (rate != p->info.sample_rate || channels != p->output.channels)) return failed(p,EPROTO);
        if(!has_output(p) && !p->ordinary_reprime_pending) {
            uint32_t output_rate = p->requested_rate ? p->requested_rate : rate;
            if(output_rate < 8000 || output_rate > 48000) return failed(p,ENOTSUP);
            if(p->compat_start_pitch && !p->requested_rate) {
                if(!p->compat_start_rate) {
                    uint32_t pitch = media_adx_initial_pitch_rate(rate,p->compat_start_pitch_units);
#ifdef MEDIA_ADX_ORDINARY_STEREO
                    if(p->ordinary_stereo || p->ordinary_mono_clock) {
                        if(!pitch) pitch = media_adx_live_pitch_rate(rate,p->compat_start_pitch_units);
                        if(!snd_ordinary_pcm16_rate_supported(pitch,sound_frames) ||
                           (pitch > 88200 && p->memory.capacity < 2 * sound_frames))
                            return failed(p,ENOTSUP);
                    }
#endif
                    p->compat_start_rate = pitch;
                }
                if(!p->compat_start_rate || (p->retained_offsets && p->compat_start_rate > 48000))
                    return failed(p,ENOTSUP);
                output_rate = p->compat_start_rate;
            }
            p->output.sample_rate = output_rate;
        }
        p->info.output_rate = p->output.sample_rate;
        p->output.channels = (uint8_t)channels;
        p->info.sample_rate = rate;
        p->info.channels = channels;
        for(unsigned ch = 0; ch < 2; ++ch)
            if(p->requested_pan[ch] == KOSDEC_ADX_PAN_AUTO)
                p->output.channel[ch].pan = output_pan(KOSDEC_ADX_PAN_AUTO,ch,channels);
        p->info.ready_start = true;
        if(p->compat_service) p->compat_ready_start = true;
        if(p->compat_paused && !p->info.wait_start)
            p->compat_preoutput_playing = true;
        if(start_held(p)) {
            p->pcm_enabled = false;
            if(poll_title_output(p) < 0) return failed(p,errno);
            return p->callback_failed ? failed(p,p->info.first_error) : 0;
        }
        if(p->compat_discarded_all && r.drained) {
            p->info.state = KOSDEC_ADX_PLAYER_SOURCE_END;
            return 0;
        }
        /* The compatibility gate describes preparation. The native refill
           still needs its startup PCM window to avoid an internal gap. */
        size_t startup_frames = sound_frames;
#ifdef MEDIA_ADX_ORDINARY_STEREO
        /* Keep a decoded ring in reserve when the first START is above
           88.2 kHz. Compatibility PREP readiness still uses its own gate. */
        if((p->ordinary_stereo || p->ordinary_mono_clock) && p->output.sample_rate > 88200)
            startup_frames += sound_frames;
#endif
        if(r.queued_frames < startup_frames) {
            if(!source_done) return prefill_wait(p);
            if(p->ordinary_reprime_pending) return failed(p,ENODATA);
        }
        p->pcm_enabled = true;
        int service = has_output(p) ? poll_title_output(p) : start_output(p);
        if(service <= 0) return service ? failed(p,errno) : 0;
#ifdef MEDIA_ADX_RETAINED_CLOCK
        if(p->retained_pcm) ++p->retained_admissions;
        if(p->compat_playend.stop_timeout_ms && p->retained_pcm) {
            struct kosdec_adx_source_output clock;
            if(!kosdec_adx_player_get_source_output(p,0,&clock)) {
                p->compat_playend.epoch = clock.epoch;
                p->compat_playend.session = clock.session;
                p->compat_playend.generation = p->info.source_generation;
            }
        }
#endif
        ++p->info.sources_started;
        p->info.state = KOSDEC_ADX_PLAYER_PLAYING;
    }
    else {
        int service = poll_title_output(p);
        if(service <= 0) return service ? failed(p,errno) : 0;
    }
    if(p->callback_failed) return failed(p,p->info.first_error);
    r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
#ifdef MEDIA_ADX_RETAINED_CLOCK
    bool collection_clock = compat_collection_vsync(p);
    bool compat_start_tick = starting && p->compat_service && (collection_clock || p->vsync_ticks);
    if(compat_start_tick) {
        p->compat_vsync_anchor = 0;
        p->compat_vsync_anchor_tick = collection_clock ? p->compat_service->vsync_count :
            p->vsync_ticks(p->vsync_context);
    }
    /* A successful poll can service the clock without admitting source PCM. */
    if(p->vsync_ticks && !p->vsync_generation && p->info.source_frames) {
        p->vsync_last_tick = compat_start_tick && !collection_clock ? p->compat_vsync_anchor_tick :
            p->vsync_ticks(p->vsync_context);
        p->vsync_count = 0;
        p->vsync_generation = p->info.source_generation;
    }
#endif
    if(r.drained) {
#ifdef MEDIA_ADX_RETAINED_CLOCK
        if(p->retained_pcm && !p->retained_sealed && r.producer == KOSDEC_ADX_DONE) {
            if(snd_retained_pcm_seal(p->retained_pcm,p->info.source_generation,p->info.source_frames) < 0)
                return failed(p,errno);
            p->retained_sealed = true;
        }
#endif
        /* Initial refill can copy a whole short source during PREP.
           Publish its end on a separate PLAYING dispatch in this profile. */
        if(!p->compat_service || running)
            p->info.state = KOSDEC_ADX_PLAYER_SOURCE_END;
    }
    else if(running && !p->compat_service && input_stalled(p,callbacks,starvations)) {
        p->info.input_stalled = true;
        return failed(p,ETIMEDOUT);
    }
    return 0;
}
int kosdec_adx_player_poll(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
    return p->compat_service ? busy() : poll_player(p);
}

static enum kosdec_adx_compat_state player_compat_state(const struct kosdec_adx_player *p) {
    if(p->compat_playend.completed)
        return KOSDEC_ADX_COMPAT_PLAYEND;
    switch(p->info.state) {
    case KOSDEC_ADX_PLAYER_READY:
    case KOSDEC_ADX_PLAYER_STOPPED:
        return KOSDEC_ADX_COMPAT_STOP;
    case KOSDEC_ADX_PLAYER_PREFILL:
        if(p->compat_preoutput_playing) return KOSDEC_ADX_COMPAT_PLAYING;
        return p->compat_prefill_frames ? KOSDEC_ADX_COMPAT_PREP : KOSDEC_ADX_COMPAT_DECINFO;
    case KOSDEC_ADX_PLAYER_PLAYING:
        return p->compat_decode_ended ? KOSDEC_ADX_COMPAT_DECEND : KOSDEC_ADX_COMPAT_PLAYING;
    default:
        return KOSDEC_ADX_COMPAT_DECEND;
    }
}

static enum kosdec_adx_compat_state compat_state(void *context) {
    return player_compat_state(context);
}

static bool playend_identity(const struct kosdec_adx_playend *end,
                             const struct kosdec_adx_source_output *clock) {
    return clock->epoch == end->epoch && clock->session == end->session &&
        clock->generation == end->generation;
}

static bool drained_source(const struct kosdec_adx_player *p, uint64_t generation,
                            struct kosdec_adx_source_output *clock) {
    return !qualified_clock(p,generation,clock) && clock->generation == generation &&
        clock->source_output_end && !clock->software_stopped &&
        !clock->output_error && !clock->stop_error;
}

static bool earlier_sources_drained(const struct kosdec_adx_player *p,
                                    const struct kosdec_adx_source_output *final) {
    /* The caller has matched the final generation to the bounded lease
       admissions. An empty final seal can succeed before earlier PCM drains. */
    for(uint64_t generation = 1; generation < final->generation; ++generation) {
        struct kosdec_adx_source_output prior;
        if(!drained_source(p,generation,&prior) || prior.epoch != final->epoch ||
           prior.session != final->session || prior.output_frames != final->output_frames ||
           prior.observation_start_ms != final->observation_start_ms ||
           prior.observation_end_ms != final->observation_end_ms)
            return false;
    }
    return true;
}

static bool latch_playend(struct kosdec_adx_player *p) {
    struct kosdec_adx_playend *end = &p->compat_playend;
    bool single = p->flycast_pause_enabled || p->ordinary_mono_clock;
    if(!end->stop_timeout_ms || !p->source_bound ||
       p->info.first_error || p->callback_failed ||
       p->compat_service->titles[p->compat_slot].faults)
        return false;
    if(!single && (!end->epoch || !end->generation ||
                    end->generation != p->retained_admissions ||
                    p->info.source_generation != end->generation || !p->retained_sealed))
        return false;

    struct kosdec_adx_pipe_result pipe = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    uint32_t queued;
    struct kosdec_adx_decode_progress progress;
    if(!pipe.drained || pipe.producer != KOSDEC_ADX_DONE ||
       !kosdec_adx_service_get_queue_count(&p->producer,&queued) || queued ||
       kosdec_adx_service_get_decode_progress(&p->producer,&progress) < 0 ||
       !progress.samples_complete || progress.status != KOSDEC_ADX_DONE ||
       progress.sample_rate != p->info.sample_rate)
        return false;
    struct kosdec_adx_source_output clock;
    if(single) {
        if(!drained_source(p,p->info.source_generation,&clock) ||
           clock.next_source_frame != progress.declared_frames)
            return false;
        end->generation = clock.generation;
        end->epoch = clock.epoch;
        end->session = clock.session;
    } else if(!drained_source(p,end->generation,&clock) || !playend_identity(end,&clock) ||
              clock.source_frames != p->info.source_frames || !earlier_sources_drained(p,&clock)) {
        return false;
    }

    end->frames = progress.declared_frames;
    end->rate = progress.sample_rate;
    end->anchor = p->compat_vsync_anchor;
    end->pending = true;
    return true;
}

static int finish_playend(struct kosdec_adx_player *p) {
    struct kosdec_adx_playend *end = &p->compat_playend;
    struct kosdec_adx_source_output clock;
    bool retrying = p->info.state == KOSDEC_ADX_PLAYER_STOPPING;
    if(retrying) {
        if(kosdec_adx_player_get_source_output(p,end->generation,&clock) < 0) {
            end->pending = false;
            return -1;
        }
        if(!playend_identity(end,&clock) || clock.output_error) {
            end->pending = false;
            errno = clock.output_error ? clock.output_error : ESTALE;
            return -1;
        }
    }
    if(stop_player(p,end->stop_timeout_ms) < 0) {
        if(errno != ETIMEDOUT || p->info.first_error != ETIMEDOUT)
            end->pending = false;
        return -1;
    }
    end->pending = false;

    if(kosdec_adx_player_get_source_output(p,end->generation,&clock) < 0)
        return -1;
    if(!playend_identity(end,&clock)) {
        errno = ESTALE;
        return -1;
    }
    if(!clock.software_stopped || clock.output_error || clock.stop_error) {
        errno = clock.output_error ? clock.output_error : clock.stop_error ? clock.stop_error : EIO;
        return -1;
    }
    if(p->info.first_error && !retrying) {
        errno = p->info.first_error;
        return -1;
    }
    end->completed = true;
    return 0;
}

static bool compat_channel_limit_rejected(const struct kosdec_adx_player *p) {
    return p->compat_phased && p->source_bound && !has_output(p) &&
        p->info.state == KOSDEC_ADX_PLAYER_PREFILL && !p->compat_prefill_frames &&
        media_adx_stream_exceeds_channel_limit(p->producer.pipe.stream);
}

static int compat_dispatch(void *context, enum kosdec_adx_compat_state state) {
    struct kosdec_adx_player *p = context;
    if(state == KOSDEC_ADX_COMPAT_PLAYEND)
        return 0;
    if(p->compat_playend.pending)
        return finish_playend(p);
    if(p->info.state == KOSDEC_ADX_PLAYER_STOPPING) {
        errno = p->info.stop_error ? p->info.stop_error : EBUSY;
        return -1;
    }
    if(state != KOSDEC_ADX_COMPAT_DECINFO) {
        if(poll_player(p) < 0) return -1;
        if(state == KOSDEC_ADX_COMPAT_PLAYING && p->info.state == KOSDEC_ADX_PLAYER_PLAYING) {
            struct kosdec_adx_decode_progress progress;
            if(kosdec_adx_player_get_decode_progress(p,&progress) < 0)
                return errno == EAGAIN ? 0 : failed(p,errno);
            if(progress.samples_complete && progress.status <= KOSDEC_ADX_DONE) {
                /* Read the queue after acquiring sample completion. Each channel
                   has two bytes per pending frame. Continue submitting this tail. */
                struct kosdec_adx_pipe_result pipe = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
                if(pipe.producer >= KOSDEC_ADX_INVALID)
                    return failed(p,EIO);
                if(pipe.queued_frames < 32 / sizeof(int16_t))
                    p->compat_decode_ended = true;
            }
        }
        return state == KOSDEC_ADX_COMPAT_DECEND &&
            p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END && latch_playend(p) ? finish_playend(p) : 0;
    }

    if(compat_channel_limit_rejected(p)) {
        /* Stock DECINFO reports the channel error and stops the title.
           Preserve the native rejection and release the unused binding. */
        if(media_adx_player_stop_compat_in_service(p,p->memory.transfer_timeout_ms) < 0)
            return -1;
        p->info.decoder = KOSDEC_ADX_UNSUPPORTED;
        p->source_bound = false;
        errno = EIO;
        return -1;
    }

    struct kosdec_adx_decode_progress progress;
    if(kosdec_adx_player_get_decode_progress(p,&progress) < 0)
        return errno == EAGAIN ? prefill_wait(p) : failed(p,errno);
    if(progress.status >= KOSDEC_ADX_INVALID)
        return failed(p,EIO);

    uint32_t rate = progress.sample_rate;
    if(!rate && p->compat_chained) {
        /* Validated chained records all share one format and cannot loop.
           A producer between entries may already have staged this format. */
        unsigned channels;
        (void)kosdec_adx_pipe_get_format(&p->producer.pipe,&rate,&channels);
    }
    if(!rate)
        return progress.status == KOSDEC_ADX_DONE ? failed(p,EPROTO) : prefill_wait(p);
    uint32_t frames;
    unsigned block = progress.block_frames ? progress.block_frames : 32;
    if(kosdec_adx_compat_prefill(p->compat_service,p->compat_slot,rate,block,
                                 progress.loop_present,&frames) < 0)
        return failed(p,errno);
    p->compat_block_frames = (uint8_t)block;
    /* Publish the first reservation before releasing the header wait. */
    __atomic_store_n(&p->compat_prefill_frames,frames,__ATOMIC_RELEASE);
    if(wake_producer(p) < 0)
        return failed(p,errno);
    if(poll_title_output(p) < 0)
        return failed(p,errno);
    if(p->callback_failed)
        return failed(p,p->info.first_error);
#ifdef MEDIA_ADX_RETAINED_START_TL
    if(p->compat_initial_attenuation && !has_output(p))
        p->compat_initial_tl = legacy_gain_tl(p->compat_requested_attenuation);
#endif
    return 0;
}

static int compat_observe(void *context, struct kosdec_adx_compat_observation *out) {
    struct kosdec_adx_player *p = context;
    struct kosdec_adx_decode_progress progress;
    if(kosdec_adx_player_get_decode_progress(p,&progress) < 0)
        return errno == EAGAIN ? 1 : -1;
    *out = (struct kosdec_adx_compat_observation){
        .decoded_position = progress.decoded_frames, .input_bytes = progress.input_bytes,
        .decoder_done = progress.samples_complete, .paused = progress.paused || p->compat_paused
    };
    return 0;
}

static bool compat_transport_failed(void *context) {
    const struct kosdec_adx_player *p = context;
    if(!p->source_bound || p->info.state == KOSDEC_ADX_PLAYER_STOPPED)
        return false;
    enum kosdec_adx_status status = kosdec_adx_service_get_state(&p->producer).status;
    if(status == KOSDEC_ADX_IO_ERROR)
        return true;
    if(status != KOSDEC_ADX_MORE || !p->compat_phased ||
       p->info.state >= KOSDEC_ADX_PLAYER_RELEASING)
        return false;

    /* Only the phased owner may inspect read-ahead before the decoder
       publishes a terminal error. Earlier staged bytes remain valid. */
    const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
    const struct kosdec_adx_stream *stream = pipe->stream;
    if(pipe->sequence && pipe->sequence->active)
        stream = &pipe->sequence->reader;
    if(!stream || stream->terminal || stream->staged_error != KOSDEC_ADX_IO_ERROR ||
       (stream->linked && !stream->follow_links))
        return false;
    if(!stream->repeat)
        return true;

    /* Bytes at or beyond the repeat trap may never be used. */
    uint32_t offset = stream->byte_position + (stream->input_count - stream->input_used);
    return stream->prepared && offset < stream->decoder.loop.end_byte;
}

static enum kosdec_adx_status compat_decode(struct kosdec_adx_player *p) {
    const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
    bool finite = !pipe->joint;
    uint32_t remaining = p->compat_prefill_frames;
    for(;;) {
        struct kosdec_adx_stream_result result =
            media_adx_service_step_limited(&p->producer,remaining,!p->compat_prefill_frames,true);
        if(!finite || !result.frames || result.frames >= remaining || result.status != KOSDEC_ADX_MORE)
            return result.status;
        remaining -= (uint32_t)result.frames;
    }
}

static int compat_player_phase(void *context, enum kosdec_adx_compat_phase phase) {
    struct kosdec_adx_player *p = context;
    enum kosdec_adx_player_state state = p->info.state;
    if(state == KOSDEC_ADX_PLAYER_READY || state >= KOSDEC_ADX_PLAYER_RELEASING)
        return 0;
    if(phase == KOSDEC_ADX_COMPAT_RENDERER) {
        if(!has_output(p))
            return 0;
        p->compat_renderer_ready = false;
        int status = poll_output(p);
        if(status < 0) {
            int error = errno;
            struct kosdec_adx_decode_progress progress;
            p->compat_renderer_failed_playing = state == KOSDEC_ADX_PLAYER_PLAYING &&
                !p->compat_decode_ended && !kosdec_adx_player_get_decode_progress(p,&progress) &&
                progress.status < KOSDEC_ADX_DONE && !progress.samples_complete;
            return failed(p,error);
        }
        if(p->callback_failed)
            return failed(p,p->info.first_error);
        p->compat_renderer_ready = status > 0;
        return status;
    }
    if(!p->source_bound)
        return 0;
    struct kosdec_adx_service_state producer = kosdec_adx_service_get_state(&p->producer);
    if(producer.status >= KOSDEC_ADX_DONE)
        return 0;
    enum kosdec_adx_status status;
    switch(phase) {
    case KOSDEC_ADX_COMPAT_DECODER:
        status = compat_decode(p);
        if(status == KOSDEC_ADX_UNSUPPORTED && compat_channel_limit_rejected(p))
            return 1; /* DECINFO handles this rejection in the title phase. */
        break;
    case KOSDEC_ADX_COMPAT_FILES:
    case KOSDEC_ADX_COMPAT_STREAMS:
    case KOSDEC_ADX_COMPAT_RELOAD: {
        const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
        const struct kosdec_adx_joint_reader *joint = pipe->joint;
        bool finite = !joint;
        bool sector_source = joint && joint->sector.request.read_at;
        bool serves_source = (phase == KOSDEC_ADX_COMPAT_FILES && finite) ||
            (phase == KOSDEC_ADX_COMPAT_STREAMS && !finite) ||
            (phase == KOSDEC_ADX_COMPAT_RELOAD && joint && joint->linked.loader);
        if(!serves_source || producer.paused || (joint && joint->joint->sealed))
            return 0;
        /* Container providers share the ordinary reader's demand bound.
           Do not surface a later read error while PCM already fills the ring. */
        if(media_adx_pipe_has_container(pipe) &&
           kosdec_adx_pipe_read(&p->producer.pipe,NULL,0).queued_frames == pipe->capacity)
            return 0;
        if(sector_source && joint->decoder.header_ready &&
           joint->decoder.frames_done == joint->decoder.total_frames && !joint->job.trap)
            return 0;
        status = media_adx_service_step_transport(&p->producer,phase == KOSDEC_ADX_COMPAT_RELOAD);
        break;
    }
    default:
        return 0;
    }
    return status >= KOSDEC_ADX_INVALID ? failed(p,EIO) : 1;
}

enum compat_route_field {
    COMPAT_ROUTE_DIRECT = 1,
    COMPAT_ROUTE_EFFECT_LEVEL = 2,
    COMPAT_ROUTE_EFFECT_BUS = 4
};

static void copy_route_fields(snd_channel_routing_t *to, const snd_channel_routing_t *from,
                              unsigned fields) {
    if(fields & COMPAT_ROUTE_DIRECT)
        to->direct_level = from->direct_level;
    if(fields & COMPAT_ROUTE_EFFECT_LEVEL)
        to->effect_send = from->effect_send;
    if(fields & COMPAT_ROUTE_EFFECT_BUS)
        to->effect_channel = from->effect_channel;
}

static void remember_routing(struct kosdec_adx_player *p, unsigned fields) {
    if(!p->compat_route_fields)
        for(unsigned channel = 0; channel < 2; ++channel)
            p->compat_original_routing[channel] = p->output.channel[channel].routing;
    p->compat_route_fields |= fields;
}

static int compat_detach(void *context) {
    struct kosdec_adx_player *p = context;
    if(p->info.state != KOSDEC_ADX_PLAYER_READY && p->info.state != KOSDEC_ADX_PLAYER_STOPPED)
        return busy();
    p->compat_service = NULL;
    p->compat_phased = false;
    p->ordinary_stereo = false;
    if(!p->ordinary_pcm) p->ordinary_mono_clock = false;
    p->compat_ready_start = false;
    p->compat_decode_ended = false;
    p->compat_paused = false;
    p->compat_preoutput_playing = false;
    p->compat_discarded_all = false;
    p->compat_requested_attenuation = 0;
    p->compat_initial_attenuation = false;
    p->compat_initial_tl = 0;
    p->compat_controls = (struct kosdec_adx_compat_controls){.effect_db = -45};
    p->compat_start_pitch = false;
    p->compat_start_rate = 0;
    for(unsigned channel = 0; channel < 2; ++channel)
        copy_route_fields(&p->output.channel[channel].routing,&p->compat_original_routing[channel],
                          p->compat_route_fields);
    p->compat_route_fields = 0;
    p->compat_prefill_frames = 0;
    p->compat_playend = (struct kosdec_adx_playend){0};
    if(!p->decode_progress)
        p->producer.progress_enabled = false;
    return 0;
}

static int bind_compat_player(struct kosdec_adx_compat_service *service, unsigned slot,
                              struct kosdec_adx_player *p, bool phased) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound)
        return bad();
    if(p->compat_service)
        return busy();
    const struct kosdec_adx_pipe *pipe = &p->producer.pipe;
    if(phased && !phased_source(pipe))
        return unsupported();

    static const struct kosdec_adx_compat_title_ops ops = {
        .state = compat_state, .dispatch = compat_dispatch, .observe = compat_observe,
        .transport_failed = compat_transport_failed
    };
    if(kosdec_adx_compat_bind(service,slot,&ops,p) < 0)
        return -1;
    if(kosdec_adx_service_enable_decode_progress(&p->producer) < 0) {
        int error = errno;
        (void)kosdec_adx_compat_unbind(service,slot);
        errno = error;
        return -1;
    }
    service->titles[slot].detach = compat_detach;
    service->titles[slot].service_phase = phased ? compat_player_phase : NULL;
    p->compat_slot = slot;
    p->compat_service = service;
    p->compat_phased = phased;
    return 0;
}

int kosdec_adx_compat_bind_player(struct kosdec_adx_compat_service *service, unsigned slot,
                                 struct kosdec_adx_player *p) {
    return bind_compat_player(service,slot,p,false);
}

int kosdec_adx_compat_bind_phased_player(struct kosdec_adx_compat_service *service, unsigned slot,
                                        struct kosdec_adx_player *p) {
    return bind_compat_player(service,slot,p,true);
}

int kosdec_adx_compat_player_is_ready_start(const struct kosdec_adx_player *p, bool *ready) {
    if(!valid(p) || !ready) return bad();
    if(!p->compat_service) return unsupported();
    *ready = p->compat_ready_start;
    return 0;
}

int kosdec_adx_compat_player_enable_playend(struct kosdec_adx_player *p, uint32_t timeout_ms) {
    if(!valid(p) || !timeout_ms) return bad();
    bool first = p->info.state == KOSDEC_ADX_PLAYER_READY && !p->info.source_generation;
    bool released = p->info.state == KOSDEC_ADX_PLAYER_IDLE && p->retained_sealed &&
        p->info.source_generation == p->retained_admissions;
    if(!first && !released) return bad();
    if(!p->compat_service || (!p->retained_offsets && !p->flycast_pause_enabled &&
                              !p->ordinary_mono_clock))
        return unsupported();
    if(p->compat_service->servicing) return busy();
    p->compat_playend.stop_timeout_ms = timeout_ms;
    return 0;
}

static bool compat_unrecognized_header(const struct kosdec_adx_player *p) {
    if(!p->compat_phased || !p->source_bound || p->info.state != KOSDEC_ADX_PLAYER_FAILED ||
       p->compat_prefill_frames)
        return false;

    const struct kosdec_adx_stream *s = p->producer.pipe.stream;
    if(!s || s->linked || s->seeking || s->staged_error || s->terminal != KOSDEC_ADX_INVALID)
        return false;

    /* The phased owner can inspect the immutable rejected header of its current
       binding. A rebind replaces the producer, including this terminal state. */
    const struct kosdec_adx_decoder *d = &s->decoder;
    if(d->header_ready || d->terminal != KOSDEC_ADX_INVALID ||
       d->header_used != sizeof(d->header) || s->byte_position != sizeof(d->header))
        return false;

    const uint8_t *h = d->header;
    /* Stock DECINFO waits when none of the container signatures match. */
    return !(h[0] == 0x80 && h[1] == 0) &&
           memcmp(h,"SPSD",4) &&
           (memcmp(h,"RIFF",4) || memcmp(h + 8,"WAVE",4)) &&
           (memcmp(h,"FORM",4) || memcmp(h + 8,"AIFF",4)) &&
           memcmp(h,".snd",4) && memcmp(h,".sd\0",4);
}

int kosdec_adx_compat_player_get_state(const struct kosdec_adx_player *p,
                                      enum kosdec_adx_compat_state *state) {
    if(!valid(p) || !state) return bad();
    if(!p->compat_service) return unsupported();
    if(p->info.state == KOSDEC_ADX_PLAYER_FAILED && p->compat_renderer_failed_playing)
        *state = KOSDEC_ADX_COMPAT_PLAYING;
    else
        *state = compat_unrecognized_header(p) ? KOSDEC_ADX_COMPAT_DECINFO : player_compat_state(p);
    return 0;
}

int kosdec_adx_compat_player_is_completed(const struct kosdec_adx_player *p, bool *completed) {
    if(!valid(p) || !completed) return bad();
    if(!p->compat_service) return unsupported();
    *completed = p->compat_playend.completed;
    return 0;
}

static int replace_compat(struct kosdec_adx_player *stopped, struct kosdec_adx_player *next,
                          bool in_service) {
    if(!valid(stopped) || !valid(next) || !stopped->compat_service ||
       stopped->info.state != KOSDEC_ADX_PLAYER_STOPPED ||
       next->info.state != KOSDEC_ADX_PLAYER_READY || !next->source_bound)
        return bad();
    struct kosdec_adx_compat_service *service = stopped->compat_service;
    if(service->servicing != in_service || next->compat_service)
        return busy();
    bool phased = stopped->compat_phased;
    const struct kosdec_adx_pipe *pipe = &next->producer.pipe;
    if(phased && !phased_source(pipe))
        return unsupported();
    if(kosdec_adx_service_enable_decode_progress(&next->producer) < 0)
        return -1;

    unsigned slot = stopped->compat_slot;
    /* Validation is complete. The collection retains the same occupied
       slot, frequency and cadence clock throughout the handover. */
    next->compat_ready_start = stopped->compat_ready_start;
    next->compat_paused = stopped->compat_paused;
    next->compat_requested_attenuation = stopped->compat_requested_attenuation;
    next->compat_controls = stopped->compat_controls;
    if(stopped->compat_route_fields && !next->retained_offsets) {
        remember_routing(next,stopped->compat_route_fields);
        for(unsigned channel = 0; channel < 2; ++channel)
            copy_route_fields(&next->output.channel[channel].routing,&stopped->output.channel[channel].routing,
                              stopped->compat_route_fields);
    }
    (void)compat_detach(stopped);
    service->titles[slot].context = next;
    next->compat_slot = slot;
    next->compat_service = service;
    next->compat_phased = phased;
    return 0;
}

int media_adx_player_replace_compat(struct kosdec_adx_player *stopped,
                                    struct kosdec_adx_player *next) {
    return replace_compat(stopped,next,false);
}

int media_adx_player_replace_compat_in_service(struct kosdec_adx_player *stopped,
                                               struct kosdec_adx_player *next) {
    return replace_compat(stopped,next,true);
}

int media_adx_player_start_compat_in_service(struct kosdec_adx_player *p) {
    if(!valid(p) || !in_phased_dispatch(p) ||
       p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound)
        return bad();
    if(start_linked_loader(p,p->producer.pipe.joint) < 0)
        return failed(p,errno);
    return start_source(p);
}

#if defined(MEDIA_ADX_RETAINED_GAIN) || defined(MEDIA_ADX_RETAINED_START_TL)
static uint8_t legacy_gain_tl(int32_t requested) {
    const unsigned magnitude = requested < -999 ? 999 : requested < 0 ? (unsigned)-requested : 0;
    size_t lo = 0, hi = sizeof(legacy_gain_ranges) / sizeof(legacy_gain_ranges[0]);
    while(lo + 1 < hi) {
        size_t middle = lo + (hi - lo) / 2;
        if(legacy_gain_ranges[middle].first > magnitude) hi = middle;
        else lo = middle;
    }
    return legacy_gain_ranges[lo].tl;
}
#endif

static int16_t attenuation_halfword(int32_t requested) {
    uint16_t raw = (uint16_t)requested;
    return (int16_t)(raw <= INT16_MAX ? raw : (int32_t)raw - 65536);
}

int kosdec_adx_player_set_attenuation(struct kosdec_adx_player *p,
    int32_t tenths_db, uint64_t *request) {
    if(!valid(p) || !request) return bad();
#ifdef MEDIA_ADX_RETAINED_GAIN
    if(!p->retained_offsets) return unsupported();
    if(!p->retained_pcm || p->info.state < KOSDEC_ADX_PLAYER_PLAYING) {
        errno = ENODATA;
        return -1;
    }
    if(p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) return bad();
    uint8_t tl = legacy_gain_tl(tenths_db);
    if(snd_retained_pcm_gain_begin(p->retained_pcm,tl,tl,request) < 0) return -1;
    p->requested_attenuation = attenuation_halfword(tenths_db);
    return 0;
#else
    (void)tenths_db;
    return unsupported();
#endif
}

int kosdec_adx_player_get_requested_attenuation(const struct kosdec_adx_player *p,
    int16_t *tenths_db) {
    if(!valid(p) || !tenths_db) return bad();
    *tenths_db = p->requested_attenuation;
    return 0;
}

int kosdec_adx_compat_player_set_attenuation(struct kosdec_adx_player *p,
    int32_t tenths_db, uint64_t *request) {
    if(!valid(p) || !request) return bad();
    if(!p->compat_service) return unsupported();
    if(p->compat_service->servicing) return busy();
    p->compat_requested_attenuation = attenuation_halfword(tenths_db);
    if(p->info.state == KOSDEC_ADX_PLAYER_READY || p->info.state == KOSDEC_ADX_PLAYER_PREFILL ||
       p->info.state == KOSDEC_ADX_PLAYER_STOPPED) {
#ifdef MEDIA_ADX_RETAINED_START_TL
        if(p->compat_initial_attenuation && p->info.state == KOSDEC_ADX_PLAYER_PREFILL &&
           p->compat_prefill_frames && !has_output(p))
            p->compat_initial_tl = legacy_gain_tl(tenths_db);
#endif
        *request = 0;
        return 0;
    }
    return kosdec_adx_player_set_attenuation(p,tenths_db,request);
}

int kosdec_adx_compat_player_get_requested_attenuation(const struct kosdec_adx_player *p,
    int16_t *tenths_db) {
    if(!valid(p) || !tenths_db) return bad();
    if(!p->compat_service) return unsupported();
    *tenths_db = p->compat_requested_attenuation;
    return 0;
}

int kosdec_adx_compat_player_enable_initial_attenuation(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
#ifdef MEDIA_ADX_RETAINED_START_TL
    if(!p->compat_service || !p->retained_offsets) return unsupported();
    if(p->compat_service->servicing) return busy();
    if(p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound) return bad();
    p->compat_initial_attenuation = true;
    return 0;
#else
    return unsupported();
#endif
}

int kosdec_adx_player_get_attenuation_status(const struct kosdec_adx_player *p,
    uint64_t request, struct kosdec_adx_attenuation_status *out) {
    if(!valid(p) || !request || !out) return bad();
#ifdef MEDIA_ADX_RETAINED_GAIN
    if(!p->retained_offsets) return unsupported();
    if(!p->retained_pcm) { errno = ENODATA; return -1; }
    snd_retained_gain_status_t status;
    if(snd_retained_pcm_gain_status(p->retained_pcm,request,&status) < 0) return -1;
    _Static_assert((int)KOSDEC_ADX_ATTENUATION_PENDING == (int)SND_RETAINED_GAIN_PENDING &&
                   (int)KOSDEC_ADX_ATTENUATION_APPLIED == (int)SND_RETAINED_GAIN_APPLIED &&
                   (int)KOSDEC_ADX_ATTENUATION_FAILED == (int)SND_RETAINED_GAIN_FAILED &&
                   KOSDEC_ADX_ATTENUATION_LEFT == SND_RETAINED_GAIN_LEFT &&
                   KOSDEC_ADX_ATTENUATION_RIGHT == SND_RETAINED_GAIN_RIGHT,
                   "attenuation state and channel bits must match the Audio contract");
    *out = (struct kosdec_adx_attenuation_status){
        .epoch = status.epoch, .request = status.request, .session = status.session,
        .state = (enum kosdec_adx_attenuation_state)status.state,
        .admitted = status.admitted, .applied = status.applied, .rejected = status.rejected,
        .unknown = status.unknown, .acknowledged = status.acknowledged,
        .requested_tl = {status.requested_tl[0],status.requested_tl[1]},
        .acknowledged_tl = {status.acknowledged_tl[0],status.acknowledged_tl[1]},
        .error = status.error
    };
    return 0;
#else
    return unsupported();
#endif
}

static int32_t clamp_control(int32_t value, int32_t lo, int32_t hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

static uint8_t route_level(int32_t db, const int8_t minima[15]) {
    uint8_t level = 0;
    while(level < 15 && db >= minima[level])
        ++level;
    return level;
}

static int set_compat_routing(struct kosdec_adx_player *p, unsigned fields, int32_t bus, int32_t db) {
    if(!valid(p) || bus < 0 || bus > 16) return bad();
    if(!p->compat_service || p->retained_offsets) return unsupported();
    if(p->ordinary_pcm && p->info.state != KOSDEC_ADX_PLAYER_STOPPED) return unsupported();
    if(p->compat_service->servicing ||
       (p->info.state >= KOSDEC_ADX_PLAYER_RELEASING && p->info.state != KOSDEC_ADX_PLAYER_STOPPED))
        return busy();

    /* First whole-dB request attaining each 4-bit level. The FX response
       crosses levels 1 and 2 at the same request. */
    static const int8_t direct_minima[15] = {
        -41, -40, -39, -37, -35, -33, -31, -29, -26, -23, -20, -17, -13, -9, -5
    };
    static const int8_t effect_minima[15] = {
        -42, -42, -41, -39, -37, -35, -33, -30, -28, -25, -21, -18, -14, -10, -5
    };
    db = clamp_control(db,-45,0);
    snd_channel_routing_t requested = {0};
    if(fields & COMPAT_ROUTE_DIRECT)
        requested.direct_level = route_level(db,direct_minima);
    else
        requested.effect_send = route_level(db,effect_minima);
    requested.effect_channel = (uint8_t)(bus & 15);
    snd_stream_config_t config = p->output;
    for(unsigned channel = 0; channel < 2; ++channel)
        copy_route_fields(&config.channel[channel].routing,&requested,fields);

    bool live = has_output(p) && p->info.state != KOSDEC_ADX_PLAYER_STOPPED;
    int result = live ? snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_ROUTING) : 0;
    if(fields & COMPAT_ROUTE_DIRECT)
        p->compat_controls.direct_db = (int8_t)db;
    if(fields & COMPAT_ROUTE_EFFECT_LEVEL)
        p->compat_controls.effect_db = (int8_t)db;
    if(fields & COMPAT_ROUTE_EFFECT_BUS)
        p->compat_controls.effect_bus = (uint8_t)bus;
    if(result < 0)
        return failed(p,errno);
    remember_routing(p,fields);
    p->output = config;
    return live ? KOSDEC_ADX_ROUTING_ADMITTED : KOSDEC_ADX_ROUTING_STAGED;
}

int kosdec_adx_compat_player_set_direct_level(struct kosdec_adx_player *p, int32_t db) {
    return set_compat_routing(p,COMPAT_ROUTE_DIRECT,0,db);
}
int kosdec_adx_compat_player_get_direct_level(const struct kosdec_adx_player *p, int32_t *db) {
    if(!valid(p) || !db) return bad();
    if(!p->compat_service) return unsupported();
    *db = p->compat_controls.direct_db;
    return 0;
}
int kosdec_adx_compat_player_set_fx(struct kosdec_adx_player *p, int32_t bus, int32_t db) {
    return set_compat_routing(p,COMPAT_ROUTE_EFFECT_BUS | COMPAT_ROUTE_EFFECT_LEVEL,bus,db);
}
int kosdec_adx_compat_player_set_fx_level(struct kosdec_adx_player *p, int32_t db) {
    return set_compat_routing(p,COMPAT_ROUTE_EFFECT_LEVEL,0,db);
}
int kosdec_adx_compat_player_get_fx(const struct kosdec_adx_player *p, int32_t *bus, int32_t *db) {
    if(!valid(p) || !bus || !db) return bad();
    if(!p->compat_service) return unsupported();
    *bus = p->compat_controls.effect_bus;
    *db = p->compat_controls.effect_db;
    return 0;
}
int kosdec_adx_compat_player_cache_transpose(struct kosdec_adx_player *p, int32_t semitones, int32_t cents) {
    if(!valid(p)) return bad();
    if(!p->compat_service) return unsupported();
    if(p->compat_service->servicing) return busy();
    p->compat_controls.transpose = (int8_t)clamp_control(semitones,-96,96);
    p->compat_controls.detune = (int8_t)clamp_control(cents,-99,99);
    return 0;
}
int kosdec_adx_compat_player_get_transpose(const struct kosdec_adx_player *p, int32_t *semitones, int32_t *cents) {
    if(!valid(p) || !semitones || !cents) return bad();
    if(!p->compat_service) return unsupported();
    *semitones = p->compat_controls.transpose;
    *cents = p->compat_controls.detune;
    return 0;
}
static int cache_pitch_units(struct kosdec_adx_player *p, int32_t semitones,
    int32_t cents, int *units) {
    if(kosdec_adx_compat_player_cache_transpose(p,semitones,cents) < 0) return -1;
    *units = 256 * p->compat_controls.transpose + (256 * p->compat_controls.detune) / 100;
    if(*units < -6399 || *units > 6399) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

int kosdec_adx_compat_player_set_initial_transpose(struct kosdec_adx_player *p,
    int32_t semitones, int32_t cents) {
    if(!valid(p)) return bad();
    const bool stopped = p->info.state == KOSDEC_ADX_PLAYER_STOPPED && compat_reprime_port(p);
    if(!stopped && (p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound))
        return unsupported();
    int units;
    if(cache_pitch_units(p,semitones,cents,&units) < 0) {
        if(stopped && errno == ERANGE) {
            /* A reset followed by a rejected relative pitch starts neutral. */
            p->compat_start_pitch = false;
            p->compat_start_rate = 0;
        }
        return -1;
    }
    uint32_t rate = stopped ? media_adx_initial_pitch_rate(p->info.sample_rate,units) : 0;
    if(stopped && !rate) return unsupported();
    p->compat_start_pitch_units = (int16_t)units;
    p->compat_start_pitch = true;
    p->compat_start_rate = rate;
    return 0;
}

static int update_output_rate(struct kosdec_adx_player *p, uint32_t rate) {
    snd_stream_config_t config = p->output;
    config.sample_rate = rate;
    if(snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_FREQUENCY) < 0)
        return failed(p,errno);
    p->output = config;
    p->info.output_rate = rate;
    return 0;
}

int kosdec_adx_compat_player_enable_stereo_transpose(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(!p->compat_service || p->retained_offsets || p->requested_rate || p->ordinary_mono_clock ||
       p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound ||
       !p->producer.pipe.stream || p->producer.pipe.stream->linked ||
       p->memory.sound_bytes < 16384 || p->memory.sound_bytes > SND_STREAM_BUFFER_MAX_PCM16 ||
       p->memory.sound_bytes % 64)
        return unsupported();
    if(p->compat_service->servicing) return busy();
    p->ordinary_stereo = true;
    return 0;
#else
    return unsupported();
#endif
}

int kosdec_adx_compat_player_enable_mono_clock(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(!p->compat_service || p->retained_offsets || p->requested_rate || p->ordinary_stereo ||
       !p->source_bound || p->info.state != KOSDEC_ADX_PLAYER_READY ||
       !p->producer.pipe.stream || p->producer.pipe.stream->linked ||
       p->producer.pipe.stream->repeat || p->memory.sound_bytes < 16384)
        return unsupported();
    if(p->compat_service->servicing) return busy();
    p->ordinary_mono_clock = true;
    return 0;
#else
    return unsupported();
#endif
}

int kosdec_adx_compat_player_set_live_transpose(struct kosdec_adx_player *p,
    int32_t semitones, int32_t cents) {
    if(!valid(p)) return bad();
    if(!p->source_bound || p->retained_offsets ||
       (!p->ordinary_pcm && (p->sound == SND_STREAM_INVALID || p->info.channels != 1)))
        return unsupported();
    bool held = false;
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause_enabled) {
        if(!p->flycast_pause || p->flycast_pause->exhausted)
            return unsupported();
        if(p->flycast_pause->pending) return busy();
        held = p->compat_paused &&
            (p->info.state == KOSDEC_ADX_PLAYER_PLAYING ||
             p->info.state == KOSDEC_ADX_PLAYER_SOURCE_END);
        if(!held && p->info.state != KOSDEC_ADX_PLAYER_PLAYING)
            return unsupported();
    }
#endif
    if(p->info.state != KOSDEC_ADX_PLAYER_PLAYING && !held) {
#ifdef MEDIA_ADX_ORDINARY_STEREO
        if(!p->ordinary_pcm || p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END)
            return unsupported();
        uint64_t played;
        if(ordinary_source_frames(p,&played) < 0) return -1;
        /* Stock's renderer measures unread PCM at slot zero. The other
           slot still participates in the checked rate change. */
        if(played >= p->info.source_frames) return unsupported();
#else
        return unsupported();
#endif
    }
    int units;
    if(cache_pitch_units(p,semitones,cents,&units) < 0) return -1;
    if(p->requested_rate) return busy();
#ifdef MEDIA_ADX_ORDINARY_STEREO
    if(p->ordinary_pcm) {
        uint32_t rate = media_adx_live_pitch_rate(p->info.sample_rate,units);
        if(!rate || !snd_ordinary_pcm16_rate_supported(rate,p->memory.sound_bytes / 2))
            return unsupported();
#ifdef MEDIA_ADX_FLYCAST_PAUSE
        if(p->flycast_pause_enabled &&
           !snd_flycast_pcm16_pause_geometry_supported(rate,p->flycast_pause->ring))
            return unsupported();
        if(held) {
            if(snd_flycast_pcm16_stage_resume_rate(p->ordinary_pcm,rate) < 0)
                return -1;
            p->flycast_pause->resume_rate = rate;
            return 0;
        }
#endif
        if(snd_ordinary_pcm16_set_rate(p->ordinary_pcm,rate) < 0) {
            /* Only a pre-post headroom refusal returns EAGAIN. */
            if(errno == EAGAIN) return -1;
            return failed(p,errno);
        }
        p->output.sample_rate = p->info.output_rate = rate;
        return 0;
    }
#endif
    uint32_t rate = media_adx_initial_pitch_rate(p->info.sample_rate,units);
    if(!rate || rate > 48000) return unsupported();
    return update_output_rate(p,rate);
}

int kosdec_adx_player_set_output(struct kosdec_adx_player *p,
    unsigned lv, unsigned rv, unsigned lp, unsigned rp) {
    if(!valid(p) || lv > 255 || rv > 255 || lp > 255 || rp > 255 ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    if(p->retained_offsets || p->ordinary_pcm) return unsupported();
    snd_stream_config_t config = p->output;
    config.channel[0].volume = (uint8_t)lv; config.channel[1].volume = (uint8_t)rv;
    config.channel[0].pan = (uint8_t)lp; config.channel[1].pan = (uint8_t)rp;
    if(has_output(p)) {
        if(snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_VOLUME | SND_CHANNEL_UPDATE_PAN) < 0)
            return failed(p,errno);
    }
    p->output = config;
    p->requested_pan[0] = (int16_t)signed_pan(lp);
    p->requested_pan[1] = (int16_t)signed_pan(rp);
    return 0;
}
int kosdec_adx_player_set_pan(struct kosdec_adx_player *p, unsigned channel, int pan) {
    if(!valid(p) || channel > 1 || (pan != KOSDEC_ADX_PAN_AUTO && (pan < -15 || pan > 15)) ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    if(p->retained_offsets || p->ordinary_pcm) return unsupported();
    snd_stream_config_t config = p->output;
    config.channel[channel].pan = output_pan(pan,channel,p->info.channels);
    if(has_output(p) &&
       snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_PAN) < 0)
        return failed(p,errno);
    p->output = config;
    p->requested_pan[channel] = (int16_t)pan;
    return 0;
}
int kosdec_adx_player_get_pan(const struct kosdec_adx_player *p, unsigned channel, int *pan) {
    if(!valid(p) || channel > 1 || !pan) return bad();
    *pan = p->requested_pan[channel];
    return 0;
}
int kosdec_adx_player_set_filter_settings(struct kosdec_adx_player *p,
    int32_t cutoff, int32_t resonance) {
    if(!valid(p)) return bad();
    p->filter_settings = (struct kosdec_adx_filter_settings){cutoff, resonance};
    return 0;
}
int kosdec_adx_player_get_filter_settings(const struct kosdec_adx_player *p,
    struct kosdec_adx_filter_settings *settings) {
    if(!valid(p) || !settings) return bad();
    *settings = p->filter_settings;
    return 0;
}
int kosdec_adx_player_set_rate(struct kosdec_adx_player *p, uint32_t hz) {
    if(!valid(p) || (hz && (hz < 8000 || hz > 48000)) ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    if(p->retained_pcm || p->ordinary_reprime_pending || p->ordinary_stereo ||
       p->ordinary_mono_clock) return unsupported();
    if(p->sound != SND_STREAM_INVALID) {
        uint32_t rate = hz ? hz : p->info.sample_rate;
        if(rate < 8000 || rate > 48000) return bad();
        if(update_output_rate(p,rate) < 0) return -1;
    }
    p->requested_rate = hz;
    return 0;
}
enum kosdec_adx_status kosdec_adx_player_post(struct kosdec_adx_player *p,
    const struct kosdec_adx_command *command) {
    if(!valid(p) || p->info.state == KOSDEC_ADX_PLAYER_READY ||
       !p->source_bound ||
       p->info.state == KOSDEC_ADX_PLAYER_RELEASING ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return KOSDEC_ADX_BAD_ARGUMENT;
    enum kosdec_adx_status r = kosdec_adx_service_post(&p->producer,command);
    /* Once accepted, the receipt still belongs to the controller even if wake
       fails. Preserve MORE and latch failure so poll/stop can report it. */
    if(r == KOSDEC_ADX_MORE && wake_producer(p) < 0) failed(p,errno);
    return r;
}
bool kosdec_adx_player_take(struct kosdec_adx_player *p, enum kosdec_adx_status *result) {
    return valid(p) && p->source_bound && kosdec_adx_service_take(&p->producer,result);
}
bool kosdec_adx_player_take_receipt(struct kosdec_adx_player *p,
                                   struct kosdec_adx_command_receipt *result) {
    return valid(p) && p->source_bound &&
        kosdec_adx_service_take_receipt(&p->producer, result);
}
int kosdec_adx_player_get_info(struct kosdec_adx_player *p, struct kosdec_adx_player_info *info) {
    if(!valid(p) || !info) return bad();
    *info = p->info;
    (void)kosdec_adx_pipe_get_loop_count(&p->producer.pipe,&info->decoded_loops);
    (void)kosdec_adx_service_get_queue_count(&p->producer,&info->queued_sources);
    info->linked=false;
    (void)kosdec_adx_service_get_linked(&p->producer,&info->linked);
    if(p->source_bound) {
        struct kosdec_adx_service_state state = kosdec_adx_service_get_state(&p->producer);
        info->decoder = state.status; info->production_paused = state.paused;
    }
    else info->production_paused = false;
    return 0;
}
int kosdec_adx_player_get_error(const struct kosdec_adx_player *p,
    struct kosdec_adx_player_error *error) {
    if(!valid(p) || !error) return bad();
    *error = p->diagnostic;
    return 0;
}
int kosdec_adx_player_clear_error(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
    p->diagnostic = (struct kosdec_adx_player_error){0};
    p->error_pending = false;
    return 0;
}
int kosdec_adx_player_set_error_callback(struct kosdec_adx_player *p,
    kosdec_adx_player_error_callback callback, void *context) {
    if(!valid(p)) return bad();
    p->error_callback = callback;
    p->error_context = context;
    return 0;
}
int kosdec_adx_player_dispatch_error(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
    if(!p->error_pending || !p->error_callback) return 0;
    struct kosdec_adx_player_error error = p->diagnostic;
    p->error_pending = false;
    int saved_errno = errno;
    p->error_callback(p->error_context,&error);
    errno = saved_errno;
    return 1;
}
int kosdec_adx_player_stop(struct kosdec_adx_player *p, uint32_t timeout_ms) {
    if(!valid(p) || !timeout_ms) return bad();
    if(in_phased_dispatch(p)) return busy();
    if(p->compat_service) {
        struct kosdec_adx_recovery *recovery =
            p->compat_service->titles[p->compat_slot].recovery;
        if(recovery)
            recovery->replay_pending = false;
    }
    p->compat_playend = (struct kosdec_adx_playend){0};
    return stop_player(p,timeout_ms);
}

int media_adx_player_stop_compat_in_service(struct kosdec_adx_player *p, uint32_t timeout_ms) {
    if(!valid(p) || !timeout_ms || !in_phased_dispatch(p)) return bad();
    p->compat_playend = (struct kosdec_adx_playend){0};
    return stop_player(p,timeout_ms);
}

static int stop_player(struct kosdec_adx_player *p, uint32_t timeout_ms) {
    if(p->info.state == KOSDEC_ADX_PLAYER_STOPPED) return 0;
    if(p->info.state != KOSDEC_ADX_PLAYER_STOPPING) {
        /* DMA can fail after the last poll. Checked stop clears driver errors:
           preserve that failure before cleanup, without treating tail silence
           (ENODATA) or an output-position estimate as playback completion. */
        enum kosdec_adx_status source_status = p->source_bound ?
            kosdec_adx_service_get_state(&p->producer).status : p->info.decoder;
        if(p->source_bound && source_status >= KOSDEC_ADX_INVALID &&
           !(p->info.state == KOSDEC_ADX_PLAYER_RELEASING && source_status == KOSDEC_ADX_CANCELLED)) {
            if(!p->info.first_error) p->info.first_error = EIO;
            report_error(p,EIO);
        }
        if(p->sound != SND_STREAM_INVALID && !p->ordinary_reprime_pending) {
            snd_stream_status_t status;
            int error = snd_stream_get_status(p->sound,&status) < 0 ? errno : status.last_error;
            if(error && error != ENODATA) {
                if(!p->info.first_error) p->info.first_error = error;
                report_error(p,error);
            }
        }
#ifdef MEDIA_ADX_RETAINED_CLOCK
        if(p->retained_pcm) {
            snd_retained_pcm_snapshot_t status;
            int error = snd_retained_pcm_query(p->retained_pcm,0,&status) < 0 ?
                errno : status.first_error;
            if(error) {
                if(!p->info.first_error) p->info.first_error = error;
                report_error(p,error);
            }
        }
#endif
    }
    p->info.state = KOSDEC_ADX_PLAYER_STOPPING;
    p->pcm_enabled = false;
    p->info.ready_start = false;
    p->compat_initial_tl = 0;
    if(p->source_bound) cancel_producer(p);
    if(p->worker) fiber_service_request_stop(p->worker);
#ifdef MEDIA_ADX_RETAINED_START_TL
    if(p->retained_close_pending && close_unstarted_output(p) < 0) goto retain;
#endif
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_pcm && !p->retained_stopped) {
        if(snd_retained_pcm_stop(p->retained_pcm,timeout_ms) < 0) goto retain;
        p->retained_stopped = true;
    }
#endif
#ifdef MEDIA_ADX_ORDINARY_STEREO
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause_unstarted) {
        if(snd_ordinary_pcm16_close(p->ordinary_pcm) < 0) goto retain;
        p->ordinary_pcm = NULL;
        p->flycast_pause_unstarted = false;
    }
#endif
    if(p->ordinary_pcm && snd_ordinary_pcm16_stop(p->ordinary_pcm,timeout_ms) < 0)
        goto retain;
#endif
    if(p->sound != SND_STREAM_INVALID && !p->ordinary_reprime_pending) {
        if(snd_stream_stop_ex(p->sound,timeout_ms) < 0) goto retain;
        snd_stream_set_callback(p->sound,NULL);
        snd_stream_set_userdata(p->sound,NULL);
    }
    if(p->executor && fiber_service_executor_destroy(p->executor) < 0) goto retain;
#ifdef MEDIA_ADX_FLYCAST_PAUSE
    if(p->flycast_pause) p->flycast_stopped_epoch = p->flycast_pause->epoch;
    free(p->flycast_pause);
    p->flycast_pause = NULL;
#endif
    p->executor = NULL; p->worker = NULL;
    p->ordinary_reprime_pending = false;
    p->info.stop_error = 0;
    p->info.retained_sound = p->sound;
    p->info.state = KOSDEC_ADX_PLAYER_STOPPED;
    return 0;
retain:
    p->info.stop_error = errno ? errno : EIO;
    report_error(p,p->info.stop_error);
    if(!p->info.first_error) p->info.first_error = p->info.stop_error;
    errno = p->info.stop_error;
    return -1;
}
