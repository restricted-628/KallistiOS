/* Original application-owned PCM stream adapter; shared Audio stays in KOS. */
#include <kosdec/adx_player.h>
#include "adx_loader_internal.h"
#include "adx_service_internal.h"
#ifdef MEDIA_ADX_RETAINED_CLOCK
#include "snd_retained_pcm.h"
#endif
#ifdef MEDIA_ADX_RETAINED_GAIN
#include "snd_retained_gain.h"
#include "adx_legacy_gain_table.h"
#endif
#include <kos/timer.h>
#include <errno.h>
#define PLAYER_MAGIC UINT32_C(0x41445841)
static bool valid(const struct kosdec_adx_player *p) {
    return p && p->magic == PLAYER_MAGIC;
}
static int bad(void) { errno = EINVAL; return -1; }
static int unsupported(void) { errno = ENOTSUP; return -1; }
static bool has_output(const struct kosdec_adx_player *p) {
    return p->retained_pcm || p->sound != SND_STREAM_INVALID;
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
    if(kosdec_adx_service_get_state(&p->producer).status >= KOSDEC_ADX_DONE) return 0;
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
    const bool detect_stalls = p->memory.input_stall_timeout_ms != 0;
    uint32_t generation = 0;
    for(;;) {
        if(fiber_service_stop_requested(self))
            kosdec_adx_service_request_cancel(&p->producer);
        uint64_t before = detect_stalls ? input_progress(&p->producer.pipe) : 0;
        struct kosdec_adx_stream_result r = kosdec_adx_service_step(&p->producer);
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
    p->executor = fiber_service_executor_create();
    if(!p->executor) return failed(p,errno);
    p->worker = fiber_service_add(p->executor,p->memory.stack,p->memory.stack_bytes,decode,p);
    if(!p->worker || fiber_service_executor_start(p->executor,NULL) < 0)
        return failed(p,errno);
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
       p->info.state == KOSDEC_ADX_PLAYER_STOPPED || !s->input_capacity) return 0;

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
int kosdec_adx_player_enable_retained_clock(struct kosdec_adx_player *p,
    uint32_t *offsets, size_t count) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_READY ||
       !offsets || (uintptr_t)offsets % _Alignof(uint32_t) ||
       count < p->memory.capacity || p->memory.sound_bytes < 2048 ||
       p->memory.sound_bytes > 65536)
        return bad();
#ifdef MEDIA_ADX_RETAINED_CLOCK
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

static int sample_time(const struct kosdec_adx_player *p, uint64_t generation,
    struct kosdec_adx_time *out) {
    struct kosdec_adx_source_output clock;
    if(kosdec_adx_player_get_source_output(p,generation,&clock) < 0) return -1;
    uint32_t rate = p->info.sample_rate;
    if(!clock.valid || !rate) {
        errno = clock.clock_error ? clock.clock_error : ENODATA;
        return -1;
    }

    int64_t frames;
    if(display_frames(clock.source_frames,p->time_offset,&frames) < 0) return -1;
    *out = (struct kosdec_adx_time){.count = frames, .frequency = rate};
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
    if(kosdec_adx_player_get_source_output(p,generation,&clock) < 0) return -1;
    if(!clock.valid || !p->info.sample_rate) {
        errno = clock.clock_error ? clock.clock_error : ENODATA;
        return -1;
    }
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

int kosdec_adx_player_set_time_mode(struct kosdec_adx_player *p,
    enum kosdec_adx_time_mode mode) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) return bad();
    if(mode == KOSDEC_ADX_TIME_SOURCE) {
        p->time_uses_vsync = false;
        return 0;
    }
    if(mode != KOSDEC_ADX_TIME_NTSC && mode != KOSDEC_ADX_TIME_PAL) return bad();
    if(!p->vsync_ticks) return unsupported();
    p->vsync_frequency = mode == KOSDEC_ADX_TIME_NTSC ? 5994 : 5000;
    p->time_uses_vsync = true;
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

int kosdec_adx_player_set_wait_start(struct kosdec_adx_player *p, bool wait) {
    if(!valid(p) || p->info.state >= KOSDEC_ADX_PLAYER_RELEASING) return bad();
    p->info.wait_start = wait;
    return 0;
}
int kosdec_adx_player_is_ready_start(const struct kosdec_adx_player *p, bool *ready) {
    if(!valid(p) || !ready) return bad();
    *ready = p->info.ready_start;
    return 0;
}
int kosdec_adx_player_release_source(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END) return bad();
    /* No poll/callback can race this serialized owner call. Future callbacks
       cannot touch the retired producer, even if join needs a retry. */
    p->pcm_enabled = false;
    p->info.ready_start = false;
    if(fiber_service_executor_destroy(p->executor) < 0) {
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
    if(!p->source_bound) return 0;
    p->pcm_enabled = false;
    p->info.ready_start = false;
    p->info.state = KOSDEC_ADX_PLAYER_RELEASING;
    p->vsync_generation = 0;
    kosdec_adx_service_request_cancel(&p->producer);
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
    p->info.state = has_output(p) ? KOSDEC_ADX_PLAYER_IDLE : KOSDEC_ADX_PLAYER_READY;
    return 0;
}
static int bind_source(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    struct kosdec_adx_joint_reader *joint, struct kosdec_adx_wave *wave, struct kosdec_adx_au *au, struct kosdec_adx_aiff *aiff, struct kosdec_adx_spsd *spsd) {
    if(!valid(p) || p->source_bound ||
       (p->info.state != KOSDEC_ADX_PLAYER_IDLE && p->info.state != KOSDEC_ADX_PLAYER_READY)) return bad();
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_offsets && (p->retained_admissions >= SND_RETAINED_PCM_GENERATIONS ||
                              p->info.source_generation == UINT64_MAX)) {
        errno = EOVERFLOW;
        return -1;
    }
#endif
    struct kosdec_adx_service producer;
    enum kosdec_adx_status status = spsd ?
        kosdec_adx_service_init_spsd(&producer,spsd,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : aiff ?
        kosdec_adx_service_init_aiff(&producer,aiff,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : au ?
        kosdec_adx_service_init_au(&producer,au,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : wave ?
        kosdec_adx_service_init_wave(&producer,wave,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) : joint ?
        kosdec_adx_service_init_joint(&producer,joint,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) :
        kosdec_adx_service_init(&producer,stream,sequence,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity);
    if(status != KOSDEC_ADX_MORE) return bad();
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

/* Return one on service, zero when a retained output reports backpressure
   before fill, and minus one on failure. */
static int poll_output(struct kosdec_adx_player *p) {
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_pcm) {
        if(!snd_retained_pcm_poll(p->retained_pcm)) return 1;
        return errno == EAGAIN ? 0 : -1;
    }
#endif
    return p->sound != SND_STREAM_INVALID &&
           snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA ? -1 : 1;
}

static int start_output(struct kosdec_adx_player *p) {
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_offsets) {
        if(snd_retained_pcm_open(&p->retained_pcm,p->output.channels,p->output.sample_rate,
                                p->memory.sound_bytes / 2,retained_fill,p) < 0)
            return -1;
        return snd_retained_pcm_start(p->retained_pcm) < 0 ? -1 : 1;
    }
#endif
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

int kosdec_adx_player_poll(struct kosdec_adx_player *p) {
    if(!valid(p)) return bad();
    if(p->info.state == KOSDEC_ADX_PLAYER_FAILED) { errno = p->info.first_error; return -1; }
    if(p->info.state != KOSDEC_ADX_PLAYER_PREFILL &&
       p->info.state != KOSDEC_ADX_PLAYER_PLAYING &&
       p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END &&
       p->info.state != KOSDEC_ADX_PLAYER_IDLE &&
       p->info.state != KOSDEC_ADX_PLAYER_RELEASING) return bad();
    if(p->info.state == KOSDEC_ADX_PLAYER_IDLE || p->info.state == KOSDEC_ADX_PLAYER_RELEASING) {
        if(poll_output(p) < 0) return failed(p,errno);
        return p->callback_failed ? failed(p,p->info.first_error) : 0;
    }
    struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
    bool running = p->info.state == KOSDEC_ADX_PLAYER_PLAYING;
    uint64_t callbacks = p->info.callbacks, starvations = p->info.starvations;
    if(p->info.state == KOSDEC_ADX_PLAYER_PREFILL) {
        if(r.queued_frames < p->memory.sound_bytes / 2 && r.producer != KOSDEC_ADX_DONE) {
            if(timer_ms_gettime64() >= p->prefill_deadline) return failed(p,ETIMEDOUT);
            if(poll_output(p) < 0) return failed(p,errno);
            if(p->callback_failed) return failed(p,p->info.first_error);
            return 0;
        }
        uint32_t rate; unsigned channels;
        if(!kosdec_adx_pipe_get_format(&p->producer.pipe,&rate,&channels)) return failed(p,EPROTO);
        if(has_output(p) &&
           (rate != p->info.sample_rate || channels != p->output.channels)) return failed(p,EPROTO);
        p->output.sample_rate = p->requested_rate ? p->requested_rate : rate;
        if(p->output.sample_rate < 8000 || p->output.sample_rate > 48000) return failed(p,ENOTSUP);
        p->info.output_rate = p->output.sample_rate;
        p->output.channels = (uint8_t)channels;
        p->info.sample_rate = rate;
        p->info.channels = channels;
        for(unsigned ch = 0; ch < 2; ++ch)
            if(p->requested_pan[ch] == KOSDEC_ADX_PAN_AUTO)
                p->output.channel[ch].pan = output_pan(KOSDEC_ADX_PAN_AUTO,ch,channels);
        p->info.ready_start = true;
        if(p->info.wait_start) {
            p->pcm_enabled = false;
            if(poll_output(p) < 0) return failed(p,errno);
            return p->callback_failed ? failed(p,p->info.first_error) : 0;
        }
        p->pcm_enabled = true;
        int service = has_output(p) ? poll_output(p) : start_output(p);
        if(service <= 0) return service ? failed(p,errno) : 0;
#ifdef MEDIA_ADX_RETAINED_CLOCK
        if(p->retained_pcm) ++p->retained_admissions;
#endif
        ++p->info.sources_started;
        p->info.state = KOSDEC_ADX_PLAYER_PLAYING;
    }
    else {
        int service = poll_output(p);
        if(service <= 0) return service ? failed(p,errno) : 0;
    }
    if(p->callback_failed) return failed(p,p->info.first_error);
    r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
#ifdef MEDIA_ADX_RETAINED_CLOCK
    /* A successful poll can service the clock without admitting source PCM. */
    if(p->vsync_ticks && !p->vsync_generation && p->info.source_frames) {
        p->vsync_last_tick = p->vsync_ticks(p->vsync_context);
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
        p->info.state = KOSDEC_ADX_PLAYER_SOURCE_END;
    }
    else if(running && input_stalled(p,callbacks,starvations)) {
        p->info.input_stalled = true;
        return failed(p,ETIMEDOUT);
    }
    return 0;
}
#ifdef MEDIA_ADX_RETAINED_GAIN
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
    uint16_t raw = (uint16_t)tenths_db;
    p->requested_attenuation = (int16_t)(raw <= INT16_MAX ? raw : (int32_t)raw - 65536);
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

int kosdec_adx_player_set_output(struct kosdec_adx_player *p,
    unsigned lv, unsigned rv, unsigned lp, unsigned rp) {
    if(!valid(p) || lv > 255 || rv > 255 || lp > 255 || rp > 255 ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    if(p->retained_offsets) return unsupported();
    snd_stream_config_t config = p->output;
    config.channel[0].volume = (uint8_t)lv; config.channel[1].volume = (uint8_t)rv;
    config.channel[0].pan = (uint8_t)lp; config.channel[1].pan = (uint8_t)rp;
    if(p->sound != SND_STREAM_INVALID) {
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
    if(p->retained_offsets) return unsupported();
    snd_stream_config_t config = p->output;
    config.channel[channel].pan = output_pan(pan,channel,p->info.channels);
    if(p->sound != SND_STREAM_INVALID &&
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
    if(p->retained_pcm) return unsupported();
    if(p->sound != SND_STREAM_INVALID) {
        snd_stream_config_t config = p->output;
        config.sample_rate = hz ? hz : p->info.sample_rate;
        if(config.sample_rate < 8000 || config.sample_rate > 48000) return bad();
        if(snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_FREQUENCY) < 0)
            return failed(p,errno);
        p->output = config;
        p->info.output_rate = config.sample_rate;
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
        if(p->sound != SND_STREAM_INVALID) {
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
    if(p->source_bound) kosdec_adx_service_request_cancel(&p->producer);
    if(p->worker) fiber_service_request_stop(p->worker);
#ifdef MEDIA_ADX_RETAINED_CLOCK
    if(p->retained_pcm && !p->retained_stopped) {
        if(snd_retained_pcm_stop(p->retained_pcm,timeout_ms) < 0) goto retain;
        p->retained_stopped = true;
    }
#endif
    if(p->sound != SND_STREAM_INVALID) {
        if(snd_stream_stop_ex(p->sound,timeout_ms) < 0) goto retain;
        snd_stream_set_callback(p->sound,NULL);
        snd_stream_set_userdata(p->sound,NULL);
    }
    if(p->executor && fiber_service_executor_destroy(p->executor) < 0) goto retain;
    p->executor = NULL; p->worker = NULL;
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
