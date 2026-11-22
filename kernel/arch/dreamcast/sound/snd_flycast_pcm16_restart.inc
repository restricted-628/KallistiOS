#include "snd_flycast_pcm16_restart.h"
#include "arm/aica_pitch.h"

#define FLYCAST_RESTART_TIMER_B UINT32_C(0xa0702894)
/* Worst RR=31 entry: 3.6 ms, rounded to 422706 Q16 units per mixer
   tick. From zero it needs 159 ticks to reach 0x3ff; allow one more. */
#define FLYCAST_RESTART_RELEASE_TICKS 160u

static bool restart_slot_matches(const strchan_t *stream, unsigned side,
                                 uint32_t rate, bool playing) {
    uintptr_t reg = UINT32_C(0xa0700000) + stream->ch[side] * 0x80u;
    uint32_t control = g2_read_32(reg);
    uint32_t address = ((control & 0x7fu) << 16) | (g2_read_32(reg + 4) & 0xffffu);
    return address == stream->spu_ram_sch[side] &&
           (control & 0x4780u) == (playing ? 0x4200u : 0x0200u) &&
           (g2_read_32(reg + 0x14) & 31u) == 31u &&
           (g2_read_32(reg + 0x18) & 0x7fffu) == (calc_aica_pitch(rate) & 0x7fffu);
}

/* Raw STOP preserves the old START token and marks the observation legacy.
   A fresh challenge checks each slot before and after the mixer fence. */
static int restart_stopped_identity(const snd_ordinary_pcm16_t *pcm,
                                    const strchan_t *stream, uint64_t deadline,
                                    bool released) {
    for(unsigned i = 0; i < pcm->channels; ++i) {
        aica_completion_channel_t c;
        uint64_t before, after;
        if(snd_observation_completion_until(stream->ch[i], pcm->session, deadline,
                                            &c, &before, &after) < 0)
            return -1;
        if(!pcm->voice[i].start || c.start_token != pcm->voice[i].start ||
           c.state != AICA_COMPLETION_INTERFERED || c.command_token ||
           c.attribution_kind != AICA_OBSERVATION_ATTR_LEGACY ||
           c.key_on || (released && c.position) ||
           !restart_slot_matches(stream, i, pcm->rate, false)) {
            errno = ESTALE;
            return -1;
        }
    }
    return 0;
}

static int restart_wait_release(const snd_ordinary_pcm16_t *pcm,
                                const strchan_t *stream, uint64_t deadline) {
    if(restart_stopped_identity(pcm, stream, deadline, false) < 0)
        return -1;

    uint32_t count = g2_read_32(FLYCAST_RESTART_TIMER_B);
    unsigned elapsed = 0;
    while(stream_time_remaining(deadline)) {
        uint32_t next = g2_read_32(FLYCAST_RESTART_TIMER_B);
        if((count | next) & 0x700u) {
            errno = ENOTSUP;
            return -1;
        }
        /* An unobserved full traversal only drops credit. Timer B advances
           in Flycast's serialized mixer call; it is never written here. */
        elapsed += (next - count) & 0xffu;
        if(elapsed >= FLYCAST_RESTART_RELEASE_TICKS)
            return restart_stopped_identity(pcm, stream, deadline, true);
        count = next;
        thd_pass();
    }
    errno = ETIMEDOUT;
    return -1;
}

int snd_flycast_pcm16_restart(snd_ordinary_pcm16_t *pcm,
    const snd_stream_config_t *config, snd_ordinary_pcm16_fill_t fill,
    void *context, uint32_t timeout_ms, bool *attempted) {
    if(attempted)
        *attempted = false;
    if(!config || !fill || !timeout_ms || !attempted) {
        errno = EINVAL;
        return -1;
    }
    if(ordinary_pcm16_lock(pcm) < 0)
        return -1;
    if(!pcm->attempted || !pcm->stopped || pcm->error || pcm->pending) {
        errno = pcm->error ? pcm->error : EBUSY;
        return ordinary_pcm16_unlock(-1);
    }
#ifdef AICA_FLYCAST_PCM16_PAUSE
    if(pause_owner == pcm) {
        errno = ENOTSUP;
        return ordinary_pcm16_unlock(-1);
    }
#endif

    strchan_t *stream = &streams[pcm->handle];
    if(config->format != SND_STREAM_FORMAT_PCM16 || config->channels != pcm->channels ||
       !snd_ordinary_pcm16_rate_supported(config->sample_rate, pcm->ring)) {
        errno = EINVAL;
        return ordinary_pcm16_unlock(-1);
    }
    for(unsigned i = 0; i < pcm->channels; ++i) {
        if(config->channel[i].lfo.pitch_depth || config->channel[i].envelope.release_rate != 31 ||
           stream->config.channel[i].envelope.release_rate != 31) {
            errno = ENOTSUP;
            return ordinary_pcm16_unlock(-1);
        }
    }
    if(snd_stream_config_validate(config) < 0 ||
       restart_wait_release(pcm, stream, timer_ms_gettime64() + timeout_ms) < 0)
        return ordinary_pcm16_unlock(-1);

    pcm->staging = ordinary_pcm16_alloc_staging(pcm->channels, pcm->ring);
    if(!pcm->staging)
        return ordinary_pcm16_unlock(-1);

    const snd_stream_config_t previous = stream->config;
    const uint64_t committed = pcm->committed;
    ordinary_pcm16_cursor_t cursors[2];
    uint32_t start[2];
    for(unsigned i = 0; i < pcm->channels; ++i) {
        cursors[i] = pcm->voice[i].cursor;
        start[i] = pcm->voice[i].start;
    }

    *attempted = true;
    pcm->stopped = false;
    pcm->fill = fill;
    pcm->context = context;
    pcm->committed = 0;
    stream->config = *config;
    uint32_t motion_rate = ordinary_pcm16_motion_rate(config->sample_rate);
    if(ordinary_pcm16_fill(pcm) < 0 || ordinary_pcm16_fill(pcm) < 0 ||
       ordinary_pcm16_key_on(pcm, stream, &stream->config, motion_rate) < 0)
        goto fail;
    for(unsigned i = 0; i < pcm->channels; ++i) {
        if(pcm->voice[i].start <= start[i] ||
           !restart_slot_matches(stream, i, config->sample_rate, true)) {
            errno = ESTALE;
            goto fail;
        }
    }
    pcm->rate = config->sample_rate;
    pcm->motion_rate = motion_rate;
    pcm->running = true;
    stream->frequency = (int)pcm->rate;
    stream->state = SND_STREAM_STATE_PLAYING;
    return ordinary_pcm16_unlock(0);

fail:
    stream->config = previous;
    pcm->committed = committed;
    for(unsigned i = 0; i < pcm->channels; ++i)
        pcm->voice[i].cursor = cursors[i];
    return ordinary_pcm16_unlock(ordinary_pcm16_fault(pcm));
}
