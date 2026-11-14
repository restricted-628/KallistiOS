static void reset(void) {
    memset(streams, 0, sizeof(streams));
    streams[0].initted = true; streams[0].channels = 1;
    streams[0].ch[1] = -1; streams[0].spu_ram_sch[0] = 0x40000;
    streams[0].dma_pending = 1;
    TAILQ_INIT(&streams[0].filters);
    ticks = 0; stop_error = barrier_error = dma_error = status_error = 0;
    playing = fetch_live = false;
    mem_frees = ch_frees = heap_frees = joins = gates = releases = 0;
    trace_size = 0; trace[0] = 0; self_context = finish_service = false;
    assert(!stream_state_mutex);
}
static void retained(int expected) {
    assert(snd_stream_destroy_ex(0, 3) < 0 && errno == expected);
    assert(streams[0].initted && !mem_frees && !ch_frees);
    assert(!stream_state_mutex);
}
int main(void) {
    reset(); streams[0].service_owner = &streams[0]; retained(EBUSY);
    assert(!trace_size); puts("STREAM: service ownership retains allocation PASS");
    reset(); streams[0].callback_active = 1; retained(EDEADLK);
    assert(!trace_size); puts("STREAM: callback reentry retains allocation PASS");
    reset(); stop_error = ENOSPC; retained(ENOSPC);
    assert(!strcmp(trace, "GSR") && gates == releases);
    puts("STREAM: stop admission failure releases queue gate, retains allocation PASS");
    reset(); barrier_error = ETIMEDOUT; retained(ETIMEDOUT);
    assert(!strcmp(trace, "GSRB")); puts("STREAM: FIFO barrier failure retains allocation PASS");
    reset(); dma_error = ETIMEDOUT; retained(ETIMEDOUT);
    assert(!strcmp(trace, "GSRBD") && streams[0].dma_pending);
    puts("STREAM: DMA timeout retains allocation PASS");
    reset(); status_error = EIO; retained(EIO);
    assert(!strcmp(trace, "GSRBDP")); puts("STREAM: channel status failure retains allocation PASS");
    reset(); playing = true; retained(ETIMEDOUT);
    assert(ticks == 3); puts("STREAM: playing timeout retains allocation PASS");
    reset(); assert(snd_stream_destroy_ex(0, 3) == 0);
    assert(!strcmp(trace, "GSRBDPCM") && mem_frees == 1 && ch_frees == 1);
    puts("STREAM: barrier then DMA then channel then free order PASS");
    reset(); fetch_live = true; playing = false;
    assert(snd_stream_destroy_ex(0, 3) == 0 && mem_frees == 1 && fetch_live);
    puts("STREAM: EXPECTED MODEL GAP: key-off permits free with independent fetch-live state");

    reset(); snd_stream_service_t *s = calloc(1, sizeof(*s)); assert(s);
    s->thread = &service_thread; s->state = SND_STREAM_SERVICE_RUNNING;
    self_context = true;
    assert(snd_stream_service_destroy(s, 3) < 0 && errno == EDEADLK);
    assert(s->state == SND_STREAM_SERVICE_RUNNING && !joins && !heap_frees);
    puts("SERVICE: self-context refusal retains service PASS");
    self_context = false;
    assert(snd_stream_service_destroy(s, 3) < 0 && errno == ETIMEDOUT);
    assert(s->state == SND_STREAM_SERVICE_STOPPING && !joins && !heap_frees);
    puts("SERVICE: timeout retains stopping service and dependencies PASS");
    finish_service = true; trace_size = 0; trace[0] = 0;
    assert(snd_stream_service_destroy(s, 3) == 0);
    assert(joins == 1 && heap_frees == 1 && !strcmp(trace, "WTJscmF"));
    puts("SERVICE: successful retry joins before destroying dependencies PASS");
    puts("STREAM/SERVICE: PASS 11 control-flow cases; 1 explicit release-tail model gap");
    return 0;
}
