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
    POLYGON_HEADER(PVR_CHUNK_STRIP_UV8_ARGB,
                   PVR_CHUNK_STRIP_USE_ALPHA),
    UINT16_C(17), UINT16_C(1), UINT16_C(0x8003),
    UINT16_C(0), UINT16_C(0), UINT16_C(255),
    UINT16_C(0x8040), UINT16_C(0x20ff),
    UINT16_C(1), UINT16_C(128), UINT16_C(0),
    UINT16_C(0xff10), UINT16_C(0x2030),
    UINT16_C(2), UINT16_C(255), UINT16_C(128),
    UINT16_C(0x7f01), UINT16_C(0x0203),
    UINT16_C(0x00ff)
};

static const uint16_t unsupported_polygons[] = {
    PVR_CHUNK_TEXTURE_TWO_VOLUME, UINT16_C(0), UINT16_C(0x00ff)
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

int pvr_prim(const void *data, size_t size) {
    submitted_count = size / sizeof(pvr_vertex_t);
    memcpy(submitted, data, size);
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
    alignas(32) pvr_vertex_t workspace[3];
    alignas(32) pvr_vertex_t output[3];
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
    assert(close_enough(output[0].u, 128.0f / 255.0f));
    assert(close_enough(output[1].v, 1.0f));
    assert(output[0].argb == UINT32_C(0xff102030));
    assert(output[1].argb == UINT32_C(0x804020ff));
    assert(output[2].argb == UINT32_C(0x7f010203));
    assert(output[0].oargb == UINT32_C(0xaa000001));
    assert(output[1].oargb == UINT32_C(0xaa000000));
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
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

int main(void) {
    test_emit();
    test_preflight_and_prefix();
    test_unsupported();
    test_intensity_policy_and_reserved_state();
    puts("pvr chunk render tests passed");
    return 0;
}
