/* KallistiOS ##version##
   Imported long-strip grid and homogeneous clipping workload.
   Copyright (C) 2026 Joseph Black
*/

#ifndef GRID_HOST
#include <kos.h>
#endif
#include <dc/pvr_chunk_asset.h>
#include <dc/pvr_chunk_render.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_TRIANGLES 1024u
#define GRID_VERTICES 561u
#define OUTPUT_CAPACITY (GRID_TRIANGLES * PVR_FRUSTUM_CLIP_MAX_VERTICES)
#define CASES 6u

static struct {
    pvr_chunk_model_view_t model;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t *index;
    void *decoded;
    pvr_vertex_t *workspace;
    pvr_vertex_t *output;
    pvr_vertex_t *reference;
    size_t capacity;
    size_t expected[CASES];
    alignas(32) pvr_vertex_t clipped[PVR_FRUSTUM_CLIP_MAX_VERTICES];
} app;

static const char *case_names[CASES] = {
    "visible", "side-split", "side-drop", "depth-split", "depth-drop", "outside"
};
static const pvr_chunk_clip_policy_t policies[CASES] = {
    PVR_CHUNK_CLIP_ASSUME_VISIBLE, PVR_CHUNK_CLIP_SPLIT, PVR_CHUNK_CLIP_DROP,
    PVR_CHUNK_CLIP_SPLIT, PVR_CHUNK_CLIP_DROP, PVR_CHUNK_CLIP_SPLIT
};

static int require(int condition) {
    if(condition)
        return 0;
    errno = EILSEQ;
    return -1;
}

static int load(const void *bytes, size_t count) {
    pvr_chunk_asset_view_t asset;
    pvr_chunk_asset_workspace_requirements_t req;
    pvr_chunk_model_plan_requirements_t plan_req;
    if(pvr_chunk_asset_open(bytes, count, &asset) < 0 ||
       pvr_chunk_asset_workspace_query(&asset, &req) < 0)
        return -1;
    if(req.bytes) {
        if(req.bytes > SIZE_MAX - 31u) {
            errno = ERANGE;
            return -1;
        }
        app.decoded = aligned_alloc(32, (req.bytes + 31u) & ~(size_t)31u);
        if(!app.decoded)
            return -1;
    }
    if(pvr_chunk_asset_load(&asset, NULL, NULL, app.decoded, req.bytes,
                             &app.model) < 0 ||
       require(app.model.info.triangles == GRID_TRIANGLES &&
               app.model.info.vertex_entries == GRID_VERTICES &&
               app.model.info.maximum_strip_vertices > 3 &&
               app.model.info.maximum_strip_vertices <= GRID_TRIANGLES + 2) < 0 ||
       pvr_chunk_model_plan_query(&app.model, &plan_req) < 0 ||
       require(plan_req.vertex_index_entries <= 1024) < 0)
        return -1;
    app.index = calloc(plan_req.vertex_index_entries, sizeof(*app.index));
    app.capacity = app.model.info.maximum_strip_vertices;
    app.workspace = aligned_alloc(32, app.capacity * sizeof(*app.workspace));
    app.output = aligned_alloc(32, (OUTPUT_CAPACITY + 1) * sizeof(*app.output));
    app.reference = aligned_alloc(32, (OUTPUT_CAPACITY + 1) * sizeof(*app.reference));
    if(!app.index || !app.workspace || !app.output || !app.reference ||
       pvr_chunk_model_plan_build(&app.model, app.index,
           plan_req.vertex_index_entries, &app.plan) < 0)
        return -1;
    printf("KOSGRID vertices=%u triangles=%u strip_max=%zu\n",
           GRID_VERTICES, GRID_TRIANGLES, app.capacity);
    return 0;
}

static int frustum_for(unsigned which, pvr_frustum_t *frustum) {
    alignas(32) static const matrix_t ortho = {
        { 16, 0, 0, 0 }, { 0, 20, 0, 0 }, { 0, 0, 1, 0 }, { 64, 80, 0, 1 }
    };
    /* W=.25+x/16; X=320W+16(x-16); Y=240W+16(y-8).
       Clipping happens in homogeneous space, before dividing by W. */
    alignas(32) static const matrix_t depth = {
        { 36, 15, 0, 0.0625f }, { 0, 16, 0, 0 },
        { 0, 0, 1, 0 }, { -176, -68, 0, 0.25f }
    };
    if(which == 1 || which == 2)
        return pvr_frustum_init(frustum, &ortho, 196, 125, 444, 355, 0.5f, 2);
    if(which == 3 || which == 4)
        return pvr_frustum_init(frustum, &depth, 64, 64, 576, 416,
                                1.015625f, 2.015625f);
    if(which == 5)
        return pvr_frustum_init(frustum, &ortho, 2000, 0, 3000, 480, 0.5f, 2);
    return pvr_frustum_init(frustum, &ortho, 0, 0, 640, 480, 0.5f, 2);
}

static int color_vertex(const pvr_chunk_render_state_t *state,
                        const pvr_chunk_vertex_attributes_t *attributes,
                        const pvr_chunk_strip_attributes_t *strip,
                        pvr_vertex_t *vertex, void *data) {
    (void)state;
    (void)attributes;
    (void)strip;
    (void)data;
    if(require(isfinite(vertex->x) && isfinite(vertex->y) &&
               vertex->x >= 0 && vertex->x <= 32 &&
               vertex->y >= 0 && vertex->y <= 16) < 0)
        return -1;
    vertex->argb = UINT32_C(0xff000040) |
        ((uint32_t)(vertex->x * 7.0f + 0.5f) << 16) |
        ((uint32_t)(vertex->y * 14.0f + 0.5f) << 8);
    vertex->oargb = 0;
    return 0;
}

static int emit(unsigned which, int prepared, pvr_geometry_sink_t *sink,
                pvr_chunk_render_begin_strip_t begin,
                pvr_chunk_render_result_t *result) {
    pvr_frustum_t frustum;
    if(frustum_for(which, &frustum) < 0)
        return -1;
    if(prepared)
        return pvr_chunk_model_emit_clipped_prepared(&app.plan, &frustum,
            policies[which], sink, app.workspace, app.capacity, app.clipped,
            PVR_FRUSTUM_CLIP_MAX_VERTICES, begin, color_vertex, NULL, result);
    return pvr_chunk_model_emit_clipped(&app.model, &frustum, policies[which],
        sink, app.workspace, app.capacity, app.clipped,
        PVR_FRUSTUM_CLIP_MAX_VERTICES, begin, color_vertex, NULL, result);
}

/* Independent trapezoid calculation: no KOS transform or clipper calls. */
static double depth_area(double left, double right) {
    double wl = 0.25 + left / 16.0, wr = 0.25 + right / 16.0;
    double xl = 320.0 + 16.0 * (left - 16.0) / wl;
    double xr = 320.0 + 16.0 * (right - 16.0) / wr;
    return (256.0 / wl + 256.0 / wr) * (xr - xl) / 2.0;
}

static int check_packets(unsigned which, size_t count) {
    double area = 0;
    size_t run = 0, triangles = 0;
    double expected = which == 0 ? 512.0 * 320.0 :
        which == 1 ? 248.0 * 230.0 : which == 2 ? 224.0 * 200.0 :
        which == 3 ? depth_area(12.25, 28.25) :
        which == 4 ? depth_area(13, 28) : 0;
    for(size_t i = 0; i < count; ++i) {
        const pvr_vertex_t *v = &app.output[i];
        if(require(isfinite(v->x) && isfinite(v->y) && isfinite(v->z) && v->z > 0 &&
                   (v->flags == PVR_CMD_VERTEX || v->flags == PVR_CMD_VERTEX_EOL)) < 0)
            return -1;
        double x = (v->x - 64.0) / 16.0, y = (v->y - 80.0) / 20.0;
        if(which == 3 || which == 4) {
            double w = 1.0 / v->z;
            x = 16.0 * (w - 0.25);
            y = (v->y - 240.0) * w / 16.0 + 8.0;
            if(require(w >= 1.015615 && w <= 2.015635) < 0)
                return -1;
        }
        if(which == 1 || which == 2)
            if(require(v->x >= 195.995f && v->x <= 444.005f &&
                       v->y >= 124.995f && v->y <= 355.005f) < 0)
                return -1;
        if(require(x >= -0.001 && x <= 32.001 && y >= -0.001 && y <= 16.001 &&
                   fabs((double)((v->argb >> 16) & 255) - 7.0 * x) <= 1.1 &&
                   fabs((double)((v->argb >> 8) & 255) - 14.0 * y) <= 1.1 &&
                   (v->argb & UINT32_C(0xff0000ff)) == UINT32_C(0xff000040) &&
                   v->oargb == 0) < 0)
            return -1;
        if(run >= 2) {
            const pvr_vertex_t *a = v - 2, *b = v - 1;
            area += fabs(((double)b->x - a->x) * ((double)v->y - a->y) -
                         ((double)b->y - a->y) * ((double)v->x - a->x)) / 2.0;
            ++triangles;
        }
        ++run;
        if(v->flags == PVR_CMD_VERTEX_EOL) {
            if(require(run >= 3) < 0)
                return -1;
            run = 0;
        }
    }
    if(require(run == 0 && fabs(area - expected) <= 0.2 &&
               (which != 0 || triangles == GRID_TRIANGLES) &&
               (which != 2 || triangles == 280) &&
               (which != 4 || triangles == 480) &&
               (which != 5 || count == 0)) < 0)
        return -1;
    printf("KOSGRID case=%s packets=%zu triangles=%zu area=%.3f expected=%.3f PASS\n",
           case_names[which], count, triangles, area, expected);
    return 0;
}

static int check(void) {
    for(unsigned c = 0; c < CASES; ++c) {
        pvr_geometry_sink_t sink;
        pvr_chunk_render_result_t result, reference;
        memset(app.output, 0xa5, (OUTPUT_CAPACITY + 1) * sizeof(*app.output));
        if(pvr_geometry_sink_init_memory(&sink, app.output, OUTPUT_CAPACITY) < 0 ||
           emit(c, 1, &sink, NULL, &result) < 0 ||
           require(result.emitted_vertices == sink.emitted_vertices) < 0 ||
           check_packets(c, sink.emitted_vertices) < 0)
            return -1;
        app.expected[c] = sink.emitted_vertices;
        const unsigned char *tail = (const unsigned char *)&app.output[sink.emitted_vertices];
        for(size_t byte = 0; byte < sizeof(*app.output); ++byte)
            if(require(tail[byte] == 0xa5) < 0)
                return -1;
        if(pvr_geometry_sink_init_memory(&sink, app.reference, OUTPUT_CAPACITY) < 0 ||
           emit(c, 0, &sink, NULL, &reference) < 0 ||
           require(result.consumed_records == reference.consumed_records &&
                   result.emitted_strips == reference.emitted_strips &&
                   result.emitted_vertices == reference.emitted_vertices &&
                   !memcmp(app.output, app.reference,
                           app.expected[c] * sizeof(*app.output))) < 0)
            return -1;
    }
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    memset(app.output, 0xa5, sizeof(*app.output));
    if(pvr_geometry_sink_init_memory(&sink, app.output, 1) < 0)
        return -1;
    errno = 0;
    if(require(emit(1, 1, &sink, NULL, &result) == -1 && errno == ENOSPC &&
               sink.emitted_vertices == 0) < 0)
        return -1;
    for(size_t b = 0; b < sizeof(*app.output); ++b)
        if(require(((unsigned char *)app.output)[b] == 0xa5) < 0)
            return -1;
    puts("KOSGRID parity=PASS guard=PASS short_sink=PASS");
    return 0;
}

#ifndef GRID_HOST
static pvr_poly_hdr_t header;
static int begin(const pvr_chunk_render_state_t *state,
                 const pvr_chunk_strip_view_t *strip, void *data) {
    (void)state;
    (void)strip;
    (void)data;
    return pvr_prim(&header, sizeof(header));
}

static int render(void) {
    pvr_poly_cxt_t context;
    pvr_geometry_sink_t sink;
    pvr_pipeline_status_t pipeline;
    int scene_open = 0, list_open = 0, error;
    if(pvr_init_defaults() < 0)
        return -1;
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &context);
    if(pvr_geometry_sink_init_current(&sink) < 0)
        goto fail;
    for(unsigned c = 0; c < CASES; ++c) {
        uint64_t total = 0;
        for(unsigned frame = 0; frame < 20; ++frame) {
            pvr_chunk_render_result_t result;
            if(pvr_wait_ready() < 0)
                goto fail;
            pvr_scene_begin();
            scene_open = 1;
            if(pvr_list_begin(PVR_LIST_OP_POLY) < 0)
                goto fail;
            list_open = 1;
            uint64_t start = timer_us_gettime64();
            if(emit(c, 1, &sink, begin, &result) < 0)
                goto fail;
            uint64_t elapsed = timer_us_gettime64() - start;
            if(frame >= 4)
                total += elapsed;
            if(require(result.emitted_vertices == app.expected[c]) < 0 ||
               pvr_list_finish() < 0)
                goto fail;
            list_open = 0;
            if(pvr_scene_finish() < 0)
                goto fail;
            scene_open = 0;
        }
        if(pvr_wait_render_done() < 0 ||
           pvr_get_pipeline_status(&pipeline) < 0 ||
           require(pipeline.faults.mask == PVR_FAULT_NONE) < 0)
            goto fail;
        printf("KOSGRID render=%s frames=20 samples=16 mean_emit_us=%llu PASS\n",
               case_names[c], (unsigned long long)(total / 16));
    }
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

int main(int argc, char **argv) {
    const void *data;
    size_t bytes;
    int result = 1, error;
#ifdef GRID_HOST
    FILE *file;
    long length;
    void *storage = NULL;
    if(argc != 2 || !(file = fopen(argv[1], "rb")))
        return 1;
    if(fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
       fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 1;
    }
    bytes = (size_t)length;
    if(bytes > SIZE_MAX - 31u) {
        fclose(file);
        return 1;
    }
    storage = aligned_alloc(32, (bytes + 31u) & ~(size_t)31u);
    if(!storage || fread(storage, 1, bytes, file) != bytes) {
        fclose(file);
        free(storage);
        return 1;
    }
    fclose(file);
    data = storage;
#else
    extern const unsigned char grid_asset_data[];
    extern const int grid_asset_size;
    (void)argc;
    (void)argv;
    data = grid_asset_data;
    bytes = (size_t)grid_asset_size;
#endif
    if(load(data, bytes) < 0 || check() < 0)
        goto out;
#ifndef GRID_HOST
    /* Only counts are needed after preflight; don't retain the large oracle
       buffers as rendering overhead. Final cleanup accepts the NULL slots. */
    free(app.output);
    free(app.reference);
    app.output = app.reference = NULL;
    if(render() < 0)
        goto out;
#endif
    result = 0;
out:
    error = errno;
    free(app.index);
    free(app.decoded);
    free(app.workspace);
    free(app.output);
    free(app.reference);
#ifdef GRID_HOST
    free(storage);
#else
    vid_clear(result ? 64 : 0, result ? 0 : 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT,
                   vid_mode->width, 1, result ? "GRID FAIL" : "GRID PASS");
#endif
    printf("KOSGRID result=%s errno=%d\n", result ? "FAIL" : "PASS", result ? error : 0);
#ifndef GRID_HOST
    for(;;)
        thd_sleep(1000);
#endif
    return result;
}
