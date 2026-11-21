enum fill_mode { TWO_SOURCES, MANY_SOURCES, SILENCE, NEW_SOURCE };
static unsigned calls;
static enum fill_mode mode;

static int fill(void *context, int16_t *samples, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    assert(context == &calls && capacity == 16 && (frames == 512 || mode == SILENCE));
    ++calls;
    for(unsigned i = 0; i < frames * streams[0].channels; ++i)
        samples[i] = i % 2 ? -2345 : 1234;
    if(mode == TWO_SOURCES && calls == 1) {
        memset(samples, 0, 100 * streams[0].channels * sizeof(*samples));
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

static snd_retained_pcm_t *open_case_with_channels(enum fill_mode next_mode, unsigned channels) {
    snd_retained_pcm_t *pcm = NULL;
    memset(streams, 0, sizeof(streams));
    retire_callback = true;
    stream_sem = 1;
    held_stop_channel = timeout_ack_channel = -1;
    stop_submissions[0] = stop_submissions[1] = 0;
    gain_submissions[0] = gain_submissions[1] = 0;
    gain_tl[0] = gain_tl[1] = 0;
    tl_at_sync[0] = tl_at_sync[1] = 0;
    linear_anchors = key_off_tl_acks = 0;
    delayed_start_key_on = delayed_tl_key_on = -1;
    in_irq = false;
    gain_features = AICA_DRIVER_FEATURE_OBSERVATION_TL;
    gain_submit_error = 0;
    gain_reject_channel = gain_stale_channel = gain_timeout_channel = -1;
    gain_unattributed_channel = gain_retry_channel = -1;
    calls = dmas = syncs = frees = 0;
    position[0] = position[1] = 0;
    now = 100;
    session = 1;
    stop_errno = 0;
    first_delay = right_dma_delay = right_observation_delay = 0;
    left_dma_delay = left_observation_delay = 0;
    overlap_right_observation = false;
    stereo_model_ring = 0;
    replacement_observation_delay[0] = replacement_observation_delay[1] = 0;
    bad_channel = fail_dma_channel = -1;
    mode = next_mode;
    assert(!snd_retained_pcm_open(&pcm, channels, 44100, 1024, fill, &calls));
    return pcm;
}

static snd_retained_pcm_t *open_case(enum fill_mode next_mode) {
    return open_case_with_channels(next_mode, 2);
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

static snd_retained_pcm_t *open_mono_frontier_case(void) {
    snd_retained_pcm_t *pcm = open_case_with_channels(SILENCE, 1);
    assert(!snd_retained_pcm_close(pcm));
    frees = 0;
    assert(!snd_retained_pcm_open(&pcm, 1, 48000, 2048, fill, &calls));
    assert(!snd_retained_pcm_start(pcm) && pcm->written == 2048);
    return pcm;
}

static void test_mono_frontier(void) {
    snd_retained_pcm_t *pcm = open_mono_frontier_case();
    poll_at(pcm, 32, 1500, 0);
    assert(pcm->written == 3072);
    poll_at(pcm, 10, 1941, 0);
    left_observation_delay = 7;
    /* The new bound reaches 3092. The prior bound at the same end is 2815. */
    uint64_t output;
    retained_cursor_t next[2];
    now += 10;
    position[0] = 2703 % 2048;
    assert(!retained_sample(pcm, next, &output) && output == 2703);
    retained_credit(pcm, next, output);
    assert(query(pcm, 0).valid && query(pcm, 0).output_frames == 2703);
    stop_case(pcm);

    pcm = open_mono_frontier_case();
    left_observation_delay = 7;
    left_dma_delay = 8;
    delayed_position = 2322 % 2048;
    now += 28;
    position[0] = 1561;
    /* DMA has returned, but the new sample already exceeds the old frontier. */
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPIPE);
    assert(pcm->written == 2048 && !query(pcm, 0).valid &&
           query(pcm, 0).output_frames == 1561);
    stop_case(pcm);

    const struct {
        unsigned prior, gap, candidate;
        int error;
    } rejected[] = {
        {1907, 16, 2703, EPIPE},  /* Prior bound equals the frontier. */
        {1907, 17, 2703, EPIPE},  /* Both bounds reach past the frontier. */
        {1941, 16, 3072, EPIPE},  /* The measured cursor equals the frontier. */
        {1941, 42, 2703, EOVERFLOW}, /* The unwrap can no longer be proven. */
    };
    for(unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        pcm = open_mono_frontier_case();
        poll_at(pcm, 32, 1500, 0);
        poll_at(pcm, 10, rejected[i].prior, 0);
        left_observation_delay = 7;
        now += rejected[i].gap;
        position[0] = rejected[i].candidate % 2048;
        assert(snd_retained_pcm_poll(pcm) == -1 && errno == rejected[i].error);
        assert(pcm->written == 3072 && !query(pcm, 0).valid &&
               query(pcm, 0).output_frames == rejected[i].prior);
        stop_case(pcm);
    }
    puts("RETAINED MONO FRONTIER HOST PASS: prior proof, observed crossing, equality and wrap");
}

static snd_retained_gain_status_t gain_status(snd_retained_pcm_t *pcm, uint64_t id) {
    snd_retained_gain_status_t status;
    assert(!snd_retained_pcm_gain_status(pcm, id, &status));
    return status;
}

enum stereo_gain_window {
    OLD_WINDOW,
    REPLACEMENT_WINDOW,
    RETRIED_REPLACEMENT_WINDOW,
    STALE_RETRY,
};

static void test_stereo_gain_witness(unsigned ring, bool long_window,
    enum stereo_gain_window window) {
    snd_retained_pcm_t *pcm = open_case(SILENCE);
    if(ring != 1024) {
        assert(!snd_retained_pcm_close(pcm));
        frees = 0;
        assert(!snd_retained_pcm_open(&pcm, 2, 44100, ring, fill, &calls));
    }
    stereo_model_ring = ring;
    stereo_model_origin = now;
    assert(!snd_retained_pcm_start(pcm));
    now = stereo_model_origin + (ring == 1024 ? 10 : 90);
    uint64_t id = 0;
    assert(!snd_retained_pcm_gain_begin(pcm, 15, 15, &id) && id == 1);
    assert(!snd_retained_pcm_poll(pcm));
    assert(gain_status(pcm, id).applied == 3 && pcm->written == ring && dmas == 4);

    const retained_cursor_t prior = pcm->cursor[0];
    assert(prior.frames == (ring == 1024 ? 441 : 3969) &&
           prior.frames == pcm->cursor[1].frames);
    now = stereo_model_origin + (ring == 1024 ? 13 : 166);
    const uint64_t sample_start = now;
    const uint64_t old = 44100 * (now - stereo_model_origin) / 1000;
    const uint64_t left_end = now + (long_window ? (ring == 1024 ? 7 : 15) : 0);
    const uint64_t latest = left_end + 1;
    const uint64_t left = 44100 * (left_end - stereo_model_origin) / 1000;
    const uint64_t right = 44100 * (latest - stereo_model_origin) / 1000;
    const uint64_t prior_upper = window == OLD_WINDOW ?
        prior.frames + retained_movement(pcm, latest - prior.before_ms) :
        old + retained_movement(pcm, latest - sample_start);
    assert(prior_upper < ring && right < ring);
    assert(right + retained_movement(pcm, latest - left_end) < ring);
    assert(!snd_retained_pcm_gain_begin(pcm, 255, 255, &id) && id == 2);

    uint32_t left_token = 0, right_token = 0;
    if(window == RETRIED_REPLACEMENT_WINDOW || window == STALE_RETRY) {
        gain_submit_error = EAGAIN;
        assert(!snd_retained_pcm_poll(pcm));
        snd_retained_gain_status_t pending = gain_status(pcm, id);
        assert(pending.state == SND_RETAINED_GAIN_PENDING && pending.error == EAGAIN &&
               pending.admitted == 1 && pending.unknown == 1 && !pending.applied);
        assert(!query(pcm, 0).valid && pcm->output == old && dmas == 4 && calls == 2);
        assert(gain_submissions[0] == 2 && gain_submissions[1] == 1);
        left_token = pcm->receipt[0].token;

        ++now;
        gain_retry_channel = 1;
        assert(!snd_retained_pcm_poll(pcm));
        pending = gain_status(pcm, id);
        assert(pending.state == SND_RETAINED_GAIN_PENDING && pending.error == EAGAIN &&
               pending.admitted == 3 && pending.unknown == 2 && pending.applied == 1);
        assert(!query(pcm, 0).valid && pcm->output == old && dmas == 4 && calls == 2);
        for(unsigned ch = 0; ch < 2; ++ch)
            assert(pcm->cursor[ch].frames == old && pcm->cursor[ch].before_ms == sample_start);
        assert(pcm->receipt[0].token == left_token);
        right_token = pcm->receipt[1].token;
        ++now;
    }

    assert(now <= left_end);
    assert((left + retained_movement(pcm, latest - now) >= ring) == long_window);
    if(window == OLD_WINDOW) {
        left_observation_delay = left_end - now;
        right_observation_delay = 1;
    } else {
        replacement_observation_delay[0] = left_end - now;
        replacement_observation_delay[1] = 1;
    }
    if(window == STALE_RETRY) {
        gain_stale_channel = 1;
        assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPROTO);
        snd_retained_gain_status_t status = gain_status(pcm, id);
        assert(status.state == SND_RETAINED_GAIN_FAILED && status.error == EPROTO &&
               status.admitted == 3 && status.applied == 1 && status.unknown == 2);
        assert(!query(pcm, 0).valid && pcm->output == old && calls == 2 && dmas == 4);
        assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPROTO && pcm->output == old);
    } else {
        assert(!snd_retained_pcm_poll(pcm));
        snd_retained_gain_status_t status = gain_status(pcm, id);
        assert(status.state == SND_RETAINED_GAIN_APPLIED && status.admitted == 3 &&
               status.applied == 3 && status.acknowledged == 3 && !status.error &&
               !status.unknown && !status.rejected &&
               status.acknowledged_tl[0] == 255 && status.acknowledged_tl[1] == 255);
        snd_retained_pcm_snapshot_t resumed = query(pcm, 0);
        assert(resumed.valid && resumed.output_frames == right && calls == 3 && dmas == 6);
    }
    if(left_token)
        assert(pcm->receipt[0].token == left_token && pcm->receipt[1].token == right_token);
    assert(gain_submissions[0] == 2 && gain_submissions[1] == 2);
    assert(!replacement_observation_delay[0] && !replacement_observation_delay[1]);
    printf("STEREO GAIN FRONTIER HOST PASS: ring=%u long_window=%u window=%u credit=%u\n",
           ring, (unsigned)long_window, (unsigned)window, (unsigned)pcm->output);
    stop_case(pcm);
}

static void test_stereo_gain_frontier_guards(enum retained_sample_phase phase) {
    const struct {
        unsigned prior_right, left, right, gap;
        bool overlap, invalid_receipt;
        int error;
    } cases[] = {
        {440, 930, 945, 11, false, false, 0}, /* Right prior proof. */
        {443, 930, 933, 11, false, false, EPIPE}, /* Both right bounds equal. */
        {480, 940, 978, 11, false, false, EPIPE}, /* Only left is provable. */
        {480, 940, 1024, 11, false, false, EPIPE},
        {480, 940, 1025, 11, false, false, EPIPE},
        {440, 940, 930, 11, false, false, EPROTO}, /* Reverse order. */
        {440, 910, 1005, 11, false, false, EPROTO}, /* Excess skew. */
        {440, 930, 945, 11, true, false, EPROTO},
        {440, 930, 945, 11, false, true, EPROTO},
        {440, 930, 945, 23, false, false, EOVERFLOW},
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        snd_retained_pcm_t *pcm = open_case(SILENCE);
        assert(!snd_retained_pcm_start(pcm));
        poll_at(pcm, 10, 440, cases[i].prior_right);
        assert(pcm->written == 1024 && dmas == 4);
        /* Drive the sampling guards independently of gain deadline admission.
           The replacement path starts from the qualified pair above. */
        if(phase == RETAINED_SAMPLE_GAIN_NEW) {
            uint64_t id;
            assert(!snd_retained_pcm_gain_begin(pcm, 60, 128, &id));
            pcm->gain_rotating = true;
            for(unsigned ch = 0; ch < 2; ++ch) {
                assert(!retained_consume(pcm, ch, now + 100));
                assert(!snd_observation_submit_tl_until(ch, pcm->gain.requested_tl[ch],
                    &pcm->receipt[ch], now + 100));
            }
            pcm->gain.admitted = pcm->gain.unknown = 3;
        }
        now += cases[i].gap;
        position[0] = cases[i].left;
        position[1] = cases[i].right % 1024;
        right_observation_delay = 1;
        overlap_right_observation = cases[i].overlap;
        bad_channel = cases[i].invalid_receipt ? 1 : -1;
        retained_cursor_t next[2];
        uint64_t output = UINT64_MAX;
        int result = retained_sample_until(pcm, next, &output, now + 100, phase);
        int error = phase == RETAINED_SAMPLE_NORMAL && i == 0 ? EPIPE : cases[i].error;
        if(error)
            assert(result == -1 && errno == error);
        else
            assert(!result && output == cases[i].left);
        assert(pcm->output == 440 && pcm->written == 1024 && dmas == 4);
        if(phase == RETAINED_SAMPLE_GAIN_NEW && error == EPIPE)
            assert(pcm->gain.applied == 3 && pcm->gain.acknowledged == 3 && !pcm->gain.unknown);
        stop_case(pcm);

        /* The first row isolates ordinary polling's still-strict fresh bound. */
        if(phase == RETAINED_SAMPLE_NORMAL)
            break;
    }
    printf("STEREO GAIN FRONTIER GUARDS HOST PASS: phase=%u, independent proof, equality, pairing and wrap\n",
           (unsigned)phase);
}

static snd_retained_gain_status_t mono_gain_status(snd_retained_pcm_t *pcm, uint64_t id) {
    snd_retained_gain_status_t status = gain_status(pcm, id);
    assert(!((status.admitted | status.applied | status.rejected |
              status.unknown | status.acknowledged) & ~SND_RETAINED_GAIN_LEFT));
    assert(!status.requested_tl[1] && !status.acknowledged_tl[1] && !gain_submissions[1]);
    return status;
}

static void test_mono_gain(void) {
    snd_retained_pcm_t *pcm = open_case_with_channels(TWO_SOURCES, 1);
    assert(!snd_retained_pcm_start(pcm));
    uint64_t id = 0;
    gain_features = 0;
    assert(snd_retained_pcm_gain_begin(pcm, 60, 88, &id) == -1 && errno == ENOTSUP);
    gain_features = AICA_DRIVER_FEATURE_OBSERVATION_TL;
    assert(snd_retained_pcm_gain_begin(pcm, 60, 256, &id) == -1 && errno == EINVAL);
    assert(!id && !gain_submissions[0]);
    assert(!snd_retained_pcm_gain_begin(pcm, 60, 88, &id) && id == 1);
    snd_retained_gain_status_t status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_PENDING && !status.admitted &&
           status.acknowledged == 1 && status.acknowledged_tl[0] == 0);
    assert(!query(pcm, 1).valid);
    gain_timeout_channel = 0;
    assert(!snd_retained_pcm_poll(pcm));
    status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_PENDING && status.error == ETIMEDOUT &&
           status.admitted == 1 && status.unknown == 1 && !status.applied);
    assert(snd_retained_pcm_gain_begin(pcm, 31, 99, &id) == -1 && errno == EBUSY && id == 1);
    gain_timeout_channel = -1;
    poll_at(pcm, 0, 800, 0);
    status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_APPLIED && status.admitted == 1 &&
           status.applied == 1 && !status.rejected && !status.unknown && !status.error);
    assert(status.acknowledged_tl[0] == 60 && gain_tl[0] == 60 && gain_submissions[0] == 1);
    assert(query(pcm, 2).valid && query(pcm, 2).source_frames == 261 && dmas == 3);

    assert(!snd_retained_pcm_gain_begin(pcm, 0, 201, &id) && id == 2);
    poll_at(pcm, 2, 890, 0);
    status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_APPLIED && status.applied == 1 &&
           status.acknowledged_tl[0] == 0 && gain_submissions[0] == 2);
    assert(snd_retained_pcm_gain_status(pcm, 1, &status) == -1 && errno == ENOENT);
    stop_case(pcm);
    assert(stop_submissions[0] == 1 && !stop_submissions[1]);

    pcm = open_case_with_channels(TWO_SOURCES, 1);
    assert(!snd_retained_pcm_start_with_tl(pcm, 127, 255));
    assert(!snd_retained_pcm_gain_begin(pcm, 60, 88, &id));
    status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_PENDING && !status.admitted &&
           status.acknowledged == 1 && status.acknowledged_tl[0] == 127);
    assert(!snd_retained_pcm_poll(pcm));
    status = mono_gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_APPLIED && status.acknowledged_tl[0] == 60 &&
           gain_submissions[0] == 3);
    stop_case(pcm);

    for(unsigned fault = 0; fault < 3; ++fault) {
        pcm = open_case_with_channels(TWO_SOURCES, 1);
        assert(!snd_retained_pcm_start(pcm));
        assert(!snd_retained_pcm_gain_begin(pcm, 60, 255, &id));
        if(fault < 2) {
            if(fault == 0)
                gain_reject_channel = 0;
            else
                gain_stale_channel = 0;
            assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPROTO);
        }
        else {
            gain_timeout_channel = 0;
            assert(!snd_retained_pcm_poll(pcm));
            gain_timeout_channel = -1;
            assert(!snd_retained_pcm_stop(pcm, 100));
        }
        status = mono_gain_status(pcm, id);
        assert(status.state == SND_RETAINED_GAIN_FAILED && status.admitted == 1 &&
               !status.applied && status.rejected == (fault == 0) &&
               status.unknown == (fault != 0) &&
               status.error == (fault == 2 ? ECANCELED : EPROTO));
        assert(status.acknowledged == 1 && status.acknowledged_tl[0] == 0 &&
               !query(pcm, 1).valid && !query(pcm, 1).source_frames);
        stop_case(pcm);
        assert(stop_submissions[0] == 1 && !stop_submissions[1]);
    }
    puts("RETAINED MONO GAIN HOST PASS: active masks, retry, rejection, unknown and checked stop");
}

static void test_initial_tl(void) {
    snd_retained_pcm_t *pcm = open_case(TWO_SOURCES);
    gain_features = 0;
    assert(snd_retained_pcm_start_with_tl(pcm, 60, 0) == -1 && errno == ENOTSUP);
    gain_features = AICA_DRIVER_FEATURE_OBSERVATION_TL;
    assert(snd_retained_pcm_start_with_tl(pcm, 60, 256) == -1 && errno == EINVAL);
    in_irq = true;
    assert(snd_retained_pcm_start_with_tl(pcm, 60, 0) == -1 && errno == EPERM);
    in_irq = false;
    assert(!pcm->attempted && !calls && !dmas && !syncs && !gain_submissions[0]);
    assert(!snd_retained_pcm_close(pcm) && frees == 1);

    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start_with_tl(pcm, 60, 0));
    assert(syncs == 1 && key_off_tl_acks == 3 && !linear_anchors);
    assert(gain_submissions[0] == 2 && gain_submissions[1] == 2);
    assert(tl_at_sync[0] == 60 && tl_at_sync[1] == 0 &&
           gain_tl[0] == 60 && gain_tl[1] == 0);
    assert(!pcm->gain.request && pcm->gain.acknowledged == 3 &&
           pcm->gain.acknowledged_tl[0] == 60 && pcm->gain.acknowledged_tl[1] == 0);
    poll_at(pcm, 14, 521, 521);
    assert(calls == 3 && dmas == 6 && query(pcm, 2).valid && query(pcm, 2).source_frames == 261);
    assert(gain_tl[0] == 60 && gain_tl[1] == 0 && !linear_anchors);
    stop_case(pcm);

    pcm = open_case_with_channels(SILENCE, 1);
    assert(!snd_retained_pcm_start_with_tl(pcm, 60, 255));
    assert(syncs == 1 && key_off_tl_acks == 1 && !linear_anchors &&
           gain_submissions[0] == 2 && !gain_submissions[1] &&
           tl_at_sync[0] == 60 && gain_tl[0] == 60 && query(pcm, 0).valid);
    stop_case(pcm);

    for(unsigned fault = 0; fault < 4; ++fault) {
        pcm = open_case(TWO_SOURCES);
        if(fault == 0)
            delayed_start_key_on = 1;
        else if(fault == 1)
            delayed_tl_key_on = 1;
        else if(fault == 2)
            gain_reject_channel = 1;
        else
            gain_timeout_channel = 1;
        assert(snd_retained_pcm_start_with_tl(pcm, 60, 61) == -1 &&
               errno == (fault == 3 ? ETIMEDOUT : EPROTO));
        assert(!syncs && key_off_tl_acks == 1 && gain_submissions[0] == 1 &&
               gain_submissions[1] == (fault != 0) && pcm->receipt[1].token);
        assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
        assert(snd_retained_pcm_start_with_tl(pcm, 60, 61) == -1 && errno == EINVAL);
        gain_timeout_channel = -1;
        stop_case(pcm);
    }

    pcm = open_case(TWO_SOURCES);
    first_delay = 30;
    assert(snd_retained_pcm_start_with_tl(pcm, 60, 61) == -1 && errno == EOVERFLOW);
    assert(syncs == 1 && key_off_tl_acks == 3 && !linear_anchors &&
           gain_submissions[0] == 2 && gain_submissions[1] == 2);
    assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
    stop_case(pcm);
    puts("RETAINED INITIAL TL HOST PASS: mono/stereo preflight, staging, handover, refill and checked stop");
}

static void test_gain(void) {
    snd_retained_pcm_t *pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    uint64_t id = 0;
    gain_features = 0;
    assert(snd_retained_pcm_gain_begin(pcm, 127, 128, &id) == -1 && errno == ENOTSUP);
    gain_features = AICA_DRIVER_FEATURE_OBSERVATION_TL;
    assert(snd_retained_pcm_gain_begin(pcm, 127, 256, &id) == -1 && errno == EINVAL);
    assert(!id && !gain_submissions[0] && !gain_submissions[1]);
    poll_at(pcm, 2, 60, 60);
    snd_retained_pcm_snapshot_t before = query(pcm, 1);
    assert(!snd_retained_pcm_gain_begin(pcm, 127, 128, &id));
    assert(!query(pcm, 1).valid && query(pcm, 1).output_frames == 60);
    assert(!gain_submissions[0] && !gain_submissions[1]);
    gain_submit_error = EAGAIN;
    poll_at(pcm, 1, 70, 70);
    snd_retained_gain_status_t status = gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_PENDING && status.error == EAGAIN &&
           status.admitted == SND_RETAINED_GAIN_LEFT && status.unknown == status.admitted &&
           !status.applied && status.acknowledged_tl[0] == 0);
    assert(!query(pcm, 1).valid && query(pcm, 1).source_frames == 70);
    assert(snd_retained_pcm_gain_begin(pcm, 0, 0, &id) == -1 && errno == EBUSY);
    poll_at(pcm, 2, 95, 95);
    status = gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_APPLIED && status.admitted == 3 &&
           status.applied == 3 && !status.unknown && !status.rejected && !status.error);
    assert(status.epoch == before.epoch && status.session == before.session &&
           status.acknowledged_tl[0] == 127 && status.acknowledged_tl[1] == 128);
    assert(query(pcm, 1).valid && query(pcm, 1).source_frames == 95);
    assert(gain_submissions[0] == 1 && gain_submissions[1] == 1);
    uint64_t previous = id;
    assert(!snd_retained_pcm_gain_begin(pcm, 254, 255, &id) && id == previous + 1);
    assert(snd_retained_pcm_gain_status(pcm, previous, &status) == -1 && errno == ENOENT);
    poll_at(pcm, 2, 110, 110);
    status = gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_APPLIED && status.applied == 3 &&
           status.acknowledged_tl[0] == 254 && status.acknowledged_tl[1] == 255);
    assert(gain_tl[0] == 254 && gain_tl[1] == 255 && query(pcm, 1).output_frames == 110);
    stop_case(pcm);

    for(unsigned fault = 0; fault < 3; ++fault) {
        pcm = open_case(TWO_SOURCES);
        assert(!snd_retained_pcm_start(pcm));
        assert(!snd_retained_pcm_gain_begin(pcm, 127, 128, &id));
        if(fault == 0)
            gain_reject_channel = 1;
        else if(fault == 1)
            gain_stale_channel = 1;
        else
            gain_unattributed_channel = 1;
        assert(snd_retained_pcm_poll(pcm) == -1 && errno == EPROTO);
        status = gain_status(pcm, id);
        assert(status.state == SND_RETAINED_GAIN_FAILED && status.admitted == 3 &&
               status.applied == (fault == 2 ? 3 : 1) &&
               status.rejected == (fault == 0 ? 2 : 0) &&
               status.unknown == (fault == 1 ? 2 : 0) && status.error == EPROTO);
        assert(!query(pcm, 1).valid && !query(pcm, 1).source_frames);
        stop_case(pcm);
    }

    for(unsigned cancel = 0; cancel < 2; ++cancel) {
        pcm = open_case(TWO_SOURCES);
        assert(!snd_retained_pcm_start(pcm));
        assert(!snd_retained_pcm_gain_begin(pcm, 127, 128, &id));
        gain_timeout_channel = 1;
        assert(!snd_retained_pcm_poll(pcm));
        status = gain_status(pcm, id);
        assert(status.state == SND_RETAINED_GAIN_PENDING && status.admitted == 3 &&
               status.applied == 1 && status.unknown == 2 && status.error == ETIMEDOUT);
        gain_timeout_channel = -1;
        if(cancel) {
            assert(!snd_retained_pcm_stop(pcm, 100));
            status = gain_status(pcm, id);
            assert(status.state == SND_RETAINED_GAIN_FAILED && status.error == ECANCELED &&
                   status.applied == 1 && status.unknown == 2);
            assert(stop_submissions[0] == 1 && stop_submissions[1] == 1);
        }
        else {
            poll_at(pcm, 0, 800, 800);
            status = gain_status(pcm, id);
            assert(status.state == SND_RETAINED_GAIN_APPLIED && status.applied == 3 &&
                   query(pcm, 1).output_frames == 800);
        }
        assert(gain_submissions[0] == 1 && gain_submissions[1] == 1);
        stop_case(pcm);
    }

    pcm = open_case(TWO_SOURCES);
    assert(!snd_retained_pcm_start(pcm));
    assert(!snd_retained_pcm_gain_begin(pcm, 127, 128, &id));
    gain_submit_error = EAGAIN;
    poll_at(pcm, 1, 40, 40);
    now += 30;
    assert(snd_retained_pcm_poll(pcm) == -1 && errno == ETIMEDOUT);
    status = gain_status(pcm, id);
    assert(status.state == SND_RETAINED_GAIN_FAILED && status.admitted == 1 &&
           status.unknown == 1 && !status.applied && !gain_submissions[1]);
    assert(!query(pcm, 1).valid && query(pcm, 1).source_frames == 40);
    stop_case(pcm);
    puts("RETAINED GAIN HOST PASS: validation, stereo handover, split failure, stale, retry and checked stop");
}

static int16_t memory_sample(unsigned frame, unsigned side) {
    return side == 0 ? (int16_t)(2048 + frame * 3) : (int16_t)(-1024 - (int)frame * 7);
}

static int16_t memory_ring_sample(unsigned frame, unsigned side) {
    int16_t value;
    memcpy(&value, sound_ram[side] + frame * sizeof(value), sizeof(value));
    return value;
}

static void test_retained_memory(void) {
    int16_t samples[701 * 2];
    for(unsigned i = 0; i < 701; ++i) {
        samples[2 * i] = memory_sample(i, 0);
        samples[2 * i + 1] = memory_sample(i, 1);
    }
    for(unsigned repeat = 0; repeat < 2; ++repeat) {
        snd_retained_pcm_t *pcm = open_case(SILENCE);
        assert(!snd_retained_pcm_close(pcm));
        frees = 0;
        assert(!snd_retained_pcm_open_memory(&pcm, 2, 44100, 1024));
        snd_retained_memory_position_t next = { UINT64_MAX, UINT32_MAX };
        assert(snd_retained_pcm_memory_position(pcm, &next) == -1 && errno == ENODATA);
        assert(next.submitted_frames == UINT64_MAX && next.next_frame == UINT32_MAX);
        assert(snd_retained_pcm_start(pcm) == -1 && errno == EINVAL && !pcm->attempted);
        assert(snd_retained_pcm_memory_bind(pcm, samples, 511, true) == -1 && errno == EINVAL);
        assert(!snd_retained_pcm_memory_bind(pcm, samples, 701, repeat));
        assert(!snd_retained_pcm_memory_position(pcm, &next) &&
               !next.submitted_frames && !next.next_frame);
        assert(snd_retained_pcm_seal(pcm, SND_RETAINED_MEMORY_GENERATION, 0) == -1 &&
               errno == EINVAL);
        assert(!snd_retained_pcm_start(pcm));
        assert(snd_retained_pcm_memory_bind(pcm, samples, 701, repeat) == -1 && errno == EBUSY);
        assert(!snd_retained_pcm_memory_position(pcm, &next));
        assert(next.submitted_frames == (repeat ? 1024 : 701) &&
               next.next_frame == (repeat ? 323 : 701));
        for(unsigned side = 0; side < 2; ++side) {
            assert(memory_ring_sample(700, side) == memory_sample(700, side));
            assert(memory_ring_sample(701, side) == (repeat ? memory_sample(0, side) : 0));
            assert(memory_ring_sample(702, side) == (repeat ? memory_sample(1, side) : 0));
        }
        poll_at(pcm, 14, 550, 550);
        assert(!snd_retained_pcm_memory_position(pcm, &next));
        assert(next.submitted_frames == (repeat ? 1536 : 701));
        if(repeat) {
            assert(next.next_frame == 134);
            now += 11;
            position[0] = position[1] = 6;
            fail_dma_channel = 1;
            assert(snd_retained_pcm_poll(pcm) == -1 && errno == EIO);
            assert(!snd_retained_pcm_memory_position(pcm, &next) &&
                   next.submitted_frames == 1536 && next.next_frame == 134);
            assert(snd_retained_pcm_poll(pcm) == -1 && errno == EIO && dmas == 8);
        } else {
            poll_at(pcm, 4, 701, 701);
            assert(query(pcm, SND_RETAINED_MEMORY_GENERATION).source_output_end);
        }
        assert(!snd_retained_pcm_stop(pcm, 100) && query(pcm, 0).software_stopped);
        assert(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY && !frees);
        assert(streams[0].service_owner == pcm);
        assert(!snd_retained_pcm_memory_position(pcm, &next));
        assert(next.submitted_frames == (repeat ? 1536 : 701));
        free(pcm); /* The test model discards its retained lease. */
    }
    puts("RETAINED MEMORY HOST PASS: tail, rewind, paired commit, error, lease");
}

int main(void) {
    test_retained_memory();
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
    test_gain();
    test_mono_gain();
    test_initial_tl();
    test_mono_frontier();
    test_stereo_gain_witness(1024, false, OLD_WINDOW);
    test_stereo_gain_witness(1024, true, OLD_WINDOW);
    test_stereo_gain_witness(8192, false, OLD_WINDOW);
    test_stereo_gain_witness(8192, true, OLD_WINDOW);
    test_stereo_gain_witness(1024, false, REPLACEMENT_WINDOW);
    test_stereo_gain_witness(1024, true, REPLACEMENT_WINDOW);
    test_stereo_gain_witness(8192, false, REPLACEMENT_WINDOW);
    test_stereo_gain_witness(8192, true, REPLACEMENT_WINDOW);
    test_stereo_gain_witness(1024, true, RETRIED_REPLACEMENT_WINDOW);
    test_stereo_gain_witness(8192, true, RETRIED_REPLACEMENT_WINDOW);
    test_stereo_gain_witness(1024, true, STALE_RETRY);
    test_stereo_gain_frontier_guards(RETAINED_SAMPLE_NORMAL);
    test_stereo_gain_frontier_guards(RETAINED_SAMPLE_GAIN_OLD);
    test_stereo_gain_frontier_guards(RETAINED_SAMPLE_GAIN_NEW);
    return 0;
}
