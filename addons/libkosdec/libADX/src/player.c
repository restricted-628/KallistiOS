/* Original application-owned PCM stream adapter; shared Audio stays in KOS. */
#include <kosdec/adx_player.h>
#include <kos/timer.h>
#include <errno.h>
#define PLAYER_MAGIC UINT32_C(0x41445841)
static bool valid(const struct kosdec_adx_player *p) {
    return p && p->magic == PLAYER_MAGIC;
}
static int bad(void) { errno = EINVAL; return -1; }
static int failed(struct kosdec_adx_player *p, int error) {
    if(!p->info.first_error) p->info.first_error = error ? error : EIO;
    p->info.state = KOSDEC_ADX_PLAYER_FAILED;
    errno = error ? error : EIO;
    return -1;
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
    struct kosdec_adx_joint_reader *joint,
    const struct kosdec_adx_player_config *c) {
    if(!p || !c || !c->scratch || (uintptr_t)c->scratch % _Alignof(int16_t) ||
       c->sound_bytes < 64 || c->sound_bytes > SND_STREAM_BUFFER_MAX_PCM16 ||
       c->sound_bytes % 64 || c->scratch_samples < c->sound_bytes / 2 + 1 ||
       c->capacity < c->sound_bytes / 2 || !c->stack ||
       (uintptr_t)c->stack % 32 || c->stack_bytes < 16384 || c->stack_bytes % 32 ||
       c->stack_bytes > UINTPTR_MAX - (uintptr_t)c->stack ||
       !c->transfer_timeout_ms || !c->prefill_timeout_ms) return bad();
    struct kosdec_adx_player initial = {.magic = PLAYER_MAGIC, .memory = *c,
        .source_bound = true,
        .sound = SND_STREAM_INVALID, .info = {.retained_sound = SND_STREAM_INVALID}};
    enum kosdec_adx_status status = joint ?
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
    return init(p,stream,sequence,NULL,c);
}
int kosdec_adx_player_init_joint(struct kosdec_adx_player *p,
    struct kosdec_adx_joint_reader *joint, const struct kosdec_adx_player_config *c) {
    if(!joint) return bad();
    return init(p,NULL,NULL,joint,c);
}
static void decode(fiber_service_t *self, void *data) {
    struct kosdec_adx_player *p = data;
    for(;;) {
        if(fiber_service_stop_requested(self))
            kosdec_adx_service_request_cancel(&p->producer);
        struct kosdec_adx_stream_result r = kosdec_adx_service_step(&p->producer);
        if(r.status >= KOSDEC_ADX_DONE) return;
        int rc;
        if(kosdec_adx_service_get_state(&p->producer).paused || r.status == KOSDEC_ADX_NEED_OUTPUT)
            rc = fiber_service_wait(self,0);
        else if(r.status == KOSDEC_ADX_NEED_INPUT || r.status == KOSDEC_ADX_NEED_END)
            rc = fiber_service_wait(self,timer_ms_gettime64() + 1);
        else rc = fiber_service_yield(self);
        if(rc < 0) kosdec_adx_service_request_cancel(&p->producer);
    }
}
static int start_source(struct kosdec_adx_player *p) {
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
int kosdec_adx_player_start(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_READY || !p->source_bound) return bad();
    return start_source(p);
}
int kosdec_adx_player_release_source(struct kosdec_adx_player *p) {
    if(!valid(p) || p->info.state != KOSDEC_ADX_PLAYER_SOURCE_END) return bad();
    /* No poll/callback can race this serialized owner call. Future callbacks
       cannot touch the retired producer, even if join needs a retry. */
    p->pcm_enabled = false;
    if(fiber_service_executor_destroy(p->executor) < 0) return -1;
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
    p->info.state = KOSDEC_ADX_PLAYER_RELEASING;
    kosdec_adx_service_request_cancel(&p->producer);
    /* Executor destruction requests stop/wakes and joins its carrier. A failed
       join retains all source users and may be retried while output polls zero. */
    if(p->executor && fiber_service_executor_destroy(p->executor) < 0) return -1;
    p->executor = NULL; p->worker = NULL;
    enum kosdec_adx_status status = kosdec_adx_service_get_state(&p->producer).status;
    p->source_bound = false;
    p->info.decoder = status < KOSDEC_ADX_DONE ? KOSDEC_ADX_CANCELLED : status;
    if(status >= KOSDEC_ADX_INVALID && status != KOSDEC_ADX_CANCELLED) return failed(p,EIO);
    p->info.state = p->sound == SND_STREAM_INVALID ? KOSDEC_ADX_PLAYER_READY : KOSDEC_ADX_PLAYER_IDLE;
    return 0;
}
static int bind_source(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence,
    struct kosdec_adx_joint_reader *joint) {
    if(!valid(p) || p->source_bound ||
       (p->info.state != KOSDEC_ADX_PLAYER_IDLE && p->info.state != KOSDEC_ADX_PLAYER_READY)) return bad();
    struct kosdec_adx_service producer;
    enum kosdec_adx_status status = joint ?
        kosdec_adx_service_init_joint(&producer,joint,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity) :
        kosdec_adx_service_init(&producer,stream,sequence,p->memory.pcm,p->memory.pcm_samples,p->memory.capacity);
    if(status != KOSDEC_ADX_MORE) return bad();
    p->producer = producer;
    p->source_bound = true;
    p->info.source_frames = 0;
    return start_source(p);
}
int kosdec_adx_player_bind_source(struct kosdec_adx_player *p,
    struct kosdec_adx_stream *stream, struct kosdec_adx_sequence *sequence) {
    return bind_source(p,stream,sequence,NULL);
}
int kosdec_adx_player_bind_joint(struct kosdec_adx_player *p,
    struct kosdec_adx_joint_reader *joint) {
    if(!joint) return bad();
    return bind_source(p,NULL,NULL,joint);
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
    }
    *received = (int)(r.frames * channels * 2);
    if(r.frames && wake_producer(p) < 0) {
        p->callback_failed = true;
        if(!p->info.first_error) p->info.first_error = errno;
    }
    return r.frames ? p->scratch : NULL;
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
        if(p->sound != SND_STREAM_INVALID &&
           snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA) return failed(p,errno);
        return p->callback_failed ? failed(p,p->info.first_error) : 0;
    }
    struct kosdec_adx_pipe_result r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
    if(p->info.state == KOSDEC_ADX_PLAYER_PREFILL) {
        if(r.queued_frames < p->memory.sound_bytes / 2 && r.producer != KOSDEC_ADX_DONE) {
            if(timer_ms_gettime64() >= p->prefill_deadline) return failed(p,ETIMEDOUT);
            if(p->sound != SND_STREAM_INVALID &&
               snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA) return failed(p,errno);
            if(p->callback_failed) return failed(p,p->info.first_error);
            return 0;
        }
        uint32_t rate; unsigned channels;
        if(!kosdec_adx_pipe_get_format(&p->producer.pipe,&rate,&channels)) return failed(p,EPROTO);
        if(p->sound != SND_STREAM_INVALID &&
           (rate != p->info.sample_rate || channels != p->output.channels)) return failed(p,EPROTO);
        p->output.sample_rate = p->requested_rate ? p->requested_rate : rate;
        if(p->output.sample_rate < 8000 || p->output.sample_rate > 48000) return failed(p,ENOTSUP);
        p->info.output_rate = p->output.sample_rate;
        p->output.channels = (uint8_t)channels;
        p->info.sample_rate = rate;
        p->info.channels = channels;
        if(!p->custom_pan) {
            p->output.channel[0].pan = channels == 1 ? 128 : 0;
            p->output.channel[1].pan = 255;
        }
        p->pcm_enabled = true;
        if(p->sound == SND_STREAM_INVALID) {
            p->sound = snd_stream_alloc(refill,(int)p->memory.sound_bytes);
            if(p->sound == SND_STREAM_INVALID) return failed(p,errno);
            snd_stream_set_userdata(p->sound,p);
            if(snd_stream_start_ex(p->sound,&p->output) < 0) return failed(p,errno);
        }
        else if(snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA) return failed(p,errno);
        ++p->info.sources_started;
        p->info.state = KOSDEC_ADX_PLAYER_PLAYING;
    }
    else if(snd_stream_poll_ex(p->sound) < 0 && errno != ENODATA) return failed(p,errno);
    if(p->callback_failed) return failed(p,p->info.first_error);
    r = kosdec_adx_pipe_read(&p->producer.pipe,NULL,0);
    if(r.producer >= KOSDEC_ADX_INVALID) return failed(p,EIO);
    if(r.drained) p->info.state = KOSDEC_ADX_PLAYER_SOURCE_END;
    return 0;
}
int kosdec_adx_player_set_output(struct kosdec_adx_player *p,
    unsigned lv, unsigned rv, unsigned lp, unsigned rp) {
    if(!valid(p) || lv > 255 || rv > 255 || lp > 255 || rp > 255 ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
    snd_stream_config_t config = p->output;
    config.channel[0].volume = (uint8_t)lv; config.channel[1].volume = (uint8_t)rv;
    config.channel[0].pan = (uint8_t)lp; config.channel[1].pan = (uint8_t)rp;
    if(p->sound != SND_STREAM_INVALID) {
        if(snd_stream_update(p->sound,&config,SND_CHANNEL_UPDATE_VOLUME | SND_CHANNEL_UPDATE_PAN) < 0)
            return failed(p,errno);
    }
    p->output = config; p->custom_pan = true;
    return 0;
}
int kosdec_adx_player_set_rate(struct kosdec_adx_player *p, uint32_t hz) {
    if(!valid(p) || (hz && (hz < 8000 || hz > 48000)) ||
       p->info.state >= KOSDEC_ADX_PLAYER_FAILED) return bad();
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
int kosdec_adx_player_get_info(struct kosdec_adx_player *p, struct kosdec_adx_player_info *info) {
    if(!valid(p) || !info) return bad();
    *info = p->info;
    (void)kosdec_adx_pipe_get_loop_count(&p->producer.pipe,&info->decoded_loops);
    if(p->source_bound) {
        struct kosdec_adx_service_state state = kosdec_adx_service_get_state(&p->producer);
        info->decoder = state.status; info->production_paused = state.paused;
    }
    else info->production_paused = false;
    return 0;
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
           !(p->info.state == KOSDEC_ADX_PLAYER_RELEASING && source_status == KOSDEC_ADX_CANCELLED) &&
           !p->info.first_error) p->info.first_error = EIO;
        if(p->sound != SND_STREAM_INVALID) {
            snd_stream_status_t status;
            int error = snd_stream_get_status(p->sound,&status) < 0 ? errno : status.last_error;
            if(error && error != ENODATA && !p->info.first_error) p->info.first_error = error;
        }
    }
    p->info.state = KOSDEC_ADX_PLAYER_STOPPING;
    p->pcm_enabled = false;
    if(p->source_bound) kosdec_adx_service_request_cancel(&p->producer);
    if(p->worker) fiber_service_request_stop(p->worker);
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
    if(!p->info.first_error) p->info.first_error = p->info.stop_error;
    errno = p->info.stop_error;
    return -1;
}
