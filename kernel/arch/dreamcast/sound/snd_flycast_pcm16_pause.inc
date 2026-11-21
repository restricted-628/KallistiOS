/* The one private owner retains both banks until the sound runtime ends.
   This reuses wave RAM only on the explicitly opted-in Flycast profile. */
static uintptr_t pause_mailbox(void) {
    return SPU_RAM_UNCACHED_BASE + FLYCAST_CUT_ADDR;
}

static uint32_t pause_word(size_t member) {
    return g2_read_32(pause_mailbox() + member);
}

static bool pause_address_matches(const strchan_t *stream) {
    for(unsigned i = 0; i < 2; ++i) {
        uintptr_t reg = UINT32_C(0xa0700000) + stream->ch[i] * 0x80u;
        uint32_t address = ((g2_read_32(reg) & 0x7fu) << 16) |
                           (g2_read_32(reg + 4) & 0xffffu);
        if(address != stream->spu_ram_sch[i])
            return false;
    }
    return true;
}

int snd_flycast_pcm16_pause_enable(snd_ordinary_pcm16_t *pcm) {
    if(ordinary_pcm16_lock(pcm) < 0)
        return -1;
    if(pcm->ring != SND_FLYCAST_PCM16_PAUSE_RING || pcm->rate != 44100 ||
       pause_word(offsetof(flycast_cut_mailbox_t, ready)) != FLYCAST_CUT_MAGIC) {
        errno = ENOTSUP;
        return ordinary_pcm16_unlock(-1);
    }
    if(pause_owner || pcm->attempted || pcm->stopped) {
        errno = EBUSY;
        return ordinary_pcm16_unlock(-1);
    }

    uint32_t alternate = snd_mem_malloc(4u * pcm->ring);
    if(!alternate)
        return ordinary_pcm16_unlock(-1);
    pcm->pause_banks[0] = streams[pcm->handle].spu_ram_sch[0];
    pcm->pause_banks[1] = alternate;
    pause_owner = pcm;
    return ordinary_pcm16_unlock(0);
}

/* The request is the already-qualified left START token. Publish it last
   under the G2 fence. The ARM keeps its result immutable until a newer START
   posts its own cut, so repeated pause after a timeout only reads the result. */
static void pause_submit(const snd_ordinary_pcm16_t *pcm, const strchan_t *stream) {
    uintptr_t box = pause_mailbox();
    g2_lock_scoped();
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, session), pcm->session);
    g2_write_32_raw(box + offsetof(flycast_cut_mailbox_t, cut), pcm->pause_cut);
    for(unsigned i = 0; i < 2; ++i) {
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
        uint64_t first = pcm->voice[0].cursor.frames;
        uint64_t second = pcm->voice[1].cursor.frames;
        uint64_t furthest = first > second ? first : second;

        /* Bound the supported operation to this bank's first traversal
           and reserve a future endpoint already committed on both legs. */
        if(furthest >= pcm->ring - 2048u || furthest + 2048u >= pcm->committed) {
            errno = EAGAIN;
            return ordinary_pcm16_unlock(-1);
        }
        pcm->pause_cut = (uint32_t)furthest + 2048u;
        pcm->pause_state = PAUSE_POSTED;
        pcm->running = false;
        pause_submit(pcm, stream);
    }

    while(stream_time_remaining(deadline)) {
        if(pause_word(offsetof(flycast_cut_mailbox_t, done)) == pcm->voice[0].start) {
            if(pause_word(offsetof(flycast_cut_mailbox_t, result)) != FLYCAST_CUT_NATURAL ||
               !pause_address_matches(stream)) {
                errno = EPROTO;
                goto fail;
            }
            pcm->pause_state = PAUSE_CUT;
            pcm->voice[0].cursor.frames = pcm->voice[1].cursor.frames = pcm->pause_cut;
            *exclusive_cut = pcm->pause_cut;
            return ordinary_pcm16_unlock(0);
        }
        thd_pass();
    }
    errno = ETIMEDOUT;
    return ordinary_pcm16_unlock(-1);

fail:
    pcm->pause_state = PAUSE_HELD;
    ordinary_pcm16_fault(pcm);
    if(errno == EAGAIN || errno == ETIMEDOUT)
        errno = EIO;
    return ordinary_pcm16_unlock(-1);
}

int snd_flycast_pcm16_resume(snd_ordinary_pcm16_t *pcm, const int16_t *interleaved,
                            uint32_t frames) {
    if(!interleaved || frames != SND_FLYCAST_PCM16_PAUSE_RING - SND_FLYCAST_PCM16_PAUSE_PREROLL) {
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
       !pause_address_matches(stream)) {
        errno = ESTALE;
        goto fail;
    }
    unsigned bank = pcm->pause_bank ^ 1u;
    uint32_t base = pcm->pause_banks[bank];
    stream->spu_ram_sch[0] = base;
    stream->spu_ram_sch[1] = base + 2u * pcm->ring;
    pcm->pause_state = PAUSE_HELD;
    pcm->committed = 0;
    const uint32_t half = pcm->ring / 2;
    const uint32_t pad = SND_FLYCAST_PCM16_PAUSE_PREROLL;
    memset(pcm->staging, 0, 4u * pad);
    memcpy(pcm->staging + 2u * pad, interleaved, 4u * (half - pad));
    if(ordinary_pcm16_write(pcm) < 0)
        goto fail;
    memcpy(pcm->staging, interleaved + 2u * (half - pad), 4u * half);
    if(ordinary_pcm16_write(pcm) < 0 || ordinary_pcm16_key_on(pcm, stream) < 0)
        goto fail;
    if(pcm->voice[0].start <= start[0] || pcm->voice[1].start <= start[1] ||
       !pause_address_matches(stream)) {
        errno = ESTALE;
        goto fail;
    }
    pcm->pause_bank = bank;
    pcm->pause_state = PAUSE_LIVE;
    return ordinary_pcm16_unlock(0);
fail:
    pcm->pause_state = PAUSE_HELD;
    return ordinary_pcm16_unlock(ordinary_pcm16_fault(pcm));
}
