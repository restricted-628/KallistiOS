enum fill_mode { TWO_SOURCES, MANY_SOURCES, SILENCE, NEW_SOURCE };
static unsigned calls;
static enum fill_mode mode;

static int fill(void *context, int16_t *samples, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    assert(context == &calls && capacity == 16 && frames == 512);
    ++calls;
    for(unsigned i = 0; i < frames * streams[0].channels; ++i)
        samples[i] = i % 2 ? -2345 : 1234;
    if(mode == TWO_SOURCES && calls == 1) {
        memset(samples, 0, 100 * 4);
        spans[0] = (snd_retained_pcm_span_t){1, 20, 100, SND_RETAINED_PCM_SOURCE};
        spans[1] = (snd_retained_pcm_span_t){0, 0, 128, SND_RETAINED_PCM_STARVATION};
        spans[2] = (snd_retained_pcm_span_t){1, 120, 28, SND_RETAINED_PCM_SOURCE};
        spans[3] = (snd_retained_pcm_span_t){2, 0, 256, SND_RETAINED_PCM_SOURCE};
        return 4;
    }
    if(mode == TWO_SOURCES && calls == 2) {
        spans[0] = (snd_retained_pcm_span_t){2, 256, 5, SND_RETAINED_PCM_SOURCE};
        spans[1] = (snd_retained_pcm_span_t){0, 0, 507, SND_RETAINED_PCM_TERMINAL_SILENCE};
        return 2;
    }
    if(mode == MANY_SOURCES && calls == 1) {
        for(unsigned i = 0; i < 16; ++i)
            spans[i] = (snd_retained_pcm_span_t){i + 1, 0, 32, SND_RETAINED_PCM_SOURCE};
        return 16;
    }
    if(mode == MANY_SOURCES) {
        spans[0] = (snd_retained_pcm_span_t){17, 0, frames, SND_RETAINED_PCM_SOURCE};
        return 1;
    }

    if(mode == NEW_SOURCE) {
        spans[0] = (snd_retained_pcm_span_t){3, 0, frames, SND_RETAINED_PCM_SOURCE};
        return 1;
    }
    spans[0] = (snd_retained_pcm_span_t){0, 0, frames, SND_RETAINED_PCM_TERMINAL_SILENCE};
    return 1;
}

static snd_retained_pcm_t *open_case(enum fill_mode next_mode) {
    snd_retained_pcm_t *pcm = NULL;
    memset(streams, 0, sizeof(streams));
    retire_callback = true;
    stream_sem = 1;
    held_stop_channel = timeout_ack_channel = -1;
    stop_submissions[0] = stop_submissions[1] = 0;
    calls = dmas = syncs = frees = 0;
    position[0] = position[1] = 0;
    now = 100;
    session = 1;
    stop_errno = 0;
    first_delay = right_dma_delay = right_observation_delay = 0;
    bad_channel = fail_dma_channel = -1;
    mode = next_mode;
    assert(!snd_retained_pcm_open(&pcm, 2, 44100, 1024, fill, &calls));
    return pcm;
}

static void poll_at(snd_retained_pcm_t *pcm, unsigned gap, unsigned left, unsigned right) {
    now += gap;
    position[0] = left;
    position[1] = right;
    assert(!snd_retained_pcm_poll(pcm));
}

static snd_retained_pcm_snapshot_t query(snd_retained_pcm_t *pcm, uint64_t generation) {
    snd_retained_pcm_snapshot_t status;
    assert(!snd_retained_pcm_query(pcm, generation, &status));
    return status;
}

static void stop_case(snd_retained_pcm_t *pcm) {
    assert(snd_retained_pcm_stop(pcm, 100) == 0);
    assert(query(pcm, 0).software_stopped);
    assert(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY && !frees);
    assert(streams[0].service_owner == pcm);
    /* End of the model instance. Its retained hardware lease is never reused
       through the tested API. Only the host fixture allocation is disposed. */
    free(pcm);
}

int main(void) {
    snd_retained_pcm_t *pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm) && dmas == 4);
    assert(!snd_retained_pcm_seal(pcm, 1, 128));
    assert(!snd_retained_pcm_seal(pcm, 2, 261));
    int16_t first, silence, last;
    for(unsigned ch = 0; ch < 2; ++ch) {
        memcpy(&first, sound_ram[ch], 2);
        memcpy(&silence, sound_ram[ch] + 200, 2);
        memcpy(&last, sound_ram[ch] + 456, 2);
        assert(!first && !silence && last == (ch ? -2345 : 1234));
    }
    poll_at(pcm, 5, 170, 180);
    snd_retained_pcm_snapshot_t a = query(pcm, 1), b = query(pcm, 2);
    assert(a.valid && a.output_frames == 170 && a.source_frames == 100 &&
           !a.source_output_end && !b.source_frames);
    poll_at(pcm, 9, 519, 521);
    a = query(pcm, 1);
    b = query(pcm, 2);
    assert(a.output_frames == 519 && a.source_frames == 128 &&
           a.next_source_frame == 148 && a.source_output_end);
    assert(b.source_frames == 261 && b.next_source_frame == 261 && b.source_output_end);
    assert(calls == 3 && dmas == 6);
    poll_at(pcm, 10, 948, 950);
    poll_at(pcm, 4, 78, 80);
    assert(query(pcm, 2).output_frames == 1102);
    now += 40;
    assert(!query(pcm, 2).valid);
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == EOVERFLOW);
    assert(query(pcm, 2).output_frames == 1102);
    stop_errno = EBUSY;
    assert(snd_retained_pcm_stop(pcm, 100) == -1 && errno == EBUSY);
    assert(query(pcm, 2).first_error == EOVERFLOW && query(pcm, 2).stop_error == EBUSY);
    stop_errno = 0;
    stop_case(pcm);

    pcm = open_case(TWO_SOURCES);
    first_delay = 30;
    assert(snd_retained_pcm_start(pcm) == -1 && errno == EOVERFLOW);
    assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
    stop_case(pcm);

    pcm = open_case(TWO_SOURCES);
    bad_channel = 1;
    assert(snd_retained_pcm_start(pcm) == -1 && errno == EPROTO);
    assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
    stop_case(pcm);

    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    now += 10;
    position[0] = 250;
    position[1] = 0;
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPROTO);
    assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
    stop_case(pcm);

    /* Both readings independently fit their movement bounds. Delay only the
       right observation so the old symmetric check would accept the reversal. */
    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    assert(!snd_retained_pcm_seal(pcm, 1, 128));
    poll_at(pcm, 2, 80, 80);
    now += 8;
    position[0] = 400;
    position[1] = 300;
    right_observation_delay = 4;
    int result = snd_retained_pcm_poll(pcm);
    int poll_error = errno;
    a = query(pcm, 1);
    if(result != -1)
        fprintf(stderr, "Delayed reverse pair accepted: output=%llu source=%llu end=%u\n",
            (unsigned long long)a.output_frames, (unsigned long long)a.source_frames,
            a.source_output_end);
    assert(result == -1 && poll_error == EPROTO);
    assert(!a.valid && a.clock_error == EPROTO && a.output_frames == 80 &&
           a.source_frames == 80 && a.next_source_frame == 100 && !a.source_output_end);
    assert(calls == 2 && dmas == 4);
    stop_case(pcm);

    /* The same delay can account for legitimate right-channel advancement.
       Credit stops at the earlier left reading. */
    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    assert(!snd_retained_pcm_seal(pcm, 1, 128));
    poll_at(pcm, 2, 80, 80);
    right_observation_delay = 4;
    poll_at(pcm, 8, 400, 550);
    a = query(pcm, 1);
    b = query(pcm, 2);
    assert(a.valid && a.output_frames == 400 && a.source_frames == 128 &&
           a.source_output_end && a.observation_end_ms - a.observation_start_ms == 4);
    assert(b.source_frames == 144 && calls == 2 && dmas == 4);
    stop_case(pcm);

    pcm = open_case(TWO_SOURCES);
    fail_dma_channel = 1;
    assert(snd_retained_pcm_start(pcm) == -1 && errno == EIO);
    assert(calls == 1 && dmas == 2 && !pcm->written && !pcm->count && !syncs);
    assert(snd_retained_pcm_query(pcm, 1, &a) == -1 && errno == ENOENT);
    stop_case(pcm);

    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    mode = NEW_SOURCE;
    now += 14;
    position[0] = position[1] = 521;
    right_dma_delay = 15;
    delayed_position = 10;
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPIPE);
    assert(calls == 3 && dmas == 6 && pcm->written == 1024);
    assert(!query(pcm, 0).valid && query(pcm, 0).output_frames == 521);
    assert(snd_retained_pcm_query(pcm, 3, &a) == -1 && errno == ENOENT);
    stop_case(pcm);

    pcm = open_case(SILENCE);
    assert(!snd_retained_pcm_start(pcm));
    held_stop_channel = 1;
    uint64_t stop_started = now;
    assert(snd_retained_pcm_stop(pcm, 12) == -1 && errno == ETIMEDOUT);
    b = query(pcm, 0);
    assert(now == stop_started + 12 && b.stop_error == ETIMEDOUT && !b.software_stopped);
    assert(stop_submissions[0] == 1 && stop_submissions[1] == 1);
    assert(pcm->stop_observed == 1 && !pcm->receipt[0].token && pcm->receipt[1].token);
    assert(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY && !frees);
    held_stop_channel = -1;
    timeout_ack_channel = 1;
    assert(snd_retained_pcm_stop(pcm, 12) == -1 && errno == ETIMEDOUT);
    assert(pcm->stop_observed == 3 && pcm->receipt[1].token && pcm->receipt_polled[1]);
    assert(!query(pcm, 0).software_stopped && !frees);
    timeout_ack_channel = -1;
    assert(!snd_retained_pcm_stop(pcm, 12));
    assert(stop_submissions[0] == 1 && stop_submissions[1] == 1);
    assert(!query(pcm, 0).stop_error && !pcm->receipt[1].token);
    stop_case(pcm);

    pcm = open_case(MANY_SOURCES);
    assert(snd_retained_pcm_start(pcm) == -1 && errno == ENOSPC);
    assert(pcm->source_count == 16 && dmas == 2 && pcm->written == 512);
    assert(snd_retained_pcm_query(pcm, 17, &a) == -1 && errno == ENOENT);
    stop_case(pcm);

    pcm = open_case(SILENCE);
    assert(!snd_retained_pcm_start(pcm));
    pcm->count = RETAINED_SPANS;
    assert(retained_write(pcm, true) == -1 && errno == EAGAIN);
    assert(calls == 2 && dmas == 4 && !pcm->first_error);
    ++session;
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == ENODEV);
    assert(!query(pcm, 0).valid && !query(pcm, 0).output_frames);
    assert(snd_retained_pcm_stop(pcm, 100) == -1 && errno == ENODEV);
    assert(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY);
    free(pcm->scratch);
    free(pcm);

    pcm = open_case(SILENCE);
    assert(!snd_retained_pcm_close(pcm) && frees == 1);
    assert(!snd_retained_pcm_open(&pcm, 1, 8000, 1024, fill, &calls));
    calls = 0;
    assert(!snd_retained_pcm_start(pcm) && dmas == 2);
    poll_at(pcm, 10, 70, 0);
    assert(query(pcm, 0).valid && query(pcm, 0).output_frames == 70);
    frees = 0;
    stop_case(pcm);
    puts("RETAINED PCM HOST PASS: spans, wrap, skew, stereo acknowledgement, DMA loss, capacity, lease retention");
    return 0;
}
