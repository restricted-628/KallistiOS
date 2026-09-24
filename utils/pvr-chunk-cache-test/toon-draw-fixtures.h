/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Shared host/SH-4 checked-versus-admitted ordinary toon/outline fixtures.
*/
#ifndef TOON_DRAW_FIXTURES_H
#define TOON_DRAW_FIXTURES_H

#include <dc/pvr_chunk_toon.h>
#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TOON_CHECK(condition) do { \
    if(!(condition)) { \
        printf("toon draw fixture failed at line %d (errno %d)\n", \
               __LINE__, errno); \
        return false; \
    } \
} while(0)

typedef struct toon_draw_probe {
    unsigned filters, begins, resolves, prepares, profiles;
    unsigned failure;
} toon_draw_probe_t;

static int toon_draw_filter(const pvr_chunk_cached_strip_t *strip, void *data) {
    toon_draw_probe_t *probe = data;
    (void)strip;
    ++probe->filters;
    if(probe->failure == 1) return 0;
    if(probe->failure == 2) { errno = ECANCELED; return -1; }
    return 1;
}

static int toon_draw_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    toon_draw_probe_t *probe = data;
    (void)strip;
    ++probe->begins;
    if(probe->failure == 3 && probe->begins == 2) {
        errno = ECANCELED;
        return -1;
    }
    return 0;
}

static int toon_draw_resolve(uint16_t index, pvr_deform_vertex_t *vertex,
                              void *data) {
    toon_draw_probe_t *probe = data;
    ++probe->resolves;
    vertex->position.x += 0.125f;
    vertex->normal.z = index ? 1.0f : -1.0f;
    if(probe->failure == 4 && probe->resolves == 5)
        vertex->normal.x = NAN;
    return 0;
}

static int toon_draw_prepare(const pvr_chunk_render_state_t *state,
    uint16_t index, const pvr_deform_vertex_t *deformation,
    pvr_vertex_t *vertex, void *data) {
    toon_draw_probe_t *probe = data;
    (void)state;
    (void)deformation;
    ++probe->prepares;
    vertex->u = (float)index * 0.25f;
    vertex->argb ^= 0x00123456;
    if(probe->prepares == 5) {
        if(probe->failure == 5) vertex->v = NAN;
        if(probe->failure == 6) vertex->z = -1.0f;
    }
    return 0;
}

static int toon_draw_profile(const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_toon_profile_t *profile, void *data) {
    toon_draw_probe_t *probe = data;
    (void)strip;
    ++probe->profiles;
    profile->light.ambient = 0.125f;
    if(probe->failure == 7 && probe->profiles == 2)
        profile->epsilon = -1.0f;
    return 0;
}

static int toon_draw_outline_profile(const pvr_chunk_cached_strip_t *strip,
    pvr_chunk_outline_profile_t *profile, void *data) {
    toon_draw_probe_t *probe = data;
    (void)strip;
    ++probe->profiles;
    profile->distance = 0.125f;
    if(probe->failure == 7 && probe->profiles == 2)
        profile->distance = -1.0f;
    return 0;
}

static bool toon_draw_fixtures(void) {
    static const uint32_t vertices[] = {
        (25u << 16) | PVR_CHUNK_VERTEX_XYZ_NORMAL, 0x00040000,
        0, 0, 0, 0, 0, 0xbf800000,
        0x3f800000, 0, 0, 0, 0, 0x3f800000,
        0, 0x3f800000, 0, 0, 0, 0x3f800000,
        0x3f800000, 0x3f800000, 0, 0, 0, 0x3f800000, 0xff
    };
    uint16_t polygons[] = {
        PVR_CHUNK_STRIP_INDEX, 6, 1, 4, 0, 1, 2, 3,
        PVR_CHUNK_STRIP_INDEX, 6, 1, 4, 0, 1, 2, 3, 0xff
    };
    pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(*vertices),
        polygons, sizeof(polygons) / sizeof(*polygons), { .5f, .5f, 0 }, 1
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t indices[256];
    alignas(32) uint8_t storage[2048];
    pvr_chunk_model_cache_t cache;
    pvr_chunk_cache_draw_t draw;
    const float thresholds[] = { 0.0f };
    const uint32_t colors[] = { 0xff404040, 0xffffffff };
    pvr_chunk_toon_profile_t profile = {
        .light = { .direction = { 0, 0, 1, 0 }, .intensity = 1, .ambient = 0 },
        .equation = PVR_TOON_SHADE_DOT, .thresholds = thresholds,
        .argb_modulation = colors, .threshold_count = 1, .epsilon = 1e-6f
    };
    const pvr_chunk_outline_profile_t outline_profile = { .125f, 0xff112233, 0 };
    alignas(8) matrix_t matrix = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
        { 0, 0, 1, 1 }, { 0, 0, 0, 1 }
    };
    pvr_normal_matrix_t normal_matrix;
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t scratch[4], clip[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_deform_vertex_t deformations[4];
    vector_t normals[4];
    float shades[4];
    pvr_toon_triangle_t triangles[3];
    pvr_chunk_toon_workspace_t workspace = {
        scratch, deformations, normals, shades, 4, triangles, 3,
        clip, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    pvr_chunk_outline_workspace_t outline_workspace = {
        scratch, deformations, 4, clip, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    alignas(32) pvr_vertex_t output[2][97];
    pvr_geometry_sink_t sink;
    pvr_chunk_toon_result_t toon_result[2];
    pvr_chunk_outline_result_t outline_result[2];
    toon_draw_probe_t probes[2];
    int status[2], errors[2];
    size_t counts[2];
#ifdef __DREAMCAST__
    shz_mat4x4_t saved_xmtrx, observed_xmtrx;
    shz_xmtrx_store_4x4(&saved_xmtrx);
#endif
    TOON_CHECK(pvr_normal_matrix_build(&normal_matrix, &matrix) == 0);
    for(unsigned variant = 0; variant < 3; ++variant) {
        unsigned flags = variant == 1 ? PVR_CHUNK_STRIP_FLAT_SHADED :
                         variant == 2 ? PVR_CHUNK_STRIP_IGNORE_LIGHT : 0;
        polygons[0] = polygons[8] = PVR_CHUNK_STRIP_INDEX | (flags << 8);
        TOON_CHECK(pvr_chunk_model_open(&model, &view) == 0);
        TOON_CHECK(pvr_chunk_model_plan_build(&view, indices, 256, &plan) == 0);
        TOON_CHECK(pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                              NULL, NULL, &cache) == 0);
        TOON_CHECK(pvr_chunk_model_cache_draw_prepare(&cache, &draw) == 0);
        /* 72 successful combinations plus dynamic failures per strip mode. */
        for(unsigned scenario = 0; scenario < 84; ++scenario) {
            unsigned failure = scenario < 72 ? 0 : scenario - 71;
            unsigned mask = failure ? 7 : scenario % 8;
            unsigned region = failure ? 0 : scenario / 24;
            pvr_chunk_clip_policy_t policy = failure ? PVR_CHUNK_CLIP_ASSUME_VISIBLE :
                (pvr_chunk_clip_policy_t)((scenario / 8) % 3);
            TOON_CHECK(pvr_frustum_init(&frustum, &matrix,
                region == 2 ? 3.0f : -1.0f, -1.0f,
                region == 1 ? .75f : 4.0f, 3.0f, .5f, 2.0f) == 0);
            for(unsigned outline = 0; outline < 2; ++outline) {
                memset(output, 0x5a, sizeof(output));
                memset(probes, 0, sizeof(probes));
                for(unsigned admitted = 0; admitted < 2; ++admitted) {
                    probes[admitted].failure = failure;
                    memset(deformations, 0xa5, sizeof(deformations));
                    TOON_CHECK(pvr_geometry_sink_init_memory(&sink,
                        output[admitted], failure == 8 ? 3 : 96) == 0);
                    pvr_chunk_toon_workspace_t tw = workspace;
                    pvr_chunk_outline_workspace_t ow = outline_workspace;
                    pvr_chunk_toon_profile_t tp = profile;
                    pvr_chunk_outline_profile_t op = outline_profile;
                    if(failure == 9) tw.vertices = ow.vertices = output[admitted];
                    if(failure == 10) tw.strip_capacity = ow.strip_capacity = 3;
                    if(failure == 11) { tp.epsilon = -1; op.distance = -1; }
                    if(failure == 12) frustum.object_to_screen[0][0] = NAN;
                    errno = 0;
                    if(outline) {
                        status[admitted] = admitted ?
                            pvr_chunk_model_cache_draw_emit_outline(&draw,
                            &frustum, policy, &op, &sink, &ow,
                            toon_draw_filter, toon_draw_begin,
                            mask & 1 ? toon_draw_resolve : NULL,
                            mask & 2 ? toon_draw_prepare : NULL,
                            mask & 4 ? toon_draw_outline_profile : NULL,
                            probes + admitted, outline_result + admitted) :
                            pvr_chunk_model_cache_emit_outline(&cache,
                            &frustum, policy, &op, &sink, &ow,
                            toon_draw_filter, toon_draw_begin,
                            mask & 1 ? toon_draw_resolve : NULL,
                            mask & 2 ? toon_draw_prepare : NULL,
                            mask & 4 ? toon_draw_outline_profile : NULL,
                            probes + admitted, outline_result + admitted);
                    }
                    else {
                        status[admitted] = admitted ?
                            pvr_chunk_model_cache_draw_emit_toon(&draw,
                            &normal_matrix, &frustum, policy, &tp, &sink, &tw,
                            toon_draw_filter, toon_draw_begin,
                            mask & 1 ? toon_draw_resolve : NULL,
                            mask & 2 ? toon_draw_prepare : NULL,
                            mask & 4 ? toon_draw_profile : NULL,
                            probes + admitted, toon_result + admitted) :
                            pvr_chunk_model_cache_emit_toon(&cache,
                            &normal_matrix, &frustum, policy, &tp, &sink, &tw,
                            toon_draw_filter, toon_draw_begin,
                            mask & 1 ? toon_draw_resolve : NULL,
                            mask & 2 ? toon_draw_prepare : NULL,
                            mask & 4 ? toon_draw_profile : NULL,
                            probes + admitted, toon_result + admitted);
                    }
                    errors[admitted] = errno;
                    counts[admitted] = sink.emitted_vertices;
#ifdef __DREAMCAST__
                    shz_xmtrx_store_4x4(&observed_xmtrx);
                    TOON_CHECK(!memcmp(&saved_xmtrx, &observed_xmtrx,
                                       sizeof(saved_xmtrx)));
#endif
                    if(admitted && !(mask & 1))
                        for(size_t i = 0; i < sizeof(deformations); ++i)
                            TOON_CHECK(((uint8_t *)deformations)[i] == 0xa5);
                    for(size_t i = counts[admitted] * sizeof(pvr_vertex_t);
                        i < sizeof(output[admitted]); ++i)
                        TOON_CHECK(((uint8_t *)output[admitted])[i] == 0x5a);
                }
                TOON_CHECK(status[0] == status[1] && errors[0] == errors[1]);
                TOON_CHECK(counts[0] == counts[1]);
                TOON_CHECK(!memcmp(output[0], output[1], sizeof(output[0])));
                TOON_CHECK(!memcmp(probes, probes + 1, sizeof(*probes)));
                TOON_CHECK(outline ? !memcmp(outline_result, outline_result + 1,
                    sizeof(*outline_result)) : !memcmp(toon_result,
                    toon_result + 1, sizeof(*toon_result)));
                if(failure <= 1) TOON_CHECK(status[1] == 0);
                /* A Z perturbation need not fail flat shell expansion or a
                   band triangle whose interpolation moves it in front. */
                else if(failure != 6) TOON_CHECK(status[1] == -1);
                if(!failure && !region) TOON_CHECK(counts[1] >= 12);
                if(failure == 1) TOON_CHECK(counts[1] == 0);
                if(failure >= 3 && failure <= 7) TOON_CHECK(counts[1] > 0);
            }
        }
    }
    return true;
}

#undef TOON_CHECK
#endif
