/* KallistiOS ##version##
   Example-private authored-asset workload, shared by host and target.
   Copyright (C) 2026 Joseph Black
*/

#define WORKLOAD_WARMUP 8u
#define WORKLOAD_FRAMES 24u

typedef struct workload_times {
    uint64_t pose;
    uint64_t draw;
} workload_times_t;

static uint64_t workload_clock(void) {
#ifdef CHUNK_SCENE_HOST
    return 0; /* Host lane checks results, not performance. */
#else
    return timer_us_gettime64();
#endif
}

static int workload_shade(const pvr_chunk_render_state_t *state,
                          uint16_t index, const pvr_deform_vertex_t *vertex,
                          pvr_vertex_t *output, void *data) {
    static const pvr_light_t light = {
        .kind = PVR_LIGHT_DIRECTIONAL,
        .source.direction = { .z = 1.0f },
        .color = { .x = 1, .y = 1, .z = 1 },
        .intensity = 0.5f
    };
    static const pvr_lighting_context_t lighting = {
        { 0.25f, 0.25f, 0.25f }, &light, 1
    };
    const uint32_t color = output->argb;
    const pvr_lighting_sample_t input = {
        vertex->position, vertex->normal,
        { ((color >> 16) & 255u) / 255.0f,
          ((color >> 8) & 255u) / 255.0f,
          (color & 255u) / 255.0f, (color >> 24) / 255.0f }
    };
    const pvr_lighting_stream_t stream = { &input, 1, sizeof(input) };
    pvr_lighting_result_t result;
    (void)state;
    (void)index;
    (void)data;
    if(pvr_lighting_apply(&output->argb, 1, &stream, &lighting, &result) < 0)
        return -1;
    return require(result.shaded_samples == 1);
}

/* An independent packed-color oracle for the authored +Z unit normals:
   .25 ambient + .5 diffuse, with round-to-nearest channel packing. */
static uint32_t workload_color(uint32_t input) {
    uint32_t result = input & UINT32_C(0xff000000);
    for(unsigned shift = 0; shift < 24; shift += 8)
        result |= ((3u * ((input >> shift) & 255u) + 2u) / 4u) << shift;
    return result;
}

/* Reuse the two loaded meshes. Shared mode samples once for the whole grid;
   independent mode samples each pair at its own phase, reusing scratch after
   submission. Neither mode reloads assets or allocates in the frame loop. */
static int workload_frame(unsigned pairs, int independent, unsigned frame,
                          int verify, pvr_geometry_sink_t *target,
                          workload_times_t *times) {
    alignas(32) pvr_vertex_t workspace[VERTICES];
    unsigned columns = pairs == 1 ? 1 : pairs == 16 ? 4 : 16;
    const float scale = 80.0f / (float)columns;
    uint64_t start;
    size_t draws = 0;

    if(require(pairs == 1 || pairs == 16 || pairs == 256) < 0)
        return -1;
    memset(times, 0, sizeof(*times));
    for(unsigned pair = 0; pair < pairs; ++pair) {
        if(independent || pair == 0) {
            float time = (float)((frame * 7u +
                         (independent ? pair * 13u : 0u)) % 240u) / 120.0f;
            start = workload_clock();
            if(sample(time) < 0)
                return -1;
            times->pose += workload_clock() - start;
        }
        for(size_t i = 0; i < MODELS; ++i) {
            const float x = ((float)(pair % columns) +
                             (i ? 0.70f : 0.20f)) * 640.0f / columns;
            const float y = ((float)(pair / columns) + 0.5f) * 480.0f / columns;
            alignas(32) matrix_t screen = {
                { scale, 0, 0, 0 }, { 0, -scale, 0, 0 },
                { 0, 0, 1, 0 }, { x, y, 1, 1 }
            };
            alignas(32) pvr_vertex_t output[VERTICES + 1];
            pvr_geometry_sink_t memory;
            pvr_chunk_cache_result_t emitted;
            pvr_chunk_cache_begin_strip_t begin = NULL;
            model_state_t *m = &app.model[i];
            if(verify) {
                memset(output, 0xa5, sizeof(output));
                if(pvr_geometry_sink_init_memory(&memory, output, VERTICES) < 0)
                    return -1;
                target = &memory;
            }
#ifndef CHUNK_SCENE_HOST
            else
                begin = begin_strip;
#endif
            start = workload_clock();
            if(pvr_chunk_model_cache_draw_emit(&m->draw, &screen, target,
                   workspace, VERTICES, NULL, begin, resolve, workload_shade,
                   m, &emitted) < 0)
                return failure("workload-draw");
            times->draw += workload_clock() - start;
            if(require(emitted.emitted_strips == 1 &&
                       emitted.emitted_vertices == VERTICES &&
                       emitted.skipped_strips == 0) < 0)
                return failure("workload-progress");
            ++draws;
            if(verify) {
                for(size_t v = 0; v < VERTICES; ++v) {
                    pvr_deform_vertex_t posed;
                    if(resolve(m->cache.source_indices[v], &posed, m) < 0 ||
                       require(fabsf(output[v].x - (scale * posed.position.x + x)) < 0.001f &&
                               fabsf(output[v].y - (-scale * posed.position.y + y)) < 0.001f &&
                               output[v].z == 1.0f &&
                               output[v].argb == workload_color(m->cache.vertices[v].argb) &&
                               output[v].flags == (v == VERTICES - 1 ?
                                   PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX)) < 0)
                        return failure("workload-packet");
                }
                const unsigned char *guard = (const unsigned char *)&output[VERTICES];
                for(size_t byte = 0; byte < sizeof(output[VERTICES]); ++byte)
                    if(require(guard[byte] == 0xa5) < 0)
                        return failure("workload-guard");
            }
        }
    }
    return require(draws == pairs * MODELS);
}

static int workload_check(void) {
    static const unsigned counts[] = { 1, 16, 256 };
    workload_times_t times;
    for(size_t c = 0; c < 3; ++c)
        for(int mode = 0; mode < 2; ++mode)
            for(unsigned frame = 0; frame < 3; ++frame)
                if(workload_frame(counts[c], mode, frame * 11u, 1, NULL,
                                  &times) < 0)
                    return -1;
    puts("KOSSCENE workload_checks=18 packet_guards=PASS lighting=PASS");
    return 0;
}

#if defined(CHUNK_SCENE_WORKLOAD) && !defined(CHUNK_SCENE_HOST)
static int workload_compare_time(const void *a, const void *b) {
    uint64_t lhs = *(const uint64_t *)a, rhs = *(const uint64_t *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static void workload_report(unsigned pairs, int mode, const char *stage,
                            uint64_t values[WORKLOAD_FRAMES]) {
    qsort(values, WORKLOAD_FRAMES, sizeof(*values), workload_compare_time);
    printf("KOSWORKLOAD pairs=%u pose=%s stage=%s samples=%u "
           "min_us=%llu median_us=%llu max_us=%llu\n", pairs,
           mode ? "independent" : "shared", stage, WORKLOAD_FRAMES,
           (unsigned long long)values[0],
           (unsigned long long)(values[WORKLOAD_FRAMES / 2 - 1] +
               (values[WORKLOAD_FRAMES / 2] - values[WORKLOAD_FRAMES / 2 - 1]) / 2),
           (unsigned long long)values[WORKLOAD_FRAMES - 1]);
}

static int render_workload(void) {
    static const unsigned counts[] = { 1, 16, 256 };
    pvr_poly_cxt_t context;
    pvr_geometry_sink_t sink;
    pvr_pipeline_status_t pipeline;
    int scene_open = 0, list_open = 0, error;
    uint64_t pose[WORKLOAD_FRAMES], draw[WORKLOAD_FRAMES];
    uint64_t ready[WORKLOAD_FRAMES], frame_time[WORKLOAD_FRAMES];

    if(pvr_init_defaults() < 0)
        return -1;
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&draw_header, &context);
    if(pvr_geometry_sink_init_current(&sink) < 0)
        goto fail;
    for(size_t c = 0; c < 3; ++c) {
        for(int mode = 0; mode < 2; ++mode) {
            for(unsigned f = 0; f < WORKLOAD_WARMUP + WORKLOAD_FRAMES; ++f) {
                workload_times_t times;
                uint64_t start = workload_clock();
                if(pvr_wait_ready() < 0)
                    goto fail;
                uint64_t waited = workload_clock() - start;
                start = workload_clock();
                pvr_scene_begin();
                scene_open = 1;
                if(pvr_list_begin(PVR_LIST_OP_POLY) < 0)
                    goto fail;
                list_open = 1;
                if(workload_frame(counts[c], mode, f, 0, &sink, &times) < 0 ||
                   pvr_list_finish() < 0)
                    goto fail;
                list_open = 0;
                if(pvr_scene_finish() < 0)
                    goto fail;
                scene_open = 0;
                uint64_t elapsed = workload_clock() - start;
                if(f >= WORKLOAD_WARMUP) {
                    unsigned n = f - WORKLOAD_WARMUP;
                    pose[n] = times.pose;
                    draw[n] = times.draw;
                    ready[n] = waited;
                    frame_time[n] = elapsed;
                }
            }
            uint64_t start = workload_clock();
            if(pvr_wait_render_done() < 0 ||
               pvr_get_pipeline_status(&pipeline) < 0 ||
               require(pipeline.faults.mask == PVR_FAULT_NONE) < 0)
                goto fail;
            uint64_t drained = workload_clock() - start;
            printf("KOSWORKLOAD pairs=%u pose=%s triangles=%u vertices=%u "
                   "warmup=%u frames=%u drain_us=%llu\n", counts[c],
                   mode ? "independent" : "shared", counts[c] * 2,
                   counts[c] * 6, WORKLOAD_WARMUP, WORKLOAD_FRAMES,
                   (unsigned long long)drained);
            workload_report(counts[c], mode, "pose", pose);
            workload_report(counts[c], mode, "draw", draw);
            workload_report(counts[c], mode, "ready", ready);
            workload_report(counts[c], mode, "cpu-frame", frame_time);
        }
    }
    puts("KOSWORKLOAD cases=6 frames=192 result=PASS");
    thd_sleep(10000);
    return pvr_shutdown();
fail:
    error = errno;
    if(list_open)
        pvr_list_finish();
    if(scene_open)
        pvr_scene_finish();
    pvr_wait_render_done();
    pvr_shutdown();
    errno = error;
    return -1;
}
#endif
