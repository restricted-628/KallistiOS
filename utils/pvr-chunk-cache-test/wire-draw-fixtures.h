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
    for(unsigned callbacks = 0; callbacks < 8; ++callbacks)
    for(unsigned failure = 0; failure < 10; ++failure) {
        pvr_chunk_wire_result_t result[2];
        wire_probe_t probe[2] = { { 0 }, { 0 } };
        int rc[2] = { -1, -1 }, errors[2] = { 0, 0 };
        size_t emitted[2] = { 0, 0 };
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
            errno = 0;
            if(admitted)
                rc[admitted] = pvr_chunk_model_cache_draw_emit_wire(
                    &draw, &frustum, (pvr_chunk_clip_policy_t)policy, &profile,
                    &sink, &workspace, wire_filter, wire_begin,
                    callbacks & 1 ? wire_resolve : NULL,
                    callbacks & 2 ? wire_prepare : NULL,
                    callbacks & 4 ? wire_profile : NULL,
                    probe + admitted, result + admitted);
            else
                rc[admitted] = pvr_chunk_model_cache_emit_wire(
                    cache, &frustum, (pvr_chunk_clip_policy_t)policy, &profile,
                    &sink, &workspace, wire_filter, wire_begin,
                    callbacks & 1 ? wire_resolve : NULL,
                    callbacks & 2 ? wire_prepare : NULL,
                    callbacks & 4 ? wire_profile : NULL,
                    probe + admitted, result + admitted);
            errors[admitted] = errno;
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
    return true;
}
#undef WIRE_CHECK
#endif
