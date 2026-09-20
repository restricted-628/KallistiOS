/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Shared checked/admitted wire rendering equivalence tests.
*/
#ifndef WIRE_DRAW_FIXTURES_H
#define WIRE_DRAW_FIXTURES_H
#include <dc/pvr_chunk_wire.h>
#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#ifdef WIRE_COUNT_PROJECTION
extern size_t wire_projected_vertices;
#endif

#define WIRE_CHECK(condition) do { \
    if(!(condition)) { \
        printf("wire draw fixture failed at line %d (errno %d)\n", \
               __LINE__, errno); \
        return false; \
    } \
} while(0)

typedef struct wire_probe {
    unsigned filter, begin, resolve, prepare, profile, failure;
} wire_probe_t;

static int wire_filter(const pvr_chunk_cached_strip_t *strip, void *data) {
    wire_probe_t *p = data;
    (void)strip;
    ++p->filter;
    if(p->failure == 1) return 0;
    if(p->failure == 2) { errno = ECANCELED; return -1; }
    return 1;
}
static int wire_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    wire_probe_t *p = data;
    (void)strip;
    ++p->begin;
    if(p->failure == 3) { errno = ECANCELED; return -1; }
    return 0;
}
static int wire_resolve(uint16_t index, pvr_deform_vertex_t *v, void *data) {
    wire_probe_t *p = data;
    (void)index;
    ++p->resolve;
    v->position.x += 0.125f;
    if(p->failure == 4 && p->resolve == 2) v->normal.z = NAN;
    return 0;
}
static int wire_prepare(const pvr_chunk_render_state_t *state,
    uint16_t index, const pvr_deform_vertex_t *d, pvr_vertex_t *v, void *data) {
    wire_probe_t *p = data;
    (void)state;
    ++p->prepare;
    v->x = d->position.x + 0.0625f;
    v->argb ^= (uint32_t)index * UINT32_C(0x00102030);
    if(p->failure == 5 && p->prepare == 2) v->u = NAN;
    return 0;
}
static int wire_profile(const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_wire_profile_t *profile, void *data) {
    wire_probe_t *p = data;
    (void)strip;
    ++p->profile;
    profile->width += 0.125f;
    if(p->failure == 6) profile->width = NAN;
    return 0;
}

typedef struct wire_reuse_probe {
    pvr_frustum_t *frustum;
    pvr_vertex_t *vertices;
    unsigned scenario, prepares, begins;
} wire_reuse_probe_t;

static int wire_reuse_prepare(const pvr_chunk_render_state_t *state,
    uint16_t index, const pvr_deform_vertex_t *d, pvr_vertex_t *v, void *data) {
    wire_reuse_probe_t *p = data;
    unsigned i = p->prepares++;
    (void)state; (void)index; (void)d;
    v->x = p->scenario == 5 && i == 1 ? 0.0f : (float)(i & 1u);
    v->y = (float)(i / 2u);
    v->z = p->scenario >= 4 && i == 2 ? 0.0f : 1.0f;
    if(p->scenario == 6) {
        v->x = i == 0 ? -0.5f : i == 1 ? 0.0f : 0.5f;
        v->y = i == 1 ? 1.0f : 0.0f;
        v->z = i == 1 ? 0.0f : 1.0f;
    }
    /* Finite source coordinates whose matrix product overflows on endpoint 2. */
    if(p->scenario == 7 && i == 2) v->z = 3.0e38f;
    v->argb = UINT32_C(0xff010203) + i * UINT32_C(0x00102030);
    v->oargb = i * UINT32_C(0x00030201);
    return 0;
}

static int wire_reuse_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    wire_reuse_probe_t *p = data;
    (void)strip;
    ++p->begins;
    if(p->scenario == 1) p->frustum->object_to_screen[3][0] += 2.0f;
    if(p->scenario == 2) p->frustum->object_to_screen[2][3] = 0.0f;
    if(p->scenario == 3) p->vertices[1].x = NAN;
    return 0;
}

/* The first line is published before begin-time changes affect later edges.
   A late unusable W must not turn that valid prefix into an eager failure. */
static bool wire_reuse_boundaries(const pvr_chunk_model_cache_t *cache) {
    pvr_chunk_cache_draw_t draw;
    pvr_chunk_wire_profile_t profile = {
        0.25f, UINT32_C(0xffabcdef), 0,
        PVR_CHUNK_WIRE_PATH, PVR_CHUNK_WIRE_COLOR_VERTEX
    };
    alignas(32) static pvr_vertex_t output[2][129];
    alignas(32) pvr_vertex_t vertices[8];
    alignas(32) pvr_deform_vertex_t deformations[8];
    pvr_chunk_wire_workspace_t workspace = { vertices, deformations, 8 };
    pvr_chunk_wire_result_t results[2];
    size_t capacity, emitted[2];
    unsigned begins[2];
    int rc[2], errors[2];
#ifdef __DREAMCAST__
    shz_mat4x4_t saved, observed;
    shz_xmtrx_store_4x4(&saved);
#endif
    WIRE_CHECK(pvr_chunk_model_cache_draw_prepare(cache, &draw) == 0);
    WIRE_CHECK(pvr_chunk_model_cache_wire_capacity(cache, &capacity) == 0);
    WIRE_CHECK(capacity <= 128 && cache->maximum_strip_vertices <= 8);
    for(unsigned policy = 0; policy < 3; ++policy)
    for(unsigned scenario = 0; scenario < 8; ++scenario) {
        if(scenario == 6 && policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE) continue;
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            matrix_t matrix = {
                { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
                { 0, 0, 1, 1 }, { 0, 0, 0, 0 }
            };
            if(scenario == 7) matrix[2][3] = 2.0f;
            pvr_frustum_t frustum;
            pvr_geometry_sink_t sink;
            wire_reuse_probe_t probe = { &frustum, vertices, scenario, 0, 0 };
            WIRE_CHECK(pvr_frustum_init(&frustum, &matrix, -2, -2, 2, 2, 0.5f, 2) == 0);
            memset(output[admitted], 0x5a, sizeof(output[admitted]));
            WIRE_CHECK(pvr_geometry_sink_init_memory(&sink, output[admitted], capacity) == 0);
            errno = 0;
            if(admitted)
                rc[admitted] = pvr_chunk_model_cache_draw_emit_wire(&draw, &frustum,
                    (pvr_chunk_clip_policy_t)policy, &profile, &sink, &workspace,
                    NULL, wire_reuse_begin, NULL, wire_reuse_prepare, NULL, &probe,
                    results + admitted);
            else
                rc[admitted] = pvr_chunk_model_cache_emit_wire(cache, &frustum,
                    (pvr_chunk_clip_policy_t)policy, &profile, &sink, &workspace,
                    NULL, wire_reuse_begin, NULL, wire_reuse_prepare, NULL, &probe,
                    results + admitted);
            errors[admitted] = errno;
            emitted[admitted] = sink.emitted_vertices;
            begins[admitted] = probe.begins;
#ifdef __DREAMCAST__
            shz_xmtrx_store_4x4(&observed);
            WIRE_CHECK(!memcmp(&saved, &observed, sizeof(saved)));
#endif
        }
        WIRE_CHECK(rc[0] == rc[1] && errors[0] == errors[1] && emitted[0] == emitted[1]);
        WIRE_CHECK(begins[0] == begins[1]);
        WIRE_CHECK(!memcmp(results, results + 1, sizeof(results[0])));
        WIRE_CHECK(!memcmp(output[0], output[1], sizeof(output[0])));
        if(scenario == 7) {
            WIRE_CHECK(rc[0] == -1 && errors[0] == ERANGE &&
                       results[0].source_edges == 2 && emitted[0] == 4);
        }
        else if(policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE || scenario == 3) {
            if(scenario < 2) WIRE_CHECK(rc[0] == 0 && emitted[0] >= 8);
            else WIRE_CHECK(rc[0] == -1 && errors[0] == EDOM &&
                       results[0].source_edges == 2 &&
                       emitted[0] == (scenario == 5 ? 0u : 4u) &&
                       begins[0] == (scenario == 5 ? 0u : 1u));
        }
        else {
            WIRE_CHECK(rc[0] == 0);
            if(scenario == 6) {
                WIRE_CHECK(results[0].clipped_edges == 2);
                if(policy == PVR_CHUNK_CLIP_DROP) WIRE_CHECK(emitted[0] == 0);
                else {
                    WIRE_CHECK(emitted[0] == 8);
                    /* Two different near-plane intersections from the same
                       source endpoint: caching either intersection is wrong. */
                    WIRE_CHECK(fabsf((output[1][2].x + output[1][3].x) * 0.5f + 0.5f) < 0.0003f);
                    WIRE_CHECK(fabsf((output[1][4].x + output[1][5].x) * 0.5f - 0.5f) < 0.0003f);
                    WIRE_CHECK(fabsf(output[1][2].y - 1.0f) < 0.0003f &&
                               fabsf(output[1][4].y - 1.0f) < 0.0003f);
                    WIRE_CHECK(output[1][2].argb == UINT32_C(0xff09121b) &&
                               output[1][4].argb == UINT32_C(0xff19324b));
                }
            }
        }
    }
    return true;
}

static bool wire_draw_fixtures(const pvr_chunk_model_cache_t *cache) {
    pvr_chunk_cache_draw_t draw;
    matrix_t matrix = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
        { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    pvr_frustum_t frustum;
    pvr_chunk_wire_profile_t profile = {
        0.25f, UINT32_C(0xff123456), UINT32_C(0x00112233),
        PVR_CHUNK_WIRE_MESH, PVR_CHUNK_WIRE_COLOR_PROFILE
    };
    alignas(32) static pvr_vertex_t output[2][129];
    alignas(32) pvr_vertex_t vertices[2][8];
    alignas(32) pvr_deform_vertex_t deformation[2][8];
    pvr_deform_vertex_t untouched[8];
    pvr_vertex_t guard;
    size_t capacity;
#ifdef __DREAMCAST__
    shz_mat4x4_t sentinel, observed;
    shz_xmtrx_store_4x4(&sentinel);
#endif
    WIRE_CHECK(pvr_chunk_model_cache_draw_prepare(cache, &draw) == 0);
    WIRE_CHECK(pvr_chunk_model_cache_wire_capacity(cache, &capacity) == 0);
    WIRE_CHECK(capacity > 0 && capacity <= 128 &&
               cache->maximum_strip_vertices <= 8);
    WIRE_CHECK(pvr_frustum_init(&frustum, &matrix, -0.5f, -0.5f,
                                1.5f, 0.5f, 0.5f, 2.0f) == 0);
    memset(&guard, 0x5a, sizeof(guard));
    memset(untouched, 0xa5, sizeof(untouched));

    for(unsigned policy = 0; policy < 3; ++policy)
    for(unsigned topology = 0; topology < 3; ++topology)
    for(unsigned color = 0; color < 2; ++color)
    for(unsigned callbacks = 0; callbacks < 16; ++callbacks)
    for(unsigned failure = 0; failure < 10; ++failure) {
        pvr_chunk_wire_result_t result[2];
        wire_probe_t probe[2] = { { 0 }, { 0 } };
        int rc[2] = { -1, -1 }, errors[2] = { 0, 0 };
        size_t emitted[2] = { 0, 0 };
#ifdef WIRE_COUNT_PROJECTION
        size_t projections[2];
#endif
        profile.topology = (pvr_chunk_wire_topology_t)topology;
        profile.color_mode = (pvr_chunk_wire_color_mode_t)color;
        frustum.object_to_screen[3][3] = failure == 9 ? 0.0f : 1.0f;
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            pvr_geometry_sink_t sink;
            pvr_chunk_wire_workspace_t workspace = {
                vertices[admitted], deformation[admitted], 8
            };
            memset(output[admitted], 0x5a, sizeof(output[admitted]));
            memset(vertices[admitted], 0, sizeof(vertices[admitted]));
            memset(deformation[admitted], 0xa5, sizeof(deformation[admitted]));
            WIRE_CHECK(pvr_geometry_sink_init_memory(&sink, output[admitted],
                failure == 7 ? capacity - 1 : capacity) == 0);
            if(failure == 8) workspace.vertices = output[admitted];
            probe[admitted].failure = failure;
#ifdef WIRE_COUNT_PROJECTION
            wire_projected_vertices = 0;
#endif
            errno = 0;
            if(admitted)
                rc[admitted] = pvr_chunk_model_cache_draw_emit_wire(
                    &draw, &frustum, (pvr_chunk_clip_policy_t)policy, &profile,
                    &sink, &workspace, wire_filter, callbacks & 8 ? wire_begin : NULL,
                    callbacks & 1 ? wire_resolve : NULL,
                    callbacks & 2 ? wire_prepare : NULL,
                    callbacks & 4 ? wire_profile : NULL,
                    probe + admitted, result + admitted);
            else
                rc[admitted] = pvr_chunk_model_cache_emit_wire(
                    cache, &frustum, (pvr_chunk_clip_policy_t)policy, &profile,
                    &sink, &workspace, wire_filter, callbacks & 8 ? wire_begin : NULL,
                    callbacks & 1 ? wire_resolve : NULL,
                    callbacks & 2 ? wire_prepare : NULL,
                    callbacks & 4 ? wire_profile : NULL,
                    probe + admitted, result + admitted);
            errors[admitted] = errno;
#ifdef WIRE_COUNT_PROJECTION
            projections[admitted] = wire_projected_vertices;
#endif
            emitted[admitted] = sink.emitted_vertices;
            WIRE_CHECK(!memcmp(output[admitted] + capacity, &guard, sizeof(guard)));
            if(admitted && !(callbacks & 1))
                WIRE_CHECK(!memcmp(deformation[admitted], untouched,
                                    sizeof(untouched)));
#ifdef __DREAMCAST__
            shz_xmtrx_store_4x4(&observed);
            WIRE_CHECK(!memcmp(&observed, &sentinel, sizeof(observed)));
#endif
        }
        WIRE_CHECK(rc[0] == rc[1] && errors[0] == errors[1] &&
                   emitted[0] == emitted[1]);
        WIRE_CHECK(!memcmp(result, result + 1, sizeof(result[0])));
        WIRE_CHECK(!memcmp(probe, probe + 1, sizeof(probe[0])));
        WIRE_CHECK(!memcmp(output[0], output[1], sizeof(output[0])));
#ifdef WIRE_COUNT_PROJECTION
        WIRE_CHECK(projections[1] <= projections[0]);
        if(!failure && !(callbacks & 8)) {
            WIRE_CHECK(projections[1] < projections[0]);
            if(topology == PVR_CHUNK_WIRE_PATH)
                WIRE_CHECK(projections[1] == cache->vertex_count);
        }
#endif
        if(!failure) WIRE_CHECK(rc[0] == 0);
        if(failure == 1) WIRE_CHECK(rc[0] == 0 && !emitted[0]);
        if(failure == 2 || (failure == 3 && probe[0].begin))
            WIRE_CHECK(rc[0] == -1 && errors[0] == ECANCELED);
        if((failure == 4 && (callbacks & 1)) ||
           (failure == 5 && (callbacks & 2)))
            WIRE_CHECK(rc[0] == -1 && errors[0] == EILSEQ);
        if(failure == 6 && (callbacks & 4))
            WIRE_CHECK(rc[0] == -1 && errors[0] == EINVAL);
        if(failure == 7) WIRE_CHECK(rc[0] == -1 && errors[0] == ENOSPC);
        if(failure == 8) WIRE_CHECK(rc[0] == -1 && errors[0] == EINVAL);
        if(failure == 9 && policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE)
            WIRE_CHECK(rc[0] == -1 && errors[0] == EDOM && !emitted[0]);
    }
    pvr_chunk_wire_result_t rejected;
    const pvr_chunk_wire_result_t zero = { 0 };
    memset(&rejected, 0x5a, sizeof(rejected));
    errno = 0;
    WIRE_CHECK(pvr_chunk_model_cache_draw_emit_wire(NULL, NULL,
        PVR_CHUNK_CLIP_SPLIT, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, &rejected) == -1 && errno == EINVAL);
    WIRE_CHECK(!memcmp(&rejected, &zero, sizeof(zero)));
    return wire_reuse_boundaries(cache);
}

/* Repeated source indices are distinct strip references. Exercise cache-slot
   eviction and the mesh topology's second (diagonal) pass on host and SH-4. */
static bool wire_long_draw_fixtures(void) {
    static const uint32_t xyz[] = {
        PVR_CHUNK_VERTEX_XYZ | (13u << 16), 4u << 16,
        0, 0, 0, UINT32_C(0x3f800000), 0, 0,
        0, UINT32_C(0x3f800000), 0,
        UINT32_C(0x3f800000), UINT32_C(0x3f800000), 0, 0xff
    };
    static const uint16_t polygons[] = {
        PVR_CHUNK_STRIP_INDEX, 10, 1, 8, 0, 1, 2, 3, 0, 1, 2, 3, 0xff
    };
    const pvr_chunk_model_t model = {
        xyz, sizeof(xyz) / sizeof(xyz[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]), { 0, 0, 0 }, 2
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_model_cache_t cache;
    alignas(32) uint8_t storage[4096];
    WIRE_CHECK(pvr_chunk_model_open(&model, &view) == 0);
    WIRE_CHECK(pvr_chunk_model_plan_build(&view, entries, 256, &plan) == 0);
    WIRE_CHECK(pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                          NULL, NULL, &cache) == 0);
    return wire_draw_fixtures(&cache);
}
#undef WIRE_CHECK
#endif
