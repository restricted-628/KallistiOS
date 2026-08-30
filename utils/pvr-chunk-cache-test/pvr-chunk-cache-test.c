/* KallistiOS ##version##

   Host-side compact-model draw-cache tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_cache.h>
#include <dc/pvr_chunk_toon.h>
#include <dc/pvr_chunk_wire.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))
#define POLYGON_HEADER(type, flags) \
    ((uint16_t)(type) | ((uint16_t)(flags) << 8))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x40000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint32_t modifier_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 25),
    UINT32_C(0x00040000),
    UINT32_C(0), UINT32_C(0), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0x3344), UINT16_C(0xff22),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const uint16_t wire_quad_polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0x3344), UINT16_C(0xff22),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(6), UINT16_C(1),
    UINT16_C(4), UINT16_C(0), UINT16_C(1), UINT16_C(2), UINT16_C(3),
    UINT16_C(0x00ff)
};

static const uint16_t unsupported_polygons[] = {
    PVR_CHUNK_STRIP_TWO_VOLUME, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const uint16_t two_volume_textured_polygons[] = {
    POLYGON_HEADER(PVR_CHUNK_TEXTURE, 0x94), UINT16_C(0xa123),
    POLYGON_HEADER(PVR_CHUNK_TEXTURE_TWO_VOLUME, 0x41), UINT16_C(0x4567),
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR, 0x25), UINT16_C(4),
    UINT16_C(0x6699), UINT16_C(0xff33),
    UINT16_C(0x0203), UINT16_C(0x0801),
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR_TWO_VOLUME, 0x25),
    UINT16_C(4), UINT16_C(0xbbcc), UINT16_C(0xffaa),
    UINT16_C(0x0506), UINT16_C(0x0404),
    POLYGON_HEADER(PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME,
                   PVR_CHUNK_STRIP_USE_ALPHA),
    UINT16_C(17), UINT16_C(1), UINT16_C(0x8003),
    UINT16_C(0), UINT16_C(0), UINT16_C(0), UINT16_C(256), UINT16_C(256),
    UINT16_C(1), UINT16_C(128), UINT16_C(0), UINT16_C(64), UINT16_C(128),
    UINT16_C(2), UINT16_C(256), UINT16_C(128), UINT16_C(0), UINT16_C(64),
    UINT16_C(0x00ff)
};

static const uint16_t two_volume_color_polygons[] = {
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE, 0x25), UINT16_C(2),
    UINT16_C(0x2233), UINT16_C(0xff11),
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 0x25),
    UINT16_C(2), UINT16_C(0x5566), UINT16_C(0xff44),
    PVR_CHUNK_STRIP_TWO_VOLUME, UINT16_C(5), UINT16_C(1), UINT16_C(3),
    UINT16_C(0), UINT16_C(1), UINT16_C(2), UINT16_C(0x00ff)
};

static const uint16_t modifier_polygons[] = {
    PVR_CHUNK_VOLUME_TRIANGLES, UINT16_C(9), UINT16_C(0x4002),
    UINT16_C(0), UINT16_C(1), UINT16_C(2), UINT16_C(0x00aa),
    UINT16_C(0), UINT16_C(2), UINT16_C(1), UINT16_C(0x00bb),
    PVR_CHUNK_VOLUME_QUADS, UINT16_C(5), UINT16_C(1),
    UINT16_C(0), UINT16_C(1), UINT16_C(2), UINT16_C(3),
    PVR_CHUNK_VOLUME_STRIPS, UINT16_C(8), UINT16_C(0x4001),
    UINT16_C(4), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00cc), UINT16_C(3), UINT16_C(0x00dd),
    UINT16_C(0x00ff)
};

typedef struct callback_state {
    size_t begins;
    size_t filters;
    size_t resolves;
    size_t prepares;
    size_t modifier_calls;
    uint16_t modifier_user_words[8];
    uint16_t fail_index;
    int filter_result;
} callback_state_t;

#ifndef __DREAMCAST__
static uint32_t submitted_modifier_modes[8];
static size_t submitted_modifier_count;

void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode,
                     uint32_t cull) {
    memset(dst, 0, sizeof(*dst));
    dst->cmd = (uint32_t)list;
    dst->mode1 = mode | (cull << 8);
}

int pvr_prim(const void *data, size_t size) {
    if(size == sizeof(pvr_mod_hdr_t) + sizeof(pvr_modifier_vol_t) &&
       submitted_modifier_count < 8u) {
        const pvr_mod_hdr_t *header = data;

        submitted_modifier_modes[submitted_modifier_count++] =
            header->mode1 & 255u;
    }
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    return pvr_prim(data, size);
}
#endif

static void identity(matrix_t *matrix) {
    const matrix_t value = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(matrix, &value, sizeof(value));
}

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    callback_state_t *state = data;

    assert(strip->vertex_count == 3);
    assert(strip->state.diffuse_argb == UINT32_C(0xff223344));
    ++state->begins;
    return 0;
}

static int filter_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    callback_state_t *state = data;

    assert(strip->minimum.x == 0.0f && strip->maximum.x == 2.0f);
    ++state->filters;
    if(state->filter_result < 0)
        errno = ECANCELED;
    return state->filter_result;
}

static int resolve_vertex(uint16_t source_index,
                          pvr_deform_vertex_t *vertex, void *data) {
    callback_state_t *state = data;

    ++state->resolves;
    if(source_index == state->fail_index) {
        errno = ECANCELED;
        return -1;
    }
    vertex->position.x += 10.0f;
    return 0;
}

static int resolve_toon_vertex(uint16_t source_index,
                               pvr_deform_vertex_t *vertex, void *data) {
    callback_state_t *state = data;

    ++state->resolves;
    vertex->normal.x = 0.0f;
    vertex->normal.y = 0.0f;
    vertex->normal.z = source_index ? 1.0f : -1.0f;
    vertex->normal.w = 0.0f;
    return 0;
}

static int resolve_wire_vertex(uint16_t source_index,
                               pvr_deform_vertex_t *vertex, void *data) {
    callback_state_t *state = data;

    ++state->resolves;
    if(!source_index)
        vertex->position.x = -2.0f;
    return 0;
}

static int resolve_outline_vertex(uint16_t source_index,
                                  pvr_deform_vertex_t *vertex, void *data) {
    callback_state_t *state = data;

    (void)source_index;
    ++state->resolves;
    vertex->normal.x = 0.0f;
    vertex->normal.y = 2.0f;
    vertex->normal.z = 0.0f;
    vertex->normal.w = 0.0f;
    return 0;
}

static int prepare_vertex(const pvr_chunk_render_state_t *state,
                          uint16_t source_index,
                          const pvr_deform_vertex_t *deformation,
                          pvr_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state->diffuse_argb == UINT32_C(0xff223344));
    assert(deformation->normal.z == 1.0f);
    vertex->argb = UINT32_C(0xff000000) | source_index;
    ++callback->prepares;
    return 0;
}

static int prepare_outline_vertex(
        const pvr_chunk_render_state_t *state, uint16_t source_index,
        const pvr_deform_vertex_t *deformation,
        pvr_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state->diffuse_argb == UINT32_C(0xff223344));
    assert(deformation->normal.y == 2.0f);
    vertex->argb = UINT32_C(0xff800000) | source_index;
    vertex->oargb = UINT32_C(0xff008000) | source_index;
    ++callback->prepares;
    return 0;
}

static int begin_two_volume(const pvr_chunk_cached_strip_t *strip,
                            void *data) {
    callback_state_t *state = data;

    assert(strip->vertex_count == 3);
    assert(strip->state.secondary_present & PVR_CHUNK_RENDER_DIFFUSE);
    ++state->begins;
    return 0;
}

static int build_two_volume_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state && strip_attributes->index == vertex_attributes->index);
    assert(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED);
    vertex->textured.oargb1 ^= vertex_attributes->index;
    ++callback->prepares;
    return 0;
}

static int prepare_cached_two_volume_vertex(
    const pvr_chunk_render_state_t *state, uint16_t source_index,
    const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state->secondary_present & PVR_CHUNK_RENDER_DIFFUSE);
    assert(deformation->normal.z == 1.0f);
    assert(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED);
    vertex->textured.argb0 = UINT32_C(0xff000000) | source_index;
    ++callback->prepares;
    return 0;
}

static int build_modifier_triangle(
    const pvr_chunk_vertex_attributes_t vertices[3],
    const uint16_t *user_words, size_t user_word_count,
    pvr_modifier_vol_t *triangle, void *data) {
    callback_state_t *callback = data;

    assert(vertices && triangle);
    if(user_word_count)
        callback->modifier_user_words[callback->modifier_calls] =
            user_words[0];
    triangle->d1 = (uint32_t)callback->modifier_calls;
    ++callback->modifier_calls;
    return 0;
}

static int prepare_cached_modifier_triangle(
    const uint16_t source_indices[3],
    const pvr_deform_vertex_t deformations[3],
    const uint16_t *user_words, size_t user_word_count,
    pvr_modifier_vol_t *triangle, void *data) {
    callback_state_t *callback = data;

    assert(source_indices && deformations && triangle);
    assert(deformations[0].normal.z == 1.0f);
    if(user_word_count)
        callback->modifier_user_words[callback->modifier_calls] =
            user_words[0];
    triangle->d2 = UINT32_C(0x100) | source_indices[2];
    ++callback->modifier_calls;
    return 0;
}

static pvr_chunk_model_t make_model(const uint16_t *polygon_words,
                                    size_t polygon_word_count) {
    pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygon_words, polygon_word_count,
        { 1.0f, 0.0f, 0.0f }, 2.0f
    };

    return model;
}

static void test_toon_cache(const pvr_chunk_model_cache_t *cache) {
    const float thresholds[] = { 0.0f };
    const uint32_t colors[] = {
        UINT32_C(0xff404040), UINT32_C(0xffffffff)
    };
    vector_t direction = { 0.0f, 0.0f, 1.0f, 0.0f };
    pvr_chunk_toon_profile_t profile;
    alignas(8) matrix_t matrix;
    pvr_normal_matrix_t normal_matrix;
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t vertices[3];
    alignas(32) pvr_deform_vertex_t deformations[3];
    vector_t normals[3];
    float shades[3];
    pvr_toon_triangle_t toon_triangles[3];
    alignas(32) pvr_vertex_t clip_vertices[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_chunk_toon_workspace_t workspace = {
        vertices, deformations, normals, shades, 3,
        toon_triangles, 3, clip_vertices, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    pvr_chunk_toon_workspace_t overlapping_workspace;
    alignas(32) pvr_vertex_t output[9];
    pvr_geometry_sink_t sink;
    pvr_chunk_toon_result_t result;
    callback_state_t callbacks = { 0 };
    uint32_t dark;
    size_t index;
    size_t dark_vertices = 0;
    size_t light_vertices = 0;
    int rv;

    identity(&matrix);
    assert(pvr_normal_matrix_build(&normal_matrix, &matrix) == 0);
    assert(pvr_frustum_init(&frustum, &matrix, -1.0f, -1.0f,
                            3.0f, 1.0f, 0.5f, 2.0f) == 0);
    memset(&profile, 0, sizeof(profile));
    assert(pvr_toon_light_init(&profile.light, &direction,
                               1.0f, 0.0f) == 0);
    profile.equation = PVR_TOON_SHADE_DOT;
    profile.thresholds = thresholds;
    profile.argb_modulation = colors;
    profile.threshold_count = 1;
    profile.epsilon = 1.0e-6f;
    assert(pvr_chunk_toon_profile_validate(&profile) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 9) == 0);
    rv = pvr_chunk_model_cache_emit_toon(
        cache, &normal_matrix, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE,
        &profile, &sink, &workspace, NULL, begin_strip,
        resolve_toon_vertex, NULL, NULL, &callbacks, &result);
    if(rv < 0)
        perror("pvr_chunk_model_cache_emit_toon");
    assert(rv == 0);
    assert(result.visited_strips == 1 && result.source_triangles == 1);
    assert(result.emitted_strips == 1 && result.emitted_triangles == 3 &&
           result.emitted_vertices == 9 && result.generated_vertices == 5);
    assert(callbacks.begins == 1 && callbacks.resolves == 3);
    assert(sink.emitted_vertices == 9);

    overlapping_workspace = workspace;
    overlapping_workspace.shades = (float *)normals;
    assert(pvr_geometry_sink_init_memory(&sink, output, 9) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit_toon(
        cache, &normal_matrix, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE,
        &profile, &sink, &overlapping_workspace, NULL, begin_strip,
        resolve_toon_vertex, NULL, NULL, &callbacks, &result) == -1);
    assert(errno == EINVAL && sink.emitted_vertices == 0);

    assert(pvr_toon_color_modulate(&dark, UINT32_C(0xff223344),
                                   colors[0]) == 0);
    for(index = 0; index < 9; ++index) {
        if(output[index].argb == dark)
            ++dark_vertices;
        else if(output[index].argb == UINT32_C(0xff223344))
            ++light_vertices;
        assert(output[index].flags == (index % 3u == 2u ?
                                       PVR_CMD_VERTEX_EOL :
                                       PVR_CMD_VERTEX));
    }
    assert(dark_vertices && light_vertices);
}

static void test_outline_cache(const pvr_chunk_model_cache_t *cache) {
    pvr_chunk_outline_profile_t profile = {
        0.5f, UINT32_C(0xff102030), UINT32_C(0xff010203)
    };
    alignas(8) matrix_t matrix;
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t vertices[3];
    alignas(32) pvr_deform_vertex_t deformations[3];
    alignas(32) pvr_vertex_t clip_vertices[
        PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_chunk_outline_workspace_t workspace = {
        vertices, deformations, 3,
        clip_vertices, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    alignas(32) pvr_vertex_t output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_outline_result_t result;
    callback_state_t callbacks = { 0 };
    size_t index;

    identity(&matrix);
    assert(pvr_frustum_init(&frustum, &matrix, -1.0f, -1.0f,
                            3.0f, 1.0f, 0.5f, 2.0f) == 0);
    assert(pvr_chunk_outline_profile_validate(&profile) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit_outline(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, begin_strip, resolve_outline_vertex,
        prepare_outline_vertex, NULL, &callbacks, &result) == 0);
    assert(result.visited_strips == 1 && result.skipped_strips == 0);
    assert(result.source_triangles == 1 && result.dropped_triangles == 0);
    assert(result.emitted_strips == 1 && result.emitted_triangles == 1 &&
           result.emitted_vertices == 3);
    assert(callbacks.begins == 1 && callbacks.resolves == 3 &&
           callbacks.prepares == 3);
    assert(sink.emitted_vertices == 3);
    for(index = 0; index < 3u; ++index) {
        assert(output[index].y == 0.5f);
        assert(output[index].argb == profile.argb);
        assert(output[index].oargb == profile.oargb);
        assert(output[index].flags == (index == 2u ? PVR_CMD_VERTEX_EOL :
                                                    PVR_CMD_VERTEX));
    }

    profile.distance = 0.0f;
    errno = 0;
    assert(pvr_chunk_model_cache_emit_outline(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == -1);
    assert(errno == EINVAL && result.visited_strips == 0);
}

static void test_wire_cache(const pvr_chunk_model_cache_t *cache) {
    alignas(8) matrix_t matrix;
    pvr_frustum_t frustum;
    pvr_chunk_wire_profile_t profile = {
        2.0f, UINT32_C(0xff80c0ff), UINT32_C(0),
        PVR_CHUNK_WIRE_MESH, PVR_CHUNK_WIRE_COLOR_PROFILE
    };
    alignas(32) pvr_vertex_t vertices[3];
    alignas(32) pvr_deform_vertex_t deformations[3];
    pvr_chunk_wire_workspace_t workspace = {
        vertices, deformations, 3
    };
    pvr_chunk_wire_workspace_t overlapping_workspace;
    alignas(32) pvr_vertex_t output[12];
    pvr_geometry_sink_t sink;
    pvr_chunk_wire_result_t result;
    callback_state_t callbacks = { 0 };
    size_t capacity;
    size_t index;

    identity(&matrix);
    assert(pvr_frustum_init(&frustum, &matrix, -1.0f, -2.0f,
                            3.0f, 2.0f, 0.5f, 2.0f) == 0);
    assert(pvr_chunk_wire_profile_validate(&profile) == 0);
    assert(pvr_chunk_model_cache_wire_capacity(cache, &capacity) == 0 &&
           capacity == 12);

    assert(pvr_geometry_sink_init_memory(&sink, output, 12) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == 0);
    assert(result.visited_strips == 1 && result.source_edges == 3 &&
           result.emitted_edges == 3 && result.emitted_vertices == 12 &&
           result.clipped_edges == 0 && result.dropped_edges == 0);
    assert(sink.emitted_vertices == 12);
    assert(output[0].x == 0.0f && output[0].y == 1.0f &&
           output[1].x == 0.0f && output[1].y == -1.0f &&
           output[2].x == 1.0f && output[2].y == 1.0f &&
           output[3].x == 1.0f && output[3].y == -1.0f);
    for(index = 0; index < 12u; ++index) {
        assert(output[index].argb == profile.argb);
        assert(output[index].flags == (index % 4u == 3u ?
                                       PVR_CMD_VERTEX_EOL :
                                       PVR_CMD_VERTEX));
    }

    memset(&callbacks, 0, sizeof(callbacks));
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, begin_strip, NULL, NULL, NULL,
        &callbacks, &result) == 0);
    assert(callbacks.begins == 1 && sink.emitted_vertices == 12 &&
           result.emitted_edges == 3 && result.emitted_vertices == 12);

    memset(&callbacks, 0, sizeof(callbacks));
    assert(pvr_geometry_sink_init_memory(&sink, output, 12) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_SPLIT, &profile,
        &sink, &workspace, NULL, NULL, resolve_wire_vertex, NULL,
        NULL, &callbacks, &result) == 0);
    assert(callbacks.resolves == 3 && result.source_edges == 3 &&
           result.clipped_edges == 2 && result.dropped_edges == 0 &&
           result.emitted_edges == 3 && result.emitted_vertices == 12);

    memset(&callbacks, 0, sizeof(callbacks));
    assert(pvr_geometry_sink_init_memory(&sink, output, 12) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_DROP, &profile,
        &sink, &workspace, NULL, NULL, resolve_wire_vertex, NULL,
        NULL, &callbacks, &result) == 0);
    assert(callbacks.resolves == 3 && result.source_edges == 3 &&
           result.clipped_edges == 2 && result.dropped_edges == 2 &&
           result.emitted_edges == 1 && result.emitted_vertices == 4);

    assert(pvr_geometry_sink_init_memory(&sink, output, 11) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == -1);
    assert(errno == ENOSPC && sink.emitted_vertices == 0 &&
           result.source_edges == 0);

    overlapping_workspace = workspace;
    overlapping_workspace.deformations =
        (pvr_deform_vertex_t *)(void *)vertices;
    assert(pvr_geometry_sink_init_memory(&sink, output, 12) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit_wire(
        cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &overlapping_workspace, NULL, NULL, NULL, NULL,
        NULL, NULL, &result) == -1);
    assert(errno == EINVAL && sink.emitted_vertices == 0);

    profile.width = 0.0f;
    errno = 0;
    assert(pvr_chunk_wire_profile_validate(&profile) == -1);
    assert(errno == EINVAL);
}

static void test_wire_topologies(void) {
    const pvr_chunk_model_t model = {
        modifier_vertices,
        sizeof(modifier_vertices) / sizeof(modifier_vertices[0]),
        wire_quad_polygons,
        sizeof(wire_quad_polygons) / sizeof(wire_quad_polygons[0]),
        { 0.5f, 0.5f, 0.0f }, 1.0f
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_cache_requirements_t requirements;
    alignas(32) uint8_t storage[2048];
    pvr_chunk_model_cache_t cache;
    alignas(8) matrix_t matrix;
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t vertices[4];
    alignas(32) pvr_deform_vertex_t deformations[4];
    pvr_chunk_wire_workspace_t workspace = {
        vertices, deformations, 4
    };
    pvr_chunk_wire_profile_t profile = {
        1.0f, UINT32_C(0xffffffff), UINT32_C(0),
        PVR_CHUNK_WIRE_BOUNDARY, PVR_CHUNK_WIRE_COLOR_VERTEX
    };
    alignas(32) pvr_vertex_t output[20];
    pvr_geometry_sink_t sink;
    pvr_chunk_wire_result_t result;
    size_t capacity;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_cache_query(&plan, &requirements) == 0);
    assert(requirements.bytes <= sizeof(storage));
    assert(pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                       NULL, NULL, &cache) == 0);
    assert(pvr_chunk_model_cache_wire_capacity(&cache, &capacity) == 0 &&
           capacity == 20);
    identity(&matrix);
    assert(pvr_frustum_init(&frustum, &matrix, -2.0f, -2.0f,
                            2.0f, 2.0f, 0.5f, 2.0f) == 0);

    assert(pvr_geometry_sink_init_memory(&sink, output, 20) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        &cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == 0);
    assert(result.source_edges == 4 && result.emitted_edges == 4 &&
           result.emitted_vertices == 16);
    assert(output[0].argb == UINT32_C(0xff223344));

    profile.topology = PVR_CHUNK_WIRE_PATH;
    assert(pvr_geometry_sink_init_memory(&sink, output, 20) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        &cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == 0);
    assert(result.source_edges == 3 && result.emitted_edges == 3 &&
           result.emitted_vertices == 12);

    profile.topology = PVR_CHUNK_WIRE_MESH;
    assert(pvr_geometry_sink_init_memory(&sink, output, 20) == 0);
    assert(pvr_chunk_model_cache_emit_wire(
        &cache, &frustum, PVR_CHUNK_CLIP_ASSUME_VISIBLE, &profile,
        &sink, &workspace, NULL, NULL, NULL, NULL, NULL, NULL,
        &result) == 0);
    assert(result.source_edges == 5 && result.emitted_edges == 5 &&
           result.emitted_vertices == 20);
}

static void test_two_volume_toon(
    const pvr_chunk_two_volume_cache_t *cache) {
    const float thresholds[] = { 0.0f };
    const uint32_t outside[] = {
        UINT32_C(0xff808080), UINT32_C(0xffffffff)
    };
    const uint32_t inside[] = {
        UINT32_C(0xffffffff), UINT32_C(0xff404040)
    };
    vector_t direction = { 0.0f, 0.0f, 1.0f, 0.0f };
    pvr_chunk_toon_profile_t profile;
    pvr_chunk_two_volume_toon_modulation_t secondary = {
        inside, inside
    };
    alignas(8) matrix_t matrix;
    pvr_normal_matrix_t normal_matrix;
    pvr_frustum_t frustum;
    alignas(32) pvr_chunk_two_volume_vertex_t vertices[3];
    alignas(32) pvr_deform_vertex_t deformations[3];
    vector_t normals[3];
    float shades[3];
    pvr_toon_triangle_t toon_triangles[3];
    pvr_toon_triangle_t secondary_toon_triangles[3];
    alignas(32) pvr_vertex_t clip_primary[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_vertex_t clip_secondary[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_chunk_two_volume_vertex_t
        clip_vertices[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_chunk_two_volume_toon_workspace_t workspace = {
        vertices, deformations, normals, shades, 3,
        toon_triangles, secondary_toon_triangles, 3,
        clip_primary, clip_secondary, clip_vertices,
        PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    alignas(32) pvr_vertex_tpcm_t output[63];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_toon_result_t result;
    callback_state_t callbacks = { 0 };
    size_t index;
    size_t distinct_sets = 0;

    identity(&matrix);
    assert(pvr_normal_matrix_build(&normal_matrix, &matrix) == 0);
    /* The left plane crosses the source triangle, exercising the parallel
       two-attribute clipping path after the shade boundary is generated. */
    assert(pvr_frustum_init(&frustum, &matrix, 0.5f, -1.0f,
                            3.0f, 1.0f, 0.5f, 2.0f) == 0);
    memset(&profile, 0, sizeof(profile));
    assert(pvr_toon_light_init(&profile.light, &direction,
                               1.0f, 0.0f) == 0);
    profile.equation = PVR_TOON_SHADE_DOT;
    profile.thresholds = thresholds;
    profile.argb_modulation = outside;
    profile.oargb_modulation = outside;
    profile.threshold_count = 1;
    profile.epsilon = 1.0e-6f;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, cache->format, output, 63) == 0);
    assert(pvr_chunk_model_two_volume_cache_emit_toon(
        cache, &normal_matrix, &frustum, PVR_CHUNK_CLIP_SPLIT,
        &profile, &secondary, &sink, &workspace, NULL,
        begin_two_volume, resolve_toon_vertex, NULL,
        &callbacks, &result) == 0);
    assert(result.visited_strips == 1 && result.source_triangles == 1);
    assert(result.emitted_strips == 1 && result.generated_vertices == 5);
    assert(result.emitted_vertices == sink.emitted_vertices &&
           result.emitted_vertices >= 3);
    assert(callbacks.begins == 1 && callbacks.resolves == 3);
    for(index = 0; index < sink.emitted_vertices; ++index) {
        assert(output[index].flags == (index % 3u == 2u ?
                                       PVR_CMD_VERTEX_EOL :
                                       PVR_CMD_VERTEX));
        assert(isfinite(output[index].u0) && isfinite(output[index].v0) &&
               isfinite(output[index].u1) && isfinite(output[index].v1));
        if(output[index].argb0 != output[index].argb1 ||
           output[index].oargb0 != output[index].oargb1 ||
           output[index].u0 != output[index].u1 ||
           output[index].v0 != output[index].v1)
            ++distinct_sets;
    }
    assert(distinct_sets == sink.emitted_vertices);
}

static void test_two_volume_cache(void) {
    pvr_chunk_model_t model = make_model(
        two_volume_textured_polygons,
        sizeof(two_volume_textured_polygons) /
        sizeof(two_volume_textured_polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_two_volume_cache_requirements_t requirements;
    alignas(32) uint8_t storage[2048];
    pvr_chunk_two_volume_cache_t cache;
    alignas(8) matrix_t matrix;
    alignas(32) pvr_chunk_two_volume_vertex_t workspace[3];
    alignas(32) pvr_vertex_tpcm_t output[3];
    alignas(32) pvr_vertex_pcm_t color_output[3];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_cache_result_t result;
    callback_state_t callbacks = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_two_volume_cache_query(
        &plan, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.format ==
           PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED);
    assert(requirements.vertex_size == sizeof(pvr_vertex_tpcm_t));
    assert(requirements.strip_count == 1 &&
           requirements.vertex_count == 3);
    assert(requirements.bytes <= sizeof(storage));

    assert(pvr_chunk_model_two_volume_cache_build(
        &plan, storage, sizeof(storage), build_two_volume_vertex,
        &callbacks, &cache) == 0);
    assert(callbacks.prepares == 3);
    assert(cache.vertex_size == sizeof(pvr_vertex_tpcm_t));
    assert(cache.strips[0].minimum.x == 0.0f &&
           cache.strips[0].maximum.x == 2.0f);
    assert(cache.source_indices[0] == 1 && cache.source_indices[1] == 0 &&
           cache.source_indices[2] == 2);

    identity(&matrix);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    callbacks.filter_result = 0;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, cache.format, output, 3) == 0);
    assert(pvr_chunk_model_two_volume_cache_emit_filtered(
        &cache, &matrix, &sink, workspace, 3, filter_strip,
        begin_two_volume, resolve_vertex,
        prepare_cached_two_volume_vertex, &callbacks, &result) == 0);
    assert(result.emitted_vertices == 0 && result.skipped_vertices == 3);
    assert(callbacks.filters == 1 && callbacks.begins == 0 &&
           callbacks.resolves == 0 && callbacks.prepares == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, cache.format, output, 3) == 0);
    assert(pvr_chunk_model_two_volume_cache_emit(
        &cache, &matrix, &sink, workspace, 3, begin_two_volume,
        resolve_vertex, prepare_cached_two_volume_vertex,
        &callbacks, &result) == 0);
    assert(result.emitted_strips == 1 && result.emitted_vertices == 3);
    assert(callbacks.begins == 1 && callbacks.resolves == 3 &&
           callbacks.prepares == 3);
    assert(output[0].x == 11.0f && output[1].x == 10.0f &&
           output[2].x == 12.0f);
    assert(output[0].u0 == 0.5f);
    assert(output[0].u1 == 0.25f);
    assert(output[0].argb0 == UINT32_C(0xff000001));
    assert(output[0].oargb1 == (UINT32_C(0xff040506) ^ 1u));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);

    test_two_volume_toon(&cache);

    model = make_model(two_volume_color_polygons,
                       sizeof(two_volume_color_polygons) /
                       sizeof(two_volume_color_polygons[0]));
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_two_volume_cache_query(
        &plan, &requirements) == 0);
    assert(requirements.format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR);
    assert(requirements.vertex_size == sizeof(pvr_vertex_pcm_t));
    assert(pvr_chunk_model_two_volume_cache_build(
        &plan, storage, sizeof(storage), NULL, NULL, &cache) == 0);
    assert(cache.vertex_size == sizeof(pvr_vertex_pcm_t));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, cache.format, color_output, 3) == 0);
    assert(pvr_chunk_model_two_volume_cache_emit(
        &cache, &matrix, &sink, workspace, 3, begin_two_volume,
        resolve_vertex, NULL, &callbacks, &result) == 0);
    assert(color_output[0].x == 10.0f && color_output[1].x == 11.0f &&
           color_output[2].x == 12.0f);
    assert(color_output[0].argb0 == UINT32_C(0xff112233));
    assert(color_output[0].argb1 == UINT32_C(0xff445566));
    assert(color_output[2].flags == PVR_CMD_VERTEX_EOL);
}

static void test_modifier_cache(void) {
    pvr_chunk_model_t model = {
        modifier_vertices,
        sizeof(modifier_vertices) / sizeof(modifier_vertices[0]),
        modifier_polygons,
        sizeof(modifier_polygons) / sizeof(modifier_polygons[0]),
        { 0.5f, 0.5f, 0.0f }, 1.0f
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_modifier_cache_requirements_t requirements;
    alignas(32) uint8_t storage[4096];
    pvr_chunk_modifier_cache_t cache;
    alignas(8) matrix_t matrix;
    pvr_chunk_modifier_config_t config = {
        PVR_LIST_OP_MOD, PVR_CULLING_NONE,
        PVR_MODIFIER_INCLUDE_LAST_POLY
    };
    alignas(32) pvr_modifier_vol_t workspace;
    alignas(32) pvr_modifier_vol_t output[6];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_modifier_cache_result_t result;
    callback_state_t callbacks = { 0 };
    pvr_chunk_cached_modifier_triangle_t *mutable_triangles;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    assert(pvr_chunk_model_modifier_cache_query(
        &plan, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.volume_count == 3);
    assert(requirements.triangle_count == 6);
    assert(requirements.corner_count == 18);
    assert(requirements.user_word_count == 4);
    assert(requirements.bytes <= sizeof(storage));

    errno = 0;
    assert(pvr_chunk_model_modifier_cache_build(
        &plan, storage, requirements.bytes - 1u, build_modifier_triangle,
        &callbacks, &cache) == -1);
    assert(errno == ENOSPC && cache.version == 0);
    assert(pvr_chunk_model_modifier_cache_build(
        &plan, storage, sizeof(storage), build_modifier_triangle,
        &callbacks, &cache) == 0);
    assert(callbacks.modifier_calls == 6);
    assert(cache.volume_count == 3 && cache.triangle_count == 6);
    assert(cache.triangles[0].final_in_volume == 0);
    assert(cache.triangles[1].final_in_volume == 1);
    assert(cache.triangles[2].final_in_volume == 0);
    assert(cache.triangles[3].final_in_volume == 1);
    assert(cache.triangles[4].final_in_volume == 0);
    assert(cache.triangles[5].final_in_volume == 1);
    assert(cache.user_words[0] == UINT16_C(0x00aa));
    assert(cache.user_words[1] == UINT16_C(0x00bb));
    assert(cache.user_words[2] == UINT16_C(0x00cc));
    assert(cache.user_words[3] == UINT16_C(0x00dd));
    assert(cache.source_indices[6] == 0 && cache.source_indices[7] == 1 &&
           cache.source_indices[8] == 2);
    assert(cache.source_indices[9] == 2 && cache.source_indices[10] == 1 &&
           cache.source_indices[11] == 3);

    identity(&matrix);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    assert(pvr_chunk_model_modifier_cache_emit(
        &cache, &matrix, &config, &sink, &workspace, resolve_vertex,
        prepare_cached_modifier_triangle, &callbacks, &result) == 0);
    assert(result.emitted_volumes == 3 && result.emitted_triangles == 6);
    assert(callbacks.resolves == 18 && callbacks.modifier_calls == 6);
    assert(output[0].ax == 10.0f && output[0].bx == 11.0f &&
           output[0].cx == 10.0f);
    assert(output[2].ax == 10.0f && output[2].bx == 11.0f &&
           output[2].cy == 1.0f);
    assert(output[3].ax == 10.0f && output[3].ay == 1.0f &&
           output[3].bx == 11.0f && output[3].by == 0.0f);
    assert(output[5].d1 == 5u && output[5].d2 == UINT32_C(0x103));
    assert(output[5].flags == PVR_CMD_VERTEX_EOL);

#ifndef __DREAMCAST__
    submitted_modifier_count = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER) == 0);
    assert(pvr_chunk_model_modifier_cache_emit(
        &cache, &matrix, &config, &sink, &workspace, NULL,
        NULL, NULL, &result) == 0);
    assert(submitted_modifier_count == 6);
    assert(submitted_modifier_modes[0] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[1] == PVR_MODIFIER_INCLUDE_LAST_POLY);
    assert(submitted_modifier_modes[2] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[3] == PVR_MODIFIER_INCLUDE_LAST_POLY);
    assert(submitted_modifier_modes[4] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[5] == PVR_MODIFIER_INCLUDE_LAST_POLY);
#endif

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 5) == 0);
    errno = 0;
    assert(pvr_chunk_model_modifier_cache_emit(
        &cache, &matrix, &config, &sink, &workspace, resolve_vertex,
        NULL, &callbacks, &result) == -1);
    assert(errno == ENOSPC && callbacks.resolves == 0 &&
           result.emitted_triangles == 0 && sink.emitted_vertices == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = 2;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    errno = 0;
    assert(pvr_chunk_model_modifier_cache_emit(
        &cache, &matrix, &config, &sink, &workspace, resolve_vertex,
        NULL, &callbacks, &result) == -1);
    assert(errno == ECANCELED && result.emitted_triangles == 0 &&
           sink.emitted_vertices == 0);

    mutable_triangles =
        (pvr_chunk_cached_modifier_triangle_t *)cache.triangles;
    mutable_triangles[0].final_in_volume = 2;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    errno = 0;
    assert(pvr_chunk_model_modifier_cache_emit(
        &cache, &matrix, &config, &sink, &workspace, NULL,
        NULL, NULL, &result) == -1);
    assert(errno == EILSEQ && sink.emitted_vertices == 0);
    mutable_triangles[0].final_in_volume = 0;
}

int main(void) {
    pvr_chunk_model_t model =
        make_model(polygons, sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_cache_requirements_t requirements;
    alignas(32) uint8_t storage[1024];
    pvr_chunk_model_cache_t cache;
    alignas(8) matrix_t matrix;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_cache_result_t result;
    callback_state_t callbacks;
    pvr_chunk_model_cache_t malformed;
    pvr_frustum_t frustum;
    pvr_frustum_classification_t classification;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);

    assert(pvr_chunk_model_cache_query(&plan, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.strip_count == 1);
    assert(requirements.vertex_count == 3);
    assert(requirements.maximum_strip_vertices == 3);
    assert(requirements.vertices_offset % 32u == 0);
    assert(requirements.deform_vertices_offset % 32u == 0);
    assert(requirements.bytes <= sizeof(storage));

    memset(&cache, 0x5a, sizeof(cache));
    errno = 0;
    assert(pvr_chunk_model_cache_build(&plan, storage,
                                       requirements.bytes - 1u,
                                       NULL, NULL, &cache) == -1);
    assert(errno == ENOSPC && cache.version == 0);

    assert(pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                       NULL, NULL, &cache) == 0);
    assert(cache.version == PVR_CHUNK_CACHE_VERSION);
    assert(cache.strip_count == 1 && cache.vertex_count == 3);
    assert(cache.vertices[0].x == 0.0f);
    assert(cache.vertices[1].x == 1.0f);
    assert(cache.vertices[2].x == 2.0f);
    assert(cache.deform_vertices[2].normal.z == 1.0f);
    assert(cache.source_indices[0] == 0);
    assert(cache.source_indices[1] == 1);
    assert(cache.source_indices[2] == 2);
    assert(cache.strips[0].minimum.x == 0.0f);
    assert(cache.strips[0].minimum.y == 0.0f);
    assert(cache.strips[0].minimum.z == 0.0f);
    assert(cache.strips[0].maximum.x == 2.0f);
    assert(cache.strips[0].maximum.y == 0.0f);
    assert(cache.strips[0].maximum.z == 0.0f);

    assert(pvr_chunk_model_cache_validate(&cache) == 0);
    test_toon_cache(&cache);
    test_outline_cache(&cache);
    test_wire_cache(&cache);

    identity(&matrix);
    assert(pvr_frustum_init(&frustum, &matrix, -1.0f, -1.0f,
                            3.0f, 1.0f, 0.5f, 2.0f) == 0);
    assert(pvr_chunk_cached_strip_classify(
        cache.strips, &frustum, &classification) == 0);
    assert(classification == PVR_FRUSTUM_INSIDE);
    assert(pvr_frustum_init(&frustum, &matrix, 3.0f, -1.0f,
                            5.0f, 1.0f, 0.5f, 2.0f) == 0);
    assert(pvr_chunk_cached_strip_classify(
        cache.strips, &frustum, &classification) == 0);
    assert(classification == PVR_FRUSTUM_OUTSIDE);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    callbacks.filter_result = 0;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit_filtered(
        &cache, &matrix, &sink, workspace, 3, filter_strip, begin_strip,
        resolve_vertex, prepare_vertex, &callbacks, &result) == 0);
    assert(result.emitted_strips == 0 && result.emitted_vertices == 0);
    assert(result.skipped_strips == 1 && result.skipped_vertices == 3);
    assert(sink.emitted_vertices == 0);
    assert(callbacks.filters == 1 && callbacks.begins == 0 &&
           callbacks.resolves == 0 && callbacks.prepares == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    callbacks.filter_result = -1;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit_filtered(
        &cache, &matrix, &sink, workspace, 3, filter_strip, NULL,
        resolve_vertex, NULL, &callbacks, &result) == -1);
    assert(errno == ECANCELED && result.emitted_vertices == 0 &&
           result.skipped_vertices == 0 && callbacks.filters == 1 &&
           callbacks.resolves == 0 && sink.emitted_vertices == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = UINT16_MAX;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit(&cache, &matrix, &sink,
                                      workspace, 3, begin_strip,
                                      resolve_vertex, prepare_vertex,
                                      &callbacks, &result) == 0);
    assert(result.emitted_strips == 1 && result.emitted_vertices == 3);
    assert(sink.emitted_vertices == 3);
    assert(callbacks.begins == 1 && callbacks.resolves == 3 &&
           callbacks.prepares == 3);
    assert(output[0].x == 10.0f && output[1].x == 11.0f &&
           output[2].x == 12.0f);
    assert(output[0].z == 1.0f);
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
    assert(output[2].argb == UINT32_C(0xff000002));

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.fail_index = 1;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_emit(&cache, &matrix, &sink,
                                      workspace, 3, NULL,
                                      resolve_vertex, NULL,
                                      &callbacks, &result) == -1);
    assert(errno == ECANCELED && result.emitted_vertices == 0 &&
           sink.emitted_vertices == 0);

    malformed = cache;
    ((pvr_chunk_cached_strip_t *)malformed.strips)->reserved = 1;
    errno = 0;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit(&malformed, &matrix, &sink,
                                      workspace, 3, NULL, NULL, NULL,
                                      NULL, &result) == -1);
    assert(errno == EILSEQ && result.emitted_vertices == 0);
    ((pvr_chunk_cached_strip_t *)malformed.strips)->reserved = 0;

    ((pvr_chunk_cached_strip_t *)malformed.strips)->minimum.x = 3.0f;
    errno = 0;
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_cache_emit(&malformed, &matrix, &sink,
                                      workspace, 3, NULL, NULL, NULL,
                                      NULL, &result) == -1);
    assert(errno == EILSEQ && result.emitted_vertices == 0);
    ((pvr_chunk_cached_strip_t *)malformed.strips)->minimum.x = 0.0f;

    model = make_model(unsupported_polygons,
                       sizeof(unsupported_polygons) /
                       sizeof(unsupported_polygons[0]));
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);
    errno = 0;
    assert(pvr_chunk_model_cache_query(&plan, &requirements) == -1);
    assert(errno == ENOTSUP && requirements.bytes == 0);

    test_two_volume_cache();
    test_modifier_cache();
    test_wire_topologies();
    puts("pvr-chunk-cache-test: PASS");
    return 0;
}
