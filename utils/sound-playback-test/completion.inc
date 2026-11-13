/* Exercise actual host/ARM code. Only transport/register inputs are injected. */
static unsigned corrupt_mode;
static void corrupt_completion(unsigned ch) {
    switch(corrupt_mode) {
        case 1: completions[ch].start_token += 1000; break;
        case 2: completions[ch].command_token += 1000; break;
        case 3: completions[ch].flags |= 0x80; break;
        case 4: completions[ch].challenge_cookie = 0; break;
        case 5: completion_header.layout_version = 99; break;
        case 6: completions[ch].session += 1; break;
        case 7: completions[ch].reserved[1] = 1; break;
        case 8: completions[ch].flags = AICA_COMPLETION_END_SEEN; completions[ch].loop_enabled = 1; break;
        case 9: completions[ch].flags = AICA_COMPLETION_END_SEEN; break;
    }
}
static snd_port_t completion_port(void) {
    pb_test_reset(); driver_features |= SND_DRIVER_FEATURE_COMPLETION;
    return new_port();
}
static int completion_error(snd_port_t p, unsigned mode, int expected,
                            snd_port_playback_state_t *state) {
    static const char *names[] = {"unknown flags", "stale challenge", "layout",
        "session", "reserved", "loop end", "ineligible active end"};
    unsigned timeout = mode == 4 ? 15 : PROTOCOL_WAIT_MS;
    uint64_t began = timer_ms_gettime64();
    int rc = snd_port_playback_get_state(p, timeout, state), error = errno;
    if(rc != -1 || error != expected)
        fprintf(stderr, "completion mode=%u case=%s port=%u start=%u operation=get_state "
                "expected_rc=-1 actual_rc=%d expected_errno=%d actual_errno=%d "
                "elapsed_ms=%llu budget_ms=%u\n", mode, names[mode - 3], p,
                pb_states[SND_PORT_SLOT_INDEX(p)].start_token, rc, expected, error,
                (unsigned long long)(timer_ms_gettime64() - began), timeout);
    errno = error;
    return rc == -1 && error == expected;
}
static void completion_tests(void) {
    snd_port_operation_t op, saved_op = {99,88,77};
    snd_port_playback_state_t state, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    snd_port_t p = completion_port(); unsigned idx = SND_PORT_SLOT_INDEX(p);
    unsigned ch = pb_states[idx].channel;
    state = sentinel;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EAGAIN &&
          !memcmp(&state, &sentinel, sizeof(state)));
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EAGAIN);
    /* End before first host poll, including valid first_end_tick0 in ARM suite. */
    lp_on_start = 1;
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 1);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EBUSY);
    settle(&op);
    unsigned start = op.token, reads_before = lp_reads;
    CHECK(ports[idx].active && ports[idx].priority == 8);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 &&
          state.start_token == start && state.state == SND_PLAYBACK_ACTIVE &&
          (state.flags & SND_PLAYBACK_END_SEEN) && !ports[idx].active && !ports[idx].priority);
    CHECK(lp_reads == reads_before && pb_states[idx].control == PB_ACTIVE);
    CHECK(snd_port_playback_detach(p) == -1 && errno == EBUSY);
    /* Fixture sentinel proves a second query does not apply reset twice. */
    ports[idx].active = true; ports[idx].priority = 7;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && ports[idx].priority == 7);
    ports[idx].active = false; ports[idx].priority = 0;
    CHECK(snd_port_playback_update(p, &config, SND_CHANNEL_UPDATE_VOLUME, &op) == 0);
    settle(&op);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && state.start_token == start &&
          state.command_token == op.token && (state.flags & SND_PLAYBACK_END_SEEN));
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 &&
          state.state == SND_PLAYBACK_STOPPED && state.start_token == start &&
          state.flags == SND_PLAYBACK_END_SEEN);
    CHECK(snd_port_playback_detach(p) == 0 && snd_port_close(p) == 0);
    /* Channel history cannot be adopted by a new attachment, including STOP. */
    p = new_port(); idx = SND_PORT_SLOT_INDEX(p);
    CHECK(pb_states[idx].channel == ch && completions[ch].start_token == start);
    state = sentinel;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EAGAIN &&
          !memcmp(&state, &sentinel, sizeof(state)));
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EAGAIN);
    lp_on_start = 0;
    CHECK(snd_port_playback_start(p, &config, 15, &op) == 1); settle(&op);
    start = op.token;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && state.start_token == start &&
          !(state.flags & SND_PLAYBACK_END_SEEN) && ports[idx].priority == 15);
    /* End after ACK: new lower-priority admission obtains a fresh end record. */
    lp_bits[ch] = 1;
    CHECK(snd_port_playback_start(p, &config, 1, &op) == 1 && ports[idx].pending &&
          !ports[idx].active);
    settle(&op); CHECK(ports[idx].priority == 1 && ports[idx].active);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && state.start_token == op.token &&
          state.start_token != start && !(state.flags & SND_PLAYBACK_END_SEEN));
    /* Rejected replacement preserves current generation/end policy. */
    start = op.token; apply_result = AICA_OBSERVATION_BAD_CONFIG;
    CHECK(snd_port_playback_start(p, &config, 2, &op) == 1); settle(&op);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && state.start_token == start &&
          ports[idx].priority == 1);
    apply_result = AICA_OBSERVATION_APPLIED;
    /* A delayed late record cannot retire the replacement. */
    after_sample = corrupt_completion; corrupt_mode = 1; state = sentinel;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EBUSY &&
          !memcmp(&state, &sentinel, sizeof(state)) && ports[idx].priority == 1 &&
          pb_states[idx].control == PB_INTERFERED);
    after_sample = NULL;
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_detach(p) == 0);
    /* Legacy interference can report historical end, but never resets priority. */
    p = completion_port(); idx = SND_PORT_SLOT_INDEX(p); ch = pb_states[idx].channel;
    lp_on_start = 1;
    CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
    aica_observer_legacy(&arm, ch);
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 &&
          state.state == SND_PLAYBACK_INTERFERED && state.flags == SND_PLAYBACK_END_SEEN &&
          ports[idx].priority == 9 && pb_states[idx].control == PB_INTERFERED);
    op = saved_op;
    CHECK(snd_port_playback_start(p, &config, 10, &op) == -1 && errno == EBUSY &&
          !memcmp(&op, &saved_op, sizeof(op)));
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_detach(p) == 0);
    /* Foreign managed UPDATE is an attribution conflict, not malformed protocol. */
    p = completion_port(); idx = SND_PORT_SLOT_INDEX(p);
    CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
    after_sample = corrupt_completion; corrupt_mode = 2; state = sentinel;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EBUSY &&
          !memcmp(&state, &sentinel, sizeof(state)) && ports[idx].priority == 9 &&
          pb_states[idx].control == PB_INTERFERED);
    after_sample = NULL;
    CHECK(snd_port_playback_stop(p, &op) == 0); settle(&op);
    CHECK(snd_port_playback_detach(p) == 0);
    /* Loop/unsupported format events do not reset, even with an LP input. */
    for(unsigned mode = 0; mode < 3; ++mode) {
        p = completion_port(); idx = SND_PORT_SLOT_INDEX(p);
        config.loop_enabled = mode == 0;
        config.format = mode == 0 ? SND_CHANNEL_SAMPLE_PCM16 : mode;
        lp_on_start = 1;
        CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
        CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == 0 && !state.flags &&
              ports[idx].priority == 9 && !lp_reads);
    }
    /* Malformed/header incompatibility, stale challenge and session loss. */
    const int expected[] = {EPROTO, ETIMEDOUT, EPROTO, ENODEV, EPROTO, EPROTO, EPROTO};
    for(unsigned mode = 3; mode <= 9; ++mode) {
        p = completion_port(); idx = SND_PORT_SLOT_INDEX(p);
        CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
        after_sample = corrupt_completion; corrupt_mode = mode; state = sentinel;
        CHECK(completion_error(p, mode, expected[mode-3], &state) &&
              !memcmp(&state, &sentinel, sizeof(state)) && ports[idx].priority == 9);
    }
    for(unsigned which = 0; which < 2; ++which) {
        p = completion_port(); idx = SND_PORT_SLOT_INDEX(p); ch = pb_states[idx].channel;
        CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
        if(which) completion_header.sequence = UINT32_MAX;
        else completions[ch].sequence = UINT32_MAX - 1;
        state = sentinel;
        CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EOVERFLOW &&
              !memcmp(&state, &sentinel, sizeof(state)) && ports[idx].priority == 9);
        CHECK(which || completions[ch].sequence == UINT32_MAX);
    }
    p = completion_port(); idx = SND_PORT_SLOT_INDEX(p);
    CHECK(snd_port_playback_start(p, &config, 9, &op) == 1); settle(&op);
    CHECK(snd_port_playback_get_state(p, 0, &state) == -1 && errno == EINVAL);
    state = sentinel; paused = true;
    CHECK(snd_port_playback_get_state(p, 5, &state) == -1 && errno == ETIMEDOUT &&
          !memcmp(&state, &sentinel, sizeof(state)) && ports[idx].priority == 9);
    paused = false; drain();
    struct holder h; pthread_t thread = start_holder(&h, &queue_proc_mutex, 80);
    CHECK(snd_port_playback_get_state(p, 10, &state) == -1 && errno == ETIMEDOUT &&
          ports[idx].priority == 9); pthread_join(thread, NULL);
    driver_features = SND_DRIVER_FEATURE_OBSERVATION;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == ENOTSUP);
    lp_bits[pb_states[idx].channel] = 1; op = saved_op;
    CHECK(snd_port_playback_start(p, &config, 8, &op) == 0 &&
          !memcmp(&op, &saved_op, sizeof(op)) && ports[idx].priority == 9);
    driver_features |= SND_DRIVER_FEATURE_COMPLETION;
    cookie = UINT32_MAX;
    CHECK(snd_port_playback_get_state(p, PROTOCOL_WAIT_MS, &state) == -1 && errno == EOVERFLOW);
    CHECK(ports[idx].priority == 9);
    uint32_t buf[16];
    CHECK(snd_iface_observation_read(AICA_MEM_COMPLETION_HEADER + 64, buf, 64) == -1 && errno == EINVAL);
    CHECK(snd_iface_observation_read(AICA_MEM_COMPLETION_END, buf, 64) == -1 && errno == EINVAL);
    CHECK(snd_iface_observation_read(AICA_MEM_COMPLETION_CHANNELS, buf, 32) == -1 && errno == EINVAL);
    CHECK(timed_zero == 0);
}
