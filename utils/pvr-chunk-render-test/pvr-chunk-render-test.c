/* KallistiOS ##version##

   Host-side compact-model renderer tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_render.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 16))
#define POLYGON_HEADER(type, flags) \
    ((uint16_t)(type) | ((uint16_t)(flags) << 8))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0x00000000),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000), UINT32_C(0x00000000),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    POLYGON_HEADER(PVR_CHUNK_CONTROL_BLEND, 0x25),
    POLYGON_HEADER(PVR_CHUNK_TEXTURE, 0x94), UINT16_C(0xa123),
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR, 0x25), UINT16_C(4),
    UINT16_C(0x6699), UINT16_C(0xff33),
    UINT16_C(0x0203), UINT16_C(0x0801),
    POLYGON_HEADER(PVR_CHUNK_STRIP_UV8_FIXED_ARGB,
                   PVR_CHUNK_STRIP_USE_ALPHA),
    UINT16_C(17), UINT16_C(1), UINT16_C(0x8003),
    UINT16_C(0), UINT16_C(0), UINT16_C(256),
    UINT16_C(0x8040), UINT16_C(0x20ff),
    UINT16_C(1), UINT16_C(128), UINT16_C(0),
    UINT16_C(0xff10), UINT16_C(0x2030),
    UINT16_C(2), UINT16_C(256), UINT16_C(128),
    UINT16_C(0x7f01), UINT16_C(0x0203),
    UINT16_C(0x00ff)
};

static const uint16_t unsupported_polygons[] = {
    PVR_CHUNK_TEXTURE_TWO_VOLUME, UINT16_C(0), UINT16_C(0x00ff)
};

static const uint16_t bump_polygons[] = {
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_BUMP, 0x25), UINT16_C(6),
    UINT16_C(0x7fff), UINT16_C(0x0000), UINT16_C(0x8000),
    UINT16_C(0x4000), UINT16_C(0xc000), UINT16_C(0x0000),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
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
    UINT16_C(0), UINT16_C(0), UINT16_C(0),
    UINT16_C(256), UINT16_C(256),
    UINT16_C(1), UINT16_C(128), UINT16_C(0),
    UINT16_C(64), UINT16_C(128),
    UINT16_C(2), UINT16_C(256), UINT16_C(128),
    UINT16_C(0), UINT16_C(64),
    UINT16_C(0x00ff)
};

static const uint16_t two_volume_color_polygons[] = {
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE, 0x25), UINT16_C(2),
    UINT16_C(0x2233), UINT16_C(0xff11),
    POLYGON_HEADER(PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 0x25),
    UINT16_C(2), UINT16_C(0x5566), UINT16_C(0xff44),
    PVR_CHUNK_STRIP_TWO_VOLUME, UINT16_C(5), UINT16_C(1), UINT16_C(3),
    UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const uint32_t intensity_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_INTENSITY, 13),
    UINT32_C(0x00030000),
    UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0xffff0000),
    UINT32_C(0x3f800000), UINT32_C(0), UINT32_C(0),
    UINT32_C(0x80004000),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x40002000),
    UINT32_C(0x000000ff)
};

static const uint32_t xyzw_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZW, 13),
    UINT32_C(0x00030000),
    UINT32_C(0x40000000), UINT32_C(0), UINT32_C(0), UINT32_C(0x40000000),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0), UINT32_C(0x3f800000),
    UINT32_C(0), UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint32_t modifier_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 13),
    UINT32_C(0x00040000),
    UINT32_C(0), UINT32_C(0), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x000000ff)
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

static const uint16_t index_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

typedef struct callback_state {
    size_t begins;
    size_t prepares;
    int begin_error;
    pvr_chunk_render_state_t state;
} callback_state_t;

static alignas(32) pvr_vertex_t submitted[8];
static size_t submitted_count;
static uint32_t submitted_modifier_modes[16];
static size_t submitted_modifier_count;

void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode,
                     uint32_t cull) {
    memset(dst, 0, sizeof(*dst));
    dst->cmd = (uint32_t)list;
    dst->mode1 = mode | (cull << 8);
}

int pvr_prim(const void *data, size_t size) {
    if(size == sizeof(pvr_mod_hdr_t) + sizeof(pvr_modifier_vol_t) &&
       submitted_modifier_count < 16u) {
        const pvr_mod_hdr_t *header = data;

        submitted_modifier_modes[submitted_modifier_count++] =
            header->mode1 & 255u;
    }
    submitted_count = size / sizeof(pvr_vertex_t);
    memcpy(submitted, data, size);
    return 0;
}

typedef struct modifier_callback_state {
    size_t calls;
    uint16_t user_words[8];
} modifier_callback_state_t;

static int prepare_modifier(
    const pvr_chunk_vertex_attributes_t vertices[3],
    const uint16_t *user_words, size_t user_word_count,
    pvr_modifier_vol_t *triangle, void *data) {
    modifier_callback_state_t *callback = data;

    assert(vertices && triangle);
    if(user_word_count)
        callback->user_words[callback->calls] = user_words[0];
    triangle->d1 = (uint32_t)callback->calls;
    ++callback->calls;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    return pvr_prim(data, size);
}

static pvr_chunk_model_t make_model(const uint16_t *polygon_words,
                                    size_t polygon_word_count) {
    pvr_chunk_model_t model = {
        .vertex_words = vertices,
        .vertex_word_count = sizeof(vertices) / sizeof(vertices[0]),
        .polygon_words = polygon_words,
        .polygon_word_count = polygon_word_count,
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 2.0f
    };

    return model;
}

static void test_model_classification(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(polygons,
        sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_frustum_t frustum;
    pvr_frustum_classification_t classification;

    model.radius = 0.25f;
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_frustum_init(&frustum, &identity, -1.0f, -1.0f,
                            1.0f, 1.0f, 0.5f, 2.0f) == 0);
    assert(pvr_chunk_model_classify(&view, &frustum, &classification) == 0);
    assert(classification == PVR_FRUSTUM_INSIDE);

    view.model.center[0] = 1.1f;
    assert(pvr_chunk_model_classify(&view, &frustum, &classification) == 0);
    assert(classification == PVR_FRUSTUM_INTERSECT);

    view.model.center[0] = 2.0f;
    assert(pvr_chunk_model_classify(&view, &frustum, &classification) == 0);
    assert(classification == PVR_FRUSTUM_OUTSIDE);
}

static int begin_strip(const pvr_chunk_render_state_t *state,
                       const pvr_chunk_strip_view_t *strip, void *data) {
    callback_state_t *callback = data;

    assert(strip->vertex_count == 3);
    ++callback->begins;
    callback->state = *state;
    if(callback->begin_error < 0)
        return -1;
    if(callback->begin_error > 0) {
        errno = callback->begin_error;
        return -1;
    }
    return 0;
}

static int prepare_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state && strip_attributes->index == vertex_attributes->index);
    ++callback->prepares;
    if(vertex_attributes->position.w != 1.0f) {
        vertex->x /= vertex_attributes->position.w;
        vertex->y /= vertex_attributes->position.w;
        vertex->z /= vertex_attributes->position.w;
    }
    vertex->oargb = UINT32_C(0xaa000000) | vertex_attributes->index;
    return 0;
}

static int prepare_two_volume_vertex(
    const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *vertex_attributes,
    const pvr_chunk_strip_attributes_t *strip_attributes,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    callback_state_t *callback = data;

    assert(state && strip_attributes->index == vertex_attributes->index);
    assert(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED);
    ++callback->prepares;
    vertex->textured.oargb1 ^= vertex_attributes->index;
    return 0;
}

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00001f;
}

static void test_emit(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        polygons, sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t index[256];
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    alignas(32) pvr_vertex_t immediate_output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == 0);
    assert(result.consumed_records == 4);
    assert(result.emitted_strips == 1 && result.emitted_vertices == 3);
    assert(sink.emitted_vertices == 3);
    assert(callback.begins == 1 && callback.prepares == 3);
    assert(callback.state.present ==
           (PVR_CHUNK_RENDER_BLEND | PVR_CHUNK_RENDER_TEXTURE |
            PVR_CHUNK_RENDER_DIFFUSE | PVR_CHUNK_RENDER_SPECULAR |
            PVR_CHUNK_RENDER_SPECULAR_EXPONENT));
    assert(callback.state.blend_source == PVR_BLEND_SRCALPHA);
    assert(callback.state.blend_destination == PVR_BLEND_INVSRCALPHA);
    assert(callback.state.texture.identifier == UINT16_C(0x123));
    assert(callback.state.texture.filter == 2);
    assert(callback.state.texture.supersample == 1);
    assert(callback.state.texture.uv_flip == 2);
    assert(callback.state.texture.uv_clamp == 1);
    assert(callback.state.texture.mipmap_adjust == 4);
    assert(callback.state.diffuse_argb == UINT32_C(0xff336699));
    assert(callback.state.specular_argb == UINT32_C(0xff010203));
    assert(callback.state.specular_exponent == 8);
    assert(callback.state.strip_flags == PVR_CHUNK_STRIP_USE_ALPHA);

    /* Reversed strips exchange the first two references before projection. */
    assert(close_enough(output[0].x, 1.0f));
    assert(close_enough(output[1].x, -1.0f));
    assert(close_enough(output[2].y, 1.0f));
    assert(close_enough(output[0].u, 0.5f));
    assert(close_enough(output[1].v, 1.0f));
    assert(output[0].argb == UINT32_C(0xff102030));
    assert(output[1].argb == UINT32_C(0x804020ff));
    assert(output[2].argb == UINT32_C(0x7f010203));
    assert(output[0].oargb == UINT32_C(0xaa000001));
    assert(output[1].oargb == UINT32_C(0xaa000000));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);

    memcpy(immediate_output, output, sizeof(output));
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, index, 256, &plan) == 0);
    memset(output, 0, sizeof(output));
    memset(&callback, 0, sizeof(callback));
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    assert(pvr_chunk_model_emit_prepared(
               &plan, &identity, &sink, workspace, 3, begin_strip,
               prepare_vertex, &callback, &result) == 0);
    assert(!memcmp(output, immediate_output, sizeof(output)));
    assert(result.consumed_records == 4 && result.emitted_strips == 1 &&
           result.emitted_vertices == 3);
    assert(callback.begins == 1 && callback.prepares == 3);
}

static void test_preflight_and_prefix(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        polygons, sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    pvr_vertex_t unchanged[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);

    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 2,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == ENOSPC && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);

    callback.begin_error = EAGAIN;
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == EAGAIN && callback.begins == 1 &&
           callback.prepares == 3 && result.emitted_vertices == 0 &&
           sink.emitted_vertices == 0);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);

    callback.begin_error = 0;
    callback.begins = 0;
    callback.prepares = 0;
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, output, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == EINVAL && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);

    callback.begin_error = -1;
    errno = EBUSY;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == EIO && result.emitted_vertices == 0 &&
           sink.emitted_vertices == 0);

    submitted_count = 0;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                NULL, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == EINVAL && submitted_count == 0);
}

static void test_unsupported(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        unsupported_polygons,
        sizeof(unsupported_polygons) / sizeof(unsupported_polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == ENOTSUP && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
    assert(result.consumed_records == 0 && result.emitted_vertices == 0);
}

static void test_bump_material(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        bump_polygons, sizeof(bump_polygons) / sizeof(bump_polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    alignas(32) pvr_vertex_t unchanged[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, NULL, &callback, &result) == -1);
    assert(errno == ENOTSUP && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
    assert(result.consumed_records == 0 && result.emitted_vertices == 0);
    assert(!memcmp(output, unchanged, sizeof(output)));

    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == 0);
    assert(callback.begins == 1 && callback.prepares == 3);
    assert(callback.state.present & PVR_CHUNK_RENDER_BUMP_BASIS);
    assert(close_enough(callback.state.bump_direction.x, 1.0f));
    assert(close_enough(callback.state.bump_direction.y, 0.0f));
    assert(close_enough(callback.state.bump_direction.z, -1.0f));
    assert(close_enough(callback.state.bump_direction.w, 0.0f));
    assert(close_enough(callback.state.bump_up.x,
                        16384.0f / 32767.0f));
    assert(close_enough(callback.state.bump_up.y,
                        -16384.0f / 32767.0f));
    assert(close_enough(callback.state.bump_up.z, 0.0f));
    assert(close_enough(callback.state.bump_up.w, 0.0f));
    assert(callback.state.blend_source == PVR_BLEND_SRCALPHA);
    assert(callback.state.blend_destination == PVR_BLEND_INVSRCALPHA);
    assert(result.consumed_records == 2 && result.emitted_strips == 1 &&
           result.emitted_vertices == 3 && sink.emitted_vertices == 3);
    assert(output[0].oargb == UINT32_C(0xaa000000));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
}

static void test_intensity_policy_and_reserved_state(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = {
        .vertex_words = intensity_vertices,
        .vertex_word_count = sizeof(intensity_vertices) /
                             sizeof(intensity_vertices[0]),
        .polygon_words = index_polygons,
        .polygon_word_count = sizeof(index_polygons) /
                              sizeof(index_polygons[0]),
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 2.0f
    };
    uint16_t reserved_polygons[sizeof(polygons) / sizeof(polygons[0])];
    pvr_chunk_model_t reserved_model;
    pvr_chunk_model_view_t view;
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
    pvr_geometry_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, NULL, &callback, &result) == -1);
    assert(errno == ENOTSUP && callback.begins == 0 &&
           sink.emitted_vertices == 0);
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == 0);
    assert(callback.prepares == 3 && sink.emitted_vertices == 3);

    model.vertex_words = xyzw_vertices;
    model.vertex_word_count = sizeof(xyzw_vertices) / sizeof(xyzw_vertices[0]);
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    memset(&callback, 0, sizeof(callback));
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, NULL, &callback, &result) == -1);
    assert(errno == ENOTSUP && callback.begins == 0 &&
           sink.emitted_vertices == 0);
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == 0);
    assert(output[0].x == 1.0f && callback.prepares == 3);

    memcpy(reserved_polygons, polygons, sizeof(reserved_polygons));
    reserved_polygons[0] = POLYGON_HEADER(PVR_CHUNK_CONTROL_BLEND, 0xe5);
    reserved_model = make_model(
        reserved_polygons,
        sizeof(reserved_polygons) / sizeof(reserved_polygons[0]));
    assert(pvr_chunk_model_open(&reserved_model, &view) == 0);
    assert(pvr_geometry_sink_init_memory(&sink, output, 3) == 0);
    memset(&callback, 0, sizeof(callback));
    errno = 0;
    assert(pvr_chunk_model_emit(&view, &identity, &sink, workspace, 3,
                                begin_strip, prepare_vertex, &callback,
                                &result) == -1);
    assert(errno == EILSEQ && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
}

static void test_two_volume_emit(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        two_volume_textured_polygons,
        sizeof(two_volume_textured_polygons) /
        sizeof(two_volume_textured_polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t index[256];
    alignas(32) pvr_chunk_two_volume_vertex_t workspace[3];
    alignas(32) pvr_vertex_tpcm_t output[3];
    alignas(32) pvr_vertex_tpcm_t immediate_output[3];
    alignas(32) pvr_vertex_tpcm_t unchanged[3];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, output, 3) == 0);
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == 0);
    assert(result.consumed_records == 5 && result.emitted_strips == 1 &&
           result.emitted_vertices == 3 && sink.emitted_vertices == 3);
    assert(callback.begins == 1 && callback.prepares == 3);
    assert(callback.state.texture.identifier == UINT16_C(0x123));
    assert(callback.state.secondary_texture.identifier == UINT16_C(0x567));
    assert(callback.state.secondary_texture.filter == 1);
    assert(callback.state.secondary_texture.uv_flip == 1);
    assert(callback.state.secondary_texture.mipmap_adjust == 1);
    assert(callback.state.diffuse_argb == UINT32_C(0xff336699));
    assert(callback.state.secondary_diffuse_argb == UINT32_C(0xffaabbcc));
    assert(callback.state.specular_argb == UINT32_C(0xff010203));
    assert(callback.state.secondary_specular_argb == UINT32_C(0xff040506));
    assert(callback.state.secondary_specular_exponent == 4);
    assert(callback.state.secondary_present ==
           (PVR_CHUNK_RENDER_TEXTURE | PVR_CHUNK_RENDER_DIFFUSE |
            PVR_CHUNK_RENDER_SPECULAR |
            PVR_CHUNK_RENDER_SPECULAR_EXPONENT));

    /* Reversal exchanges complete references, including both UV sets. */
    assert(output[0].x == 1.0f && output[1].x == -1.0f);
    assert(close_enough(output[0].u0, 0.5f));
    assert(close_enough(output[0].u1, 0.25f));
    assert(close_enough(output[0].v1, 0.5f));
    assert(output[0].argb0 == UINT32_C(0xff336699));
    assert(output[0].argb1 == UINT32_C(0xffaabbcc));
    assert(output[0].oargb0 == UINT32_C(0xff010203));
    assert(output[0].oargb1 == (UINT32_C(0xff040506) ^ 1u));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);

    memcpy(immediate_output, output, sizeof(output));
    assert(pvr_chunk_model_plan_build(&view, index, 256, &plan) == 0);
    memset(output, 0, sizeof(output));
    memset(&callback, 0, sizeof(callback));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, output, 3) == 0);
    assert(pvr_chunk_model_emit_two_volume_prepared(
        &plan, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == 0);
    assert(!memcmp(output, immediate_output, sizeof(output)));
    assert(callback.begins == 1 && callback.prepares == 3);

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR, output, 3) == 0);
    memset(&callback, 0, sizeof(callback));
    errno = 0;
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == -1);
    assert(errno == EINVAL && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
    assert(!memcmp(output, unchanged, sizeof(output)));
}

static void test_two_volume_color_emit(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = make_model(
        two_volume_color_polygons,
        sizeof(two_volume_color_polygons) /
        sizeof(two_volume_color_polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_chunk_two_volume_vertex_t workspace[3];
    alignas(32) pvr_vertex_pcm_t output[3];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR, output, 3) == 0);
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip, NULL,
        &callback, &result) == 0);
    assert(callback.begins == 1 && result.emitted_vertices == 3);
    assert(output[0].argb0 == UINT32_C(0xff112233));
    assert(output[0].argb1 == UINT32_C(0xff445566));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);

    submitted_count = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) == 0);
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip, NULL,
        &callback, &result) == 0);
    assert(submitted_count == 3);
}

static void test_two_volume_preflight(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    uint16_t malformed[sizeof(two_volume_textured_polygons) /
                       sizeof(two_volume_textured_polygons[0])];
    pvr_chunk_model_t model = make_model(
        two_volume_textured_polygons,
        sizeof(two_volume_textured_polygons) /
        sizeof(two_volume_textured_polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_chunk_two_volume_vertex_t workspace[3];
    alignas(32) pvr_vertex_tpcm_t output[3];
    alignas(32) pvr_vertex_tpcm_t unchanged[3];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_render_result_t result;
    callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 2, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == -1);
    assert(errno == ENOSPC && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
    assert(!memcmp(output, unchanged, sizeof(output)));

    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, output, 2) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == -1);
    assert(errno == ENOSPC && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);

    memcpy(malformed, two_volume_textured_polygons, sizeof(malformed));
    malformed[15] = UINT16_C(0x1104);
    model = make_model(malformed, sizeof(malformed) / sizeof(malformed[0]));
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, output, 3) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == -1);
    assert(errno == EILSEQ && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);

    model = make_model(polygons, sizeof(polygons) / sizeof(polygons[0]));
    assert(pvr_chunk_model_open(&model, &view) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_two_volume(
        &view, &identity, &sink, workspace, 3, begin_strip,
        prepare_two_volume_vertex, &callback, &result) == -1);
    assert(errno == ENOTSUP && callback.begins == 0 &&
           callback.prepares == 0 && sink.emitted_vertices == 0);
}

static void test_modifier_emit(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    pvr_chunk_model_t model = {
        .vertex_words = modifier_vertices,
        .vertex_word_count = sizeof(modifier_vertices) /
                             sizeof(modifier_vertices[0]),
        .polygon_words = modifier_polygons,
        .polygon_word_count = sizeof(modifier_polygons) /
                              sizeof(modifier_polygons[0]),
        .center = { 0.5f, 0.5f, 0.0f },
        .radius = 1.0f
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t index[256];
    pvr_chunk_modifier_config_t config = {
        PVR_LIST_OP_MOD, PVR_CULLING_NONE,
        PVR_MODIFIER_INCLUDE_LAST_POLY
    };
    alignas(32) pvr_modifier_vol_t workspace;
    alignas(32) pvr_modifier_vol_t output[6];
    alignas(32) pvr_modifier_vol_t immediate_output[6];
    alignas(32) pvr_modifier_vol_t unchanged[6];
    pvr_geometry_vertex_sink_t sink;
    pvr_chunk_modifier_result_t result;
    modifier_callback_state_t callback = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    assert(pvr_chunk_model_emit_modifiers(
        &view, &identity, &config, &sink, &workspace, prepare_modifier,
        &callback, &result) == 0);
    assert(result.consumed_records == 3 && result.emitted_volumes == 3 &&
           result.emitted_triangles == 6 && sink.emitted_vertices == 6);
    assert(callback.calls == 6);
    assert(callback.user_words[0] == UINT16_C(0x00aa));
    assert(callback.user_words[1] == UINT16_C(0x00bb));
    assert(callback.user_words[4] == UINT16_C(0x00cc));
    assert(callback.user_words[5] == UINT16_C(0x00dd));

    /* Both quad and strip expansion preserve alternating winding. */
    assert(output[2].ax == 0.0f && output[2].bx == 1.0f &&
           output[2].cx == 0.0f && output[2].cy == 1.0f);
    assert(output[3].ax == 0.0f && output[3].ay == 1.0f &&
           output[3].bx == 1.0f && output[3].by == 0.0f &&
           output[3].cx == 1.0f && output[3].cy == 1.0f);
    assert(output[5].ax == 0.0f && output[5].ay == 1.0f &&
           output[5].bx == 1.0f && output[5].by == 0.0f);
    assert(output[5].flags == PVR_CMD_VERTEX_EOL && output[5].d1 == 5u);

    memcpy(immediate_output, output, sizeof(output));
    assert(pvr_chunk_model_plan_build(&view, index, 256, &plan) == 0);
    memset(output, 0, sizeof(output));
    memset(&callback, 0, sizeof(callback));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    assert(pvr_chunk_model_emit_modifiers_prepared(
        &plan, &identity, &config, &sink, &workspace, prepare_modifier,
        &callback, &result) == 0);
    assert(!memcmp(output, immediate_output, sizeof(output)));
    assert(callback.calls == 6 && result.emitted_triangles == 6);

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    memset(&callback, 0, sizeof(callback));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 5) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_modifiers(
        &view, &identity, &config, &sink, &workspace, prepare_modifier,
        &callback, &result) == -1);
    assert(errno == ENOSPC && callback.calls == 0 &&
           sink.emitted_vertices == 0);
    assert(!memcmp(output, unchanged, 5u * sizeof(output[0])));

    submitted_modifier_count = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER) == 0);
    assert(pvr_chunk_model_emit_modifiers(
        &view, &identity, &config, &sink, &workspace, NULL, NULL,
        &result) == 0);
    assert(submitted_modifier_count == 6 && sink.emitted_vertices == 6);
    assert(submitted_modifier_modes[0] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[1] == PVR_MODIFIER_INCLUDE_LAST_POLY);
    assert(submitted_modifier_modes[2] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[3] == PVR_MODIFIER_INCLUDE_LAST_POLY);
    assert(submitted_modifier_modes[4] == PVR_MODIFIER_OTHER_POLY);
    assert(submitted_modifier_modes[5] == PVR_MODIFIER_INCLUDE_LAST_POLY);

    config.final_mode = PVR_MODIFIER_OTHER_POLY;
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, output, 6) == 0);
    errno = 0;
    assert(pvr_chunk_model_emit_modifiers(
        &view, &identity, &config, &sink, &workspace, NULL, NULL,
        &result) == -1);
    assert(errno == EINVAL && sink.emitted_vertices == 0);
}

int main(void) {
    test_model_classification();
    test_emit();
    test_preflight_and_prefix();
    test_unsupported();
    test_bump_material();
    test_intensity_policy_and_reserved_state();
    test_two_volume_emit();
    test_two_volume_color_emit();
    test_two_volume_preflight();
    test_modifier_emit();
    puts("pvr chunk render tests passed");
    return 0;
}
