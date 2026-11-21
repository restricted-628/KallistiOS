/* The one private owner retains both banks until the sound runtime ends.
   This reuses wave RAM only on the explicitly opted-in Flycast profile. */
static uintptr_t pause_mailbox(void) {
    return SPU_RAM_UNCACHED_BASE + FLYCAST_CUT_ADDR;
}

static uint32_t pause_word(size_t member) {
    return g2_read_32(pause_mailbox() + member);
}

bool snd_flycast_pcm16_pause_rate_supported(uint32_t rate) {
    return rate >= FLYCAST_CUT_RATE_MIN && rate <= FLYCAST_CUT_RATE_MAX;
}

/* This one-octave subset is identical to the ARM START encoder after
   masking the wire word to the AICA OCT/FNS register bits. */
static uint32_t pause_pitch_word(uint32_t rate) {
    if(rate == FLYCAST_CUT_RATE_MAX)
        return 0;
    uint32_t fns = rate * 1024u / (FLYCAST_CUT_RATE_MAX / 2u);
    return FLYCAST_CUT_OCTAVE_DOWN | (fns & FLYCAST_CUT_FNS_MASK);
}

/* Flycast discards the half-step bit at OCT=-1. Round the resulting
   source-frame rate up before applying the existing movement allowance. */
static uint32_t pause_motion_rate(uint32_t rate) {
    uint32_t pitch = pause_pitch_word(rate);
    uint32_t step = 1024u | (pitch & FLYCAST_CUT_FNS_MASK);
    if(pitch)
        step >>= 1;
    return (FLYCAST_CUT_RATE_MAX * step + 1023u) / 1024u;
}

static bool pause_identity_matches(const snd_ordinary_pcm16_t *pcm, const strchan_t *stream) {
    uint32_t pitch = pause_pitch_word(pcm->rate);
    for(unsigned i = 0; i < pcm->channels; ++i) {
        uintptr_t reg = UINT32_C(0xa0700000) + stream->ch[i] * 0x80u;
        uint32_t address = ((g2_read_32(reg) & 0x7fu) << 16) |
                           (g2_read_32(reg + 4) & 0xffffu);
        if(address != stream->spu_ram_sch[i] || (g2_read_32(reg + 0x18) & 0x7fffu) != pitch)
            return false;
    }
    return true;
}

int snd_flycast_pcm16_pause_enable(snd_ordinary_pcm16_t *pcm) {
    if(ordinary_pcm16_lock(pcm) < 0)
        return -1;
    if(pcm->ring != SND_FLYCAST_PCM16_PAUSE_RING || !snd_flycast_pcm16_pause_rate_supported(pcm->rate) ||
       pause_word(offsetof(flycast_cut_mailbox_t, ready)) != FLYCAST_CUT_MAGIC) {
        errno = ENOTSUP;
        return ordinary_pcm16_unlock(-1);
    }
    if(pause_owner || pcm->attempted || pcm->stopped) {
        errno = EBUSY;
        return ordinary_pcm16_unlock(-1);
    }

    uint32_t alternate = snd_mem_malloc(2u * pcm->channels * pcm->ring);
    if(!alternate)
        return ordinary_pcm16_unlock(-1);
    pcm->pause_banks[0] = streams[pcm->handle].spu_ram_sch[0];
    pcm->pause_banks[1] = alternate;
    pcm->motion_rate = pause_motion_rate(pcm->rate);
    pause_owner = pcm;
    return ordinary_pcm16_unlock(0);
}

/* The request is the already-qualified slot 0 START token. Publish it last
   under the G2 fence. The ARM keeps its result immutable until a newer START
   posts its own cut, so repeated pause after a timeout only reads the result. */
static void pause_submit(const snd_ordinary_pcm16_t *pcm, const strchan_t *stream) {
    uintptr_t box = pause_mailbox();
    g2_lock_scoped();
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, session), pcm->session);
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, cut), pcm->pause_cut % pcm->ring);
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, channels), pcm->channels);
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, pitch), pause_pitch_word(pcm->rate));
    for(unsigned i = 0; i < pcm->channels; ++i) {
        uintptr_t slot = box + offsetof(flycast_cut_mailbox_t, slot) +
                         i * sizeof(flycast_cut_slot_t);
        g2_fifo_wait();
        g2_write_32_raw(slot + offsetof(flycast_cut_slot_t, channel), stream->ch[i]);
        g2_write_32_raw(slot + offsetof(flycast_cut_slot_t, start), pcm->voice[i].start);
        g2_write_32_raw(slot + offsetof(flycast_cut_slot_t, command), pcm->voice[i].command);
        g2_write_32_raw(slot + offsetof(flycast_cut_slot_t, address), stream->spu_ram_sch[i]);
    }
    g2_fifo_wait();
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, request), pcm->voice[0].start);
    g2_fifo_wait();
}

/* Each quote was captured after the prior cursor sample. The time from the
   start of that sample to receipt of the quote must exclude a full traversal.
   Then the residue has only one possible forward mapping from that anchor. */
static bool pause_quote_matches(const snd_ordinary_pcm16_t *pcm) {
    uint32_t position[2];
    for(unsigned i = 0; i < pcm->channels; ++i)
        position[i] = pause_word(offsetof(flycast_cut_mailbox_t, position) + i * sizeof(uint32_t));

    uint64_t now = timer_ms_gettime64();
    uint64_t lap = pcm->pause_cut - pcm->pause_cut % pcm->ring;
    for(unsigned i = 0; i < pcm->channels; ++i) {
        const ordinary_pcm16_cursor_t *anchor = &pcm->voice[i].cursor;
        uint64_t bound = ordinary_pcm16_movement(pcm->motion_rate, now - anchor->before_ms);
        if(now < anchor->after_ms || bound >= pcm->ring || position[i] >= pcm->ring)
            return false;
        uint32_t prev = anchor->frames % pcm->ring;
        uint32_t delta = (position[i] + pcm->ring - prev) % pcm->ring;
        if(delta > bound || anchor->frames > UINT64_MAX - delta)
            return false;
        uint64_t frames = anchor->frames + delta;
        if(frames < lap || frames >= pcm->pause_cut || pcm->pause_cut - frames <= FLYCAST_CUT_LEAD)
            return false;
    }
    return true;
}

int snd_flycast_pcm16_pause(snd_ordinary_pcm16_t *pcm, uint32_t timeout,
                           uint64_t *exclusive_cut) {
    if(!timeout || !exclusive_cut) {
        errno = EINVAL;
        return -1;
    }
    if(ordinary_pcm16_lock(pcm) < 0)
        return -1;
    if(pause_owner != pcm || !pcm->attempted || pcm->stopped ||
       pcm->pause_state == PAUSE_HELD) {
        errno = EBUSY;
        return ordinary_pcm16_unlock(-1);
    }
    if(pcm->pause_state == PAUSE_CUT) {
        *exclusive_cut = pcm->pause_cut;
        return ordinary_pcm16_unlock(0);
    }

    uint64_t deadline = timer_ms_gettime64() + timeout;
    strchan_t *stream = &streams[pcm->handle];
    if(pcm->pause_state == PAUSE_LIVE) {
        if(pcm->pending) {
            errno = EBUSY;
            goto fail;
        }
        if(ordinary_pcm16_ready(pcm) < 0 ||
           ordinary_pcm16_sample(pcm, pcm->motion_rate, deadline) < 0)
            goto fail;
        uint64_t furthest = pcm->voice[0].cursor.frames;
        if(pcm->channels == 2 && pcm->voice[1].cursor.frames > furthest)
            furthest = pcm->voice[1].cursor.frames;

        const uint32_t lead = 2048;
        if(furthest > UINT64_MAX - 2 * lead) {
            errno = EOVERFLOW;
            goto fail;
        }
        uint64_t cut = furthest + lead;
        /* Leave time for the quote and authorization if the target is across
           the wrap. Only a short guard interval is added, never another lap. */
        uint32_t position = cut % pcm->ring;
        if(position < lead)
            cut += lead - position;
        if(cut >= pcm->committed) {
            errno = EAGAIN;
            return ordinary_pcm16_unlock(-1);
        }
        pcm->pause_cut = cut;
        pcm->pause_state = PAUSE_PLANNED;
        pcm->running = false;
    }

    /* Let the active cursors enter the target traversal with the original LEA.
       Keep the same absolute cut and stop refilling throughout the wait. */
    if(pcm->pause_state == PAUSE_PLANNED) {
        uint64_t lap = pcm->pause_cut - pcm->pause_cut % pcm->ring;
        while(pcm->voice[0].cursor.frames < lap ||
              (pcm->channels == 2 && pcm->voice[1].cursor.frames < lap)) {
            if(!stream_time_remaining(deadline))
                goto timeout;
            if(ordinary_pcm16_sample(pcm, pcm->motion_rate, deadline) < 0) {
                if(errno == ETIMEDOUT)
                    goto timeout;
                goto fail;
            }
        }
        for(unsigned i = 0; i < pcm->channels; ++i) {
            if(pcm->voice[i].cursor.frames >= pcm->pause_cut ||
               pcm->pause_cut - pcm->voice[i].cursor.frames <= FLYCAST_CUT_LEAD) {
                errno = EPROTO;
                goto fail;
            }
        }
        if(!stream_time_remaining(deadline))
            goto timeout;
        pause_submit(pcm, stream);
        pcm->pause_state = PAUSE_POSTED;
    }

    while(stream_time_remaining(deadline)) {
        uint32_t start = pcm->voice[0].start;
        if(pause_word(offsetof(flycast_cut_mailbox_t, done)) == start) {
            if(pcm->pause_state != PAUSE_AUTHORIZED ||
               pause_word(offsetof(flycast_cut_mailbox_t, result)) != FLYCAST_CUT_NATURAL ||
               !pause_identity_matches(pcm, stream)) {
                errno = EPROTO;
                goto fail;
            }
            pcm->pause_state = PAUSE_CUT;
            for(unsigned i = 0; i < pcm->channels; ++i)
                pcm->voice[i].cursor.frames = pcm->pause_cut;
            *exclusive_cut = pcm->pause_cut;
            return ordinary_pcm16_unlock(0);
        }
        if(pcm->pause_state == PAUSE_POSTED &&
           pause_word(offsetof(flycast_cut_mailbox_t, quoted)) == start) {
            if(!pause_quote_matches(pcm)) {
                errno = EPROTO;
                goto fail;
            }
            if(!stream_time_remaining(deadline))
                goto timeout;
            g2_write_32(pause_mailbox() + offsetof(flycast_cut_mailbox_t, authorize), start);
            pcm->pause_state = PAUSE_AUTHORIZED;
        }
        thd_pass();
    }
timeout:
    errno = ETIMEDOUT;
    return ordinary_pcm16_unlock(-1);

fail:
    pcm->pause_state = PAUSE_HELD;
    ordinary_pcm16_fault(pcm);
    if(errno == EAGAIN || errno == ETIMEDOUT)
        errno = EIO;
    return ordinary_pcm16_unlock(-1);
}

int snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *samples,
                            uint32_t frames) {
    if(!samples || frames != SND_FLYCAST_PCM16_PAUSE_RING - SND_FLYCAST_PCM16_PAUSE_PREROLL) {
        errno = EINVAL;
        return -1;
    }
    if(ordinary_pcm16_lock(pcm) < 0)
        return -1;
    if(pause_owner != pcm || pcm->pause_state != PAUSE_CUT || pcm->pending || pcm->stopped) {
        errno = pcm->error ? pcm->error : EBUSY;
        return ordinary_pcm16_unlock(-1);
    }

    /* A receipt follows the terminal mixer decode in Flycast, including the
       reset-position PCM reads. The previous replacement START used the
       current bank; only the other owned bank becomes writable here. */
    strchan_t *stream = &streams[pcm->handle];
    uint32_t start[2] = { pcm->voice[0].start, pcm->voice[1].start };
    if(pause_word(offsetof(flycast_cut_mailbox_t, ready)) != FLYCAST_CUT_MAGIC ||
       pause_word(offsetof(flycast_cut_mailbox_t, done)) != start[0] ||
       pause_word(offsetof(flycast_cut_mailbox_t, result)) != FLYCAST_CUT_NATURAL ||
       !pause_identity_matches(pcm, stream)) {
        errno = ESTALE;
        goto fail;
    }
    unsigned bank = pcm->pause_bank ^ 1u;
    uint32_t base = pcm->pause_banks[bank];
    stream->spu_ram_sch[0] = base;
    if(pcm->channels == 2)
        stream->spu_ram_sch[1] = base + 2u * pcm->ring;
    pcm->pause_state = PAUSE_HELD;
    pcm->committed = 0;
    const uint32_t half = pcm->ring / 2;
    const uint32_t pad = SND_FLYCAST_PCM16_PAUSE_PREROLL;
    const unsigned channels = pcm->channels;
    memset(pcm->staging, 0, sizeof(*samples) * channels * pad);
    memcpy(pcm->staging + channels * pad, samples, sizeof(*samples) * channels * (half - pad));
    if(ordinary_pcm16_write(pcm) < 0)
        goto fail;
    memcpy(pcm->staging, samples + channels * (half - pad), sizeof(*samples) * channels * half);
    if(ordinary_pcm16_write(pcm) < 0 || ordinary_pcm16_key_on(pcm, stream) < 0)
        goto fail;
    for(unsigned i = 0; i < pcm->channels; ++i) {
        if(pcm->voice[i].start <= start[i]) {
            errno = ESTALE;
            goto fail;
        }
    }
    pcm->pause_bank = bank;
    pcm->pause_state = PAUSE_LIVE;
    return ordinary_pcm16_unlock(0);
fail:
    pcm->pause_state = PAUSE_HELD;
    return ordinary_pcm16_unlock(ordinary_pcm16_fault(pcm));
}
