/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Shared color/textured two-volume toon packet regression.
*/
#ifndef TWO_VOLUME_TOON_FIXTURES_H
#define TWO_VOLUME_TOON_FIXTURES_H
#include <dc/pvr_chunk_toon.h>
#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define TVT_CHECK(c) do { if(!(c)) { \
    printf("two-volume toon fixture line %d errno %d\n", __LINE__, errno); \
    return false; } } while(0)

static int tvt_prepare(const pvr_chunk_render_state_t *state, uint16_t index,
    const pvr_deform_vertex_t *deformation, pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    (void)state; (void)index; (void)deformation;
    unsigned failure = *(const unsigned *)data;
    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
        const unsigned char *bytes = (const unsigned char *)vertex;
        for(size_t i = sizeof(pvr_vertex_pcm_t); i < sizeof(*vertex); ++i)
            if(bytes[i]) { errno = EILSEQ; return -1; }
        vertex->color.argb0 = UINT32_C(0xff112233);
        vertex->color.argb1 = UINT32_C(0xff445566);
    }
    else {
        vertex->textured.argb0 = UINT32_C(0xff112233);
        vertex->textured.argb1 = UINT32_C(0xff445566);
        vertex->textured.oargb0 = UINT32_C(0xff102030);
        vertex->textured.oargb1 = UINT32_C(0xff405060);
        vertex->textured.u0 = .25f;
        vertex->textured.v0 = .5f;
        vertex->textured.u1 = .75f;
        vertex->textured.v1 = 1.0f;
    }
    if(failure == 1 && index == 1) vertex->color.x = NAN;
    return 0;
}

static int tvt_resolve(uint16_t index, pvr_deform_vertex_t *vertex, void *data) {
    (void)data;
    vertex->normal.x = index == 0 ? 1.0f : 0.0f;
    vertex->normal.y = 0;
    vertex->normal.z = index == 0 ? 0.0f : 1.0f;
    return 0;
}

typedef struct tvt_probe {
    unsigned failure, filters, begins, resolves, prepares;
} tvt_probe_t;

static int tvt_count_prepare(const pvr_chunk_render_state_t *state,
    uint16_t index, const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format, pvr_chunk_two_volume_vertex_t *vertex,
    void *data) {
    tvt_probe_t *p = data;
    ++p->prepares;
    return tvt_prepare(state, index, deformation, format, vertex, &p->failure);
}
static int tvt_count_resolve(uint16_t index, pvr_deform_vertex_t *vertex,
                             void *data) {
    tvt_probe_t *p = data;
    ++p->resolves;
    tvt_resolve(index, vertex, NULL);
    if(p->failure == 4 && index == 1) vertex->normal.z = NAN;
    return 0;
}
static int tvt_filter(const pvr_chunk_cached_strip_t *strip, void *data) {
    tvt_probe_t *p = data;
    (void)strip;
    ++p->filters;
    if(p->failure == 5) return 0;
    if(p->failure == 6) { errno = ECANCELED; return -1; }
    return 1;
}
static int tvt_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    tvt_probe_t *p = data;
    (void)strip;
    ++p->begins;
    if(p->failure == 7) { errno = ECANCELED; return -1; }
    return 0;
}

static bool two_volume_toon_draw_compare(
    const pvr_chunk_two_volume_cache_t *cache,
    const pvr_normal_matrix_t *normal_matrix, const matrix_t *matrix,
    const pvr_chunk_toon_profile_t *profile,
    pvr_chunk_two_volume_toon_workspace_t *workspace) {
    pvr_chunk_two_volume_cache_draw_t draw;
    alignas(32) static uint8_t output[2][64 * 64 + 32];
    pvr_deform_vertex_t untouched[3];
    const uint32_t outside[2] = { 0xff808080, 0xffffffff };
    const uint32_t inside[2] = { 0xff404040, 0xff909090 };
    const pvr_chunk_two_volume_toon_modulation_t modulation = { inside, inside };
#ifdef __DREAMCAST__
    shz_mat4x4_t saved, observed;
    shz_xmtrx_store_4x4(&saved);
#endif
    TVT_CHECK(workspace->strip_capacity == 3);
    TVT_CHECK(pvr_chunk_model_two_volume_cache_draw_prepare(cache, &draw) == 0);
    memset(untouched, 0xa5, sizeof(untouched));
    for(unsigned policy = 0; policy < 3; ++policy)
    for(unsigned crossing = 0; crossing < 2; ++crossing)
    for(unsigned callbacks = 0; callbacks < 4; ++callbacks)
    for(unsigned failure = 0; failure < 10; ++failure) {
        pvr_frustum_t frustum;
        pvr_chunk_toon_profile_t p = *profile;
        pvr_chunk_toon_result_t results[2] = { { 0 }, { 0 } };
        tvt_probe_t probes[2] = { { 0 }, { 0 } };
        int status[2] = { -1, -1 }, errors[2] = { 0, 0 };
        size_t counts[2] = { 0, 0 };
        TVT_CHECK(pvr_frustum_init(&frustum, matrix,
            crossing ? -.5f : -2.0f, -2, 2, 2, .5f, 2) == 0);
        if(failure == 2) frustum.object_to_screen[3][3] = 0;
        p.argb_modulation = outside;
        p.oargb_modulation = outside;
        if(failure == 8) p.epsilon = -1;
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            pvr_geometry_vertex_sink_t sink;
            pvr_chunk_two_volume_toon_workspace_t w = *workspace;
            probes[admitted].failure = failure;
            memset(output[admitted], 0x5a, sizeof(output[admitted]));
            memcpy(w.deformations, untouched, sizeof(untouched));
            TVT_CHECK(pvr_geometry_vertex_sink_init_memory(&sink, cache->format,
                output[admitted], failure == 3 ? 1 : 64) == 0);
            if(failure == 9) w.vertices = (void *)output[admitted];
            errno = 0;
            if(admitted)
                status[admitted] = pvr_chunk_model_two_volume_cache_draw_emit_toon(
                    &draw, normal_matrix, &frustum, (pvr_chunk_clip_policy_t)policy,
                    &p, &modulation, &sink, &w, tvt_filter, tvt_begin,
                    callbacks & 1 ? tvt_count_resolve : NULL,
                    callbacks & 2 ? tvt_count_prepare : NULL,
                    probes + admitted, results + admitted);
            else
                status[admitted] = pvr_chunk_model_two_volume_cache_emit_toon(
                    cache, normal_matrix, &frustum, (pvr_chunk_clip_policy_t)policy,
                    &p, &modulation, &sink, &w, tvt_filter, tvt_begin,
                    callbacks & 1 ? tvt_count_resolve : NULL,
                    callbacks & 2 ? tvt_count_prepare : NULL,
                    probes + admitted, results + admitted);
            errors[admitted] = errno;
            counts[admitted] = sink.emitted_vertices;
            if(admitted && !(callbacks & 1))
                TVT_CHECK(!memcmp(w.deformations, untouched, sizeof(untouched)));
            for(size_t i = counts[admitted] * cache->vertex_size;
                i < sizeof(output[admitted]); ++i)
                TVT_CHECK(output[admitted][i] == 0x5a);
#ifdef __DREAMCAST__
            shz_xmtrx_store_4x4(&observed);
            TVT_CHECK(!memcmp(&saved, &observed, sizeof(saved)));
#endif
        }
        TVT_CHECK(status[0] == status[1] && errors[0] == errors[1] &&
                   counts[0] == counts[1]);
        TVT_CHECK(!memcmp(results, results + 1, sizeof(results[0])));
        TVT_CHECK(!memcmp(probes, probes + 1, sizeof(probes[0])));
        TVT_CHECK(!memcmp(output[0], output[1], sizeof(output[0])));
        if(!failure || failure == 5) TVT_CHECK(status[0] == 0);
        if(failure == 5) TVT_CHECK(!counts[0]);
        if((failure == 1 && (callbacks & 2)) ||
           (failure == 4 && (callbacks & 1)))
            TVT_CHECK(status[0] == -1 && errors[0] == EILSEQ);
        if(failure == 2 && policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE)
            TVT_CHECK(status[0] == -1 && errors[0] == EDOM);
        if(failure == 3) TVT_CHECK(!counts[0]);
        if(failure == 6 || (failure == 7 && probes[0].begins))
            TVT_CHECK(status[0] == -1 && errors[0] == ECANCELED);
        if(failure == 8 || failure == 9)
            TVT_CHECK(status[0] == -1 && errors[0] == EINVAL);
    }
    pvr_chunk_toon_result_t rejected;
    const pvr_chunk_toon_result_t zero = { 0 };
    memset(&rejected, 0x5a, sizeof(rejected));
    errno = 0;
    TVT_CHECK(pvr_chunk_model_two_volume_cache_draw_emit_toon(NULL, NULL, NULL,
        PVR_CHUNK_CLIP_SPLIT, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, &rejected) == -1 && errno == EINVAL);
    TVT_CHECK(!memcmp(&rejected, &zero, sizeof(zero)));
    return true;
}

static bool two_volume_toon_fixtures(void) {
    static const uint32_t source[] = {
        PVR_CHUNK_VERTEX_XYZ | (10u << 16), 0x00030000,
        0xbf800000, 0xbf800000, 0, 0x3f800000, 0xbf800000, 0,
        0, 0x3f800000, 0, 0xff
    };
    uint16_t color[] = {
        PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0x2233, 0xff11,
        PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 2, 0x5566, 0xff44,
        PVR_CHUNK_STRIP_TWO_VOLUME, 5, 1, 3, 0, 1, 2, 0xff
    };
    uint16_t textured[] = {
        PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0x2233, 0xff11,
        PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 2, 0x5566, 0xff44,
        PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME, 17, 1, 3,
        0, 0, 0, 256, 256, 1, 128, 0, 64, 128,
        2, 256, 128, 0, 64, 0xff
    };
    pvr_chunk_model_t model = {
        source, sizeof(source) / sizeof(*source), NULL, 0, { 0, 0, 0 }, 2
    };
    pvr_chunk_vertex_index_entry_t indices[256];
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_two_volume_cache_t cache;
    alignas(32) uint8_t storage[2048];
    alignas(32) pvr_chunk_two_volume_vertex_t vertices[3], clipped[21];
    alignas(32) pvr_deform_vertex_t deformations[3];
    vector_t normals[3];
    float shades[3];
    pvr_toon_triangle_t bands[3], secondary_bands[3];
    alignas(32) pvr_vertex_t primary[21], secondary[21];
    alignas(32) static uint8_t output[64 * 64 + 32];
    pvr_chunk_two_volume_toon_workspace_t workspace = {
        vertices, deformations, normals, shades, 3, bands, secondary_bands, 3,
        primary, secondary, clipped, 21
    };
    matrix_t matrix = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
        { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    pvr_normal_matrix_t normal_matrix;
    pvr_frustum_t frustum;
    pvr_chunk_toon_profile_t profile = { 0 };
    const float threshold = .5f;
    const vector_t light = { 0, 0, 1, 0 };
#ifdef __DREAMCAST__
    shz_mat4x4_t saved, observed;
    shz_xmtrx_store_4x4(&saved);
#endif
    TVT_CHECK(pvr_normal_matrix_build(&normal_matrix, &matrix) == 0);
    TVT_CHECK(pvr_toon_light_init(&profile.light, &light, 1, 0) == 0);
    profile.equation = PVR_TOON_SHADE_DOT;
    profile.thresholds = &threshold;
    profile.threshold_count = 1;
    profile.epsilon = 1e-6f;
    for(unsigned format = 0; format < 2; ++format)
    for(unsigned shading = 0; shading < 3; ++shading) {
        uint16_t *polygons = format ? textured : color;
        unsigned flags = shading == 1 ? PVR_CHUNK_STRIP_FLAT_SHADED :
                         shading == 2 ? PVR_CHUNK_STRIP_IGNORE_LIGHT : 0;
        polygons[8] = (uint16_t)((format ? PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME :
                                 PVR_CHUNK_STRIP_TWO_VOLUME) | (flags << 8));
        model.polygon_words = polygons;
        model.polygon_word_count = format ? sizeof(textured) / sizeof(*textured) :
                                            sizeof(color) / sizeof(*color);
        TVT_CHECK(pvr_chunk_model_open(&model, &view) == 0);
        TVT_CHECK(pvr_chunk_model_plan_build(&view, indices, 256, &plan) == 0);
        TVT_CHECK(pvr_chunk_model_two_volume_cache_build(
            &plan, storage, sizeof(storage), NULL, NULL, &cache) == 0);
        TVT_CHECK(two_volume_toon_draw_compare(&cache, &normal_matrix, &matrix,
                                               &profile, &workspace));
        for(unsigned policy = 0; policy < 3; ++policy)
        for(unsigned crossing = 0; crossing < 2; ++crossing)
        for(unsigned failure = 0; failure < 4; ++failure) {
            pvr_geometry_vertex_sink_t sink;
            pvr_chunk_toon_result_t result;
            float x[3] = { 0 }, y[3] = { 0 };
            double area = 0;
            TVT_CHECK(pvr_frustum_init(&frustum, &matrix,
                crossing ? -.5f : -2.0f, -2, 2, 2, .5f, 2) == 0);
            if(failure == 2) frustum.object_to_screen[3][3] = 0;
            memset(output, 0x5a, sizeof(output));
            TVT_CHECK(pvr_geometry_vertex_sink_init_memory(&sink, cache.format,
                output, failure == 3 ? 1 : 64) == 0);
            errno = 0;
            int rc = pvr_chunk_model_two_volume_cache_emit_toon(&cache,
                &normal_matrix, &frustum, (pvr_chunk_clip_policy_t)policy,
                &profile, NULL, &sink, &workspace, NULL, NULL, tvt_resolve,
                tvt_prepare, &failure, &result);
#ifdef __DREAMCAST__
            shz_xmtrx_store_4x4(&observed);
            TVT_CHECK(!memcmp(&saved, &observed, sizeof(saved)));
#endif
            if(failure == 1) TVT_CHECK(rc == -1 && errno == EILSEQ);
            else if(failure == 2 && policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE)
                TVT_CHECK(rc == -1 && errno == EDOM);
            else if(failure == 3)
                TVT_CHECK((rc == -1 && errno == ENOSPC) ||
                           (rc == 0 && !sink.emitted_vertices));
            else TVT_CHECK(rc == 0);
            TVT_CHECK(result.emitted_vertices == sink.emitted_vertices);
            if(!failure && (!crossing || policy != PVR_CHUNK_CLIP_DROP))
                TVT_CHECK(sink.emitted_vertices >= 3);
            for(size_t i = 0; i < sink.emitted_vertices; ++i) {
                pvr_chunk_two_volume_vertex_t packet = { 0 };
                memcpy(&packet, output + i * cache.vertex_size, cache.vertex_size);
                TVT_CHECK(packet.color.flags == (i % 3 == 2 ?
                    PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
                TVT_CHECK(isfinite(packet.color.x) && isfinite(packet.color.y) &&
                           fabsf(packet.color.z - 1.0f) < .0003f);
                x[i % 3] = packet.color.x;
                y[i % 3] = packet.color.y;
                if(i % 3 == 2)
                    area += fabs(((double)x[1] - x[0]) * (y[2] - y[0]) -
                                 ((double)x[2] - x[0]) * (y[1] - y[0])) * .5;
                if(policy != PVR_CHUNK_CLIP_ASSUME_VISIBLE)
                    TVT_CHECK(packet.color.x >= frustum.left - .0003f);
                if(format) {
                    TVT_CHECK(packet.textured.argb0 == 0xff112233 &&
                               packet.textured.argb1 == 0xff445566);
                    TVT_CHECK(packet.textured.oargb0 == 0xff102030 &&
                               packet.textured.oargb1 == 0xff405060);
                    TVT_CHECK(fabsf(packet.textured.u0 - .25f) < .0003f &&
                               fabsf(packet.textured.v0 - .5f) < .0003f &&
                               fabsf(packet.textured.u1 - .75f) < .0003f &&
                               fabsf(packet.textured.v1 - 1) < .0003f);
                }
                else TVT_CHECK(packet.color.argb0 == 0xff112233 &&
                                packet.color.argb1 == 0xff445566);
            }
            /* Independent geometry oracle for the unlit triangle: clipping
               x >= -0.5 removes a corner of area 0.25 from area 2. */
            if(!failure && shading == 2) {
                double expected_area = crossing && policy == PVR_CHUNK_CLIP_DROP ?
                    0 : crossing && policy == PVR_CHUNK_CLIP_SPLIT ? 1.75 : 2;
                TVT_CHECK(fabs(area - expected_area) < .001);
                if(!crossing || policy == PVR_CHUNK_CLIP_ASSUME_VISIBLE) {
                    TVT_CHECK(sink.emitted_vertices == 3);
                    TVT_CHECK(fabsf(x[0] + 1) < .0003f &&
                               fabsf(x[1] - 1) < .0003f && fabsf(x[2]) < .0003f);
                    TVT_CHECK(fabsf(y[0] + 1) < .0003f &&
                               fabsf(y[1] + 1) < .0003f &&
                               fabsf(y[2] - 1) < .0003f);
                }
            }
            for(size_t i = sink.emitted_vertices * cache.vertex_size;
                i < sizeof(output); ++i) TVT_CHECK(output[i] == 0x5a);
        }
    }
    return true;
}
#undef TVT_CHECK
#endif
