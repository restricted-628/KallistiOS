/* KallistiOS ##version##

   Host-side PVR geometry contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_geometry.h>
#include <dc/pvr_frustum.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct extended_vertex {
    pvr_vertex_t vertex;
    uint32_t application_data[8];
} extended_vertex_t;

static alignas(32) pvr_vertex_t submitted[8];
static size_t submitted_count;
static pvr_list_t submitted_list;
static int submit_error;

int pvr_prim(const void *data, size_t size) {
    assert(!(size % sizeof(pvr_vertex_t)));

    if(submit_error) {
        if(submit_error > 0)
            errno = submit_error;
        return -1;
    }

    submitted_count = size / sizeof(pvr_vertex_t);
    memcpy(submitted, data, size);
    submitted_list = (pvr_list_t)-1;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    assert(!(size % sizeof(pvr_vertex_t)));

    if(submit_error) {
        if(submit_error > 0)
            errno = submit_error;
        return -1;
    }

    submitted_count = size / sizeof(pvr_vertex_t);
    memcpy(submitted, data, size);
    submitted_list = list;
    return 0;
}

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00001f;
}

static pvr_vertex_t make_vertex(float x, float y, float z, uint32_t flags,
                                uint32_t color) {
    pvr_vertex_t vertex = {
        .flags = flags,
        .x = x,
        .y = y,
        .z = z,
        .u = 0.25f,
        .v = 0.75f,
        .argb = color,
        .oargb = UINT32_C(0x10203040)
    };

    return vertex;
}

static void test_projection(void) {
    alignas(32) const matrix_t projection = {
        { 240.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 240.0f, 0.0f, 0.0f },
        { -320.0f, -240.0f, -1.02020202f, -1.0f },
        { 320.0f, 240.0f, -2.02020202f, 1.0f }
    };
    extended_vertex_t input[2] = {
        { .vertex = { 0 }, .application_data = { 1 } },
        { .vertex = { 0 }, .application_data = { 2 } }
    };
    alignas(32) pvr_vertex_t output[2];
    pvr_geometry_stream_t stream = {
        input, 2, sizeof(extended_vertex_t)
    };
    pvr_geometry_result_t result;

    input[0].vertex = make_vertex(0.0f, 0.0f, -2.0f,
                                  PVR_CMD_VERTEX,
                                  UINT32_C(0xff112233));
    input[1].vertex = make_vertex(1.0f, 0.0f, -2.0f,
                                  PVR_CMD_VERTEX_EOL,
                                  UINT32_C(0xff445566));

    assert(pvr_geometry_project(output, 2, &stream, &projection, &result) == 0);
    assert(result.consumed_vertices == 2 && result.produced_vertices == 2);
    assert(close_enough(output[0].x, 320.0f));
    assert(close_enough(output[0].y, 240.0f));
    assert(close_enough(output[0].z, 1.0f / 3.0f));
    assert(close_enough(output[1].x, 400.0f));
    assert(close_enough(output[1].y, 240.0f));
    assert(output[0].flags == PVR_CMD_VERTEX);
    assert(output[1].flags == PVR_CMD_VERTEX_EOL);
    assert(output[0].u == input[0].vertex.u);
    assert(output[0].v == input[0].vertex.v);
    assert(output[0].argb == input[0].vertex.argb);
    assert(output[0].oargb == input[0].vertex.oargb);

    /* Exact canonical in-place projection is explicitly supported. */
    memcpy(output, &input[0].vertex, sizeof(output[0]));
    memcpy(output + 1, &input[1].vertex, sizeof(output[1]));
    stream.vertices = output;
    stream.stride = sizeof(pvr_vertex_t);
    assert(pvr_geometry_project(output, 2, &stream, &projection, NULL) == 0);
    assert(close_enough(output[0].x, 320.0f));
    assert(close_enough(output[1].x, 400.0f));
}

static void test_projection_failures(void) {
    alignas(32) matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    alignas(32) pvr_vertex_t input[3];
    alignas(32) pvr_vertex_t output[3];
    alignas(32) pvr_vertex_t unchanged[3];
    pvr_geometry_stream_t stream = { input, 3, sizeof(pvr_vertex_t) };
    pvr_geometry_result_t result;
    alignas(32) unsigned char unaligned[sizeof(pvr_vertex_t) * 3u + 32u];

    input[0] = make_vertex(1.0f, 2.0f, 3.0f, PVR_CMD_VERTEX,
                           UINT32_C(0xff010203));
    input[1] = make_vertex(4.0f, 5.0f, 6.0f, UINT32_C(0xd0000000),
                           UINT32_C(0xff040506));
    input[2] = make_vertex(7.0f, 8.0f, 9.0f, PVR_CMD_VERTEX_EOL,
                           UINT32_C(0xff070809));
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));

    errno = 0;
    assert(pvr_geometry_project(output, 3, &stream, &identity, &result) == -1);
    assert(errno == EILSEQ);
    assert(result.consumed_vertices == 1 && result.produced_vertices == 1);
    assert(close_enough(output[0].x, 1.0f));
    assert(close_enough(output[0].y, 2.0f));
    assert(close_enough(output[0].z, 1.0f));
    assert(memcmp(output + 1, unchanged + 1, 2 * sizeof(pvr_vertex_t)) == 0);

    errno = 0;
    assert(pvr_geometry_project(output, 2, &stream, &identity, &result) == -1);
    assert(errno == ENOSPC && result.produced_vertices == 0);

    identity[0][0] = NAN;
    errno = 0;
    assert(pvr_geometry_project(output, 3, &stream, &identity, &result) == -1);
    assert(errno == EDOM && result.produced_vertices == 0);

    identity[0][0] = 1.0f;
    input[1].flags = PVR_CMD_VERTEX_EOL;
    input[1].x = NAN;
    errno = 0;
    assert(pvr_geometry_project(output, 3, &stream, &identity, &result) == -1);
    assert(errno == EDOM && result.produced_vertices == 1);

    input[1] = make_vertex(4.0f, 5.0f, 6.0f, PVR_CMD_VERTEX_EOL,
                           UINT32_C(0xff040506));
    stream.vertex_count = 1;
    identity[3][3] = -1.0f;
    errno = 0;
    assert(pvr_geometry_project(output, 3, &stream, &identity, &result) == -1);
    assert(errno == EDOM && result.produced_vertices == 0);

    identity[3][3] = 1.0f;
    errno = 0;
    assert(pvr_geometry_project((pvr_vertex_t *)(void *)(unaligned + 4), 3,
                                &stream, &identity, &result) == -1);
    assert(errno == EINVAL && result.produced_vertices == 0);

    /* Shifted overlap is rejected before any destination write. */
    stream.vertex_count = 2;
    errno = 0;
    assert(pvr_geometry_project(input + 1, 2, &stream, &identity, &result) == -1);
    assert(errno == EINVAL && result.produced_vertices == 0);
}

static void test_sinks(void) {
    alignas(32) pvr_vertex_t vertices[3] = {
        { .flags = PVR_CMD_VERTEX },
        { .flags = PVR_CMD_VERTEX },
        { .flags = PVR_CMD_VERTEX_EOL }
    };
    alignas(32) pvr_vertex_t memory[4];
    pvr_geometry_sink_t sink;

    assert(pvr_geometry_sink_init_memory(&sink, memory, 4) == 0);
    assert(pvr_geometry_sink_emit(&sink, vertices, 2) == 0);
    assert(pvr_geometry_sink_emit(&sink, vertices + 2, 1) == 0);
    assert(sink.emitted_vertices == 3);
    assert(memcmp(memory, vertices, sizeof(vertices)) == 0);

    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, vertices, 2) == -1);
    assert(errno == ENOSPC && sink.emitted_vertices == 3);

    submitted_count = 0;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    assert(pvr_geometry_sink_emit(&sink, vertices, 3) == 0);
    assert(sink.emitted_vertices == 3 && submitted_count == 3);
    assert(submitted_list == (pvr_list_t)-1);
    assert(memcmp(submitted, vertices, sizeof(vertices)) == 0);

    submitted_count = 0;
    assert(pvr_geometry_sink_init_buffered(&sink, PVR_LIST_TR_POLY) == 0);
    assert(pvr_geometry_sink_emit(&sink, vertices, 3) == 0);
    assert(sink.emitted_vertices == 3 && submitted_count == 3);
    assert(submitted_list == PVR_LIST_TR_POLY);

    submit_error = EBUSY;
    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, vertices, 1) == -1);
    assert(errno == EBUSY && sink.emitted_vertices == 3);
    submit_error = 0;

    submit_error = -1;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, vertices, 1) == -1);
    assert(errno == EPERM && sink.emitted_vertices == 0);

    assert(pvr_geometry_sink_init_buffered(&sink, PVR_LIST_TR_POLY) == 0);
    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, vertices, 1) == -1);
    assert(errno == EIO && sink.emitted_vertices == 0);
    submit_error = 0;

    errno = 0;
    assert(pvr_geometry_sink_init_buffered(&sink, PVR_LIST_OP_MOD) == -1);
    assert(errno == EINVAL);

    sink.kind = (pvr_geometry_sink_kind_t)99;
    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, vertices, 1) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(pvr_geometry_sink_emit(&sink, NULL, 0) == -1);
    assert(errno == EINVAL);

    assert(pvr_geometry_sink_init_current(&sink) == 0);
    assert(pvr_geometry_sink_emit(&sink, NULL, 0) == 0);
}

static void test_format_projection(void) {
    alignas(32) const matrix_t transform = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 10.0f, 20.0f, 0.0f, 1.0f }
    };
    alignas(32) pvr_vertex_tpcm_t textured[3] = {
        {
            .flags = PVR_CMD_VERTEX,
            .x = 1.0f, .y = 2.0f, .z = 3.0f,
            .u0 = 0.25f, .v0 = 0.5f,
            .argb0 = UINT32_C(0xff112233),
            .oargb0 = UINT32_C(0xff445566),
            .u1 = 0.75f, .v1 = 1.0f,
            .argb1 = UINT32_C(0xff778899),
            .oargb1 = UINT32_C(0xffaabbcc),
            .d1 = 1, .d2 = 2, .d3 = 3, .d4 = 4
        },
        {
            .flags = PVR_CMD_VERTEX_EOL,
            .x = -1.0f, .y = -2.0f, .z = -3.0f,
            .u0 = 0.125f, .v0 = 0.375f,
            .argb0 = UINT32_C(0xff010203),
            .oargb0 = UINT32_C(0xff040506),
            .u1 = 0.625f, .v1 = 0.875f,
            .argb1 = UINT32_C(0xff070809),
            .oargb1 = UINT32_C(0xff0a0b0c),
            .d1 = 5, .d2 = 6, .d3 = 7, .d4 = 8
        }
    };
    alignas(32) pvr_vertex_tpcm_t output[3];
    alignas(32) pvr_vertex_tpcm_t unchanged[3];
    alignas(32) pvr_vertex_pcm_t colors[2] = {
        {
            .flags = PVR_CMD_VERTEX,
            .x = 2.0f, .y = 4.0f, .z = 6.0f,
            .argb0 = UINT32_C(0xff102030),
            .argb1 = UINT32_C(0xff405060),
            .d1 = 9, .d2 = 10
        },
        {
            .flags = PVR_CMD_VERTEX_EOL,
            .x = 3.0f, .y = 5.0f, .z = 7.0f,
            .argb0 = UINT32_C(0xff708090),
            .argb1 = UINT32_C(0xffa0b0c0),
            .d1 = 11, .d2 = 12
        }
    };
    alignas(32) pvr_vertex_pcm_t color_output[2];
    pvr_geometry_vertex_stream_t stream = {
        textured, 2, sizeof(textured[0]),
        PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED
    };
    pvr_geometry_result_t result;
    alignas(32) pvr_modifier_vol_t modifier = {
        .flags = PVR_CMD_VERTEX_EOL,
        .ax = 1.0f, .ay = 2.0f, .az = 3.0f,
        .bx = 4.0f, .by = 5.0f, .bz = 6.0f,
        .cx = 7.0f, .cy = 8.0f, .cz = 9.0f,
        .d1 = 1, .d2 = 2, .d3 = 3, .d4 = 4, .d5 = 5, .d6 = 6
    };
    alignas(32) pvr_modifier_vol_t modifier_output;

    memset(output, 0x5a, sizeof(output));
    assert(pvr_geometry_project_vertices(output, 3, &stream, &transform,
                                         &result) == 0);
    assert(result.consumed_vertices == 2 && result.produced_vertices == 2);
    assert(output[0].x == 12.0f && output[0].y == 26.0f &&
           output[0].z == 1.0f);
    assert(output[1].x == 8.0f && output[1].y == 14.0f &&
           output[1].z == 1.0f);
    assert(!memcmp((const uint8_t *)&output[0] + 16u,
                   (const uint8_t *)&textured[0] + 16u,
                   sizeof(textured[0]) - 16u));
    assert(!memcmp((const uint8_t *)&output[1] + 16u,
                   (const uint8_t *)&textured[1] + 16u,
                   sizeof(textured[1]) - 16u));

    stream.vertices = colors;
    stream.vertex_count = 2;
    stream.stride = sizeof(colors[0]);
    stream.format = PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR;
    assert(pvr_geometry_project_vertices(color_output, 2, &stream, &transform,
                                         NULL) == 0);
    assert(color_output[0].x == 14.0f && color_output[0].y == 32.0f &&
           color_output[0].argb0 == colors[0].argb0 &&
           color_output[0].argb1 == colors[0].argb1 &&
           color_output[0].d1 == colors[0].d1);

    stream.vertices = textured;
    stream.stride = sizeof(textured[0]);
    stream.format = PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED;
    memcpy(output, textured, 2u * sizeof(textured[0]));
    stream.vertices = output;
    assert(pvr_geometry_project_vertices(output, 2, &stream, &transform,
                                         NULL) == 0);
    assert(output[0].x == 12.0f && output[1].x == 8.0f);

    stream.vertices = textured;
    textured[1].flags = UINT32_C(0xd0000000);
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_geometry_project_vertices(output, 3, &stream, &transform,
                                         &result) == -1);
    assert(errno == EILSEQ && result.produced_vertices == 1);
    assert(!memcmp(output + 1, unchanged + 1,
                   2u * sizeof(output[0])));
    textured[1].flags = PVR_CMD_VERTEX_EOL;

    errno = 0;
    assert(pvr_geometry_project_vertices(output, 1, &stream, &transform,
                                         &result) == -1);
    assert(errno == ENOSPC && result.produced_vertices == 0);

    stream.format = (pvr_geometry_vertex_format_t)99;
    errno = 0;
    assert(pvr_geometry_project_vertices(output, 3, &stream, &transform,
                                         &result) == -1);
    assert(errno == EINVAL && result.produced_vertices == 0);

    stream.format = PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED;
    stream.vertex_count = 2;
    errno = 0;
    assert(pvr_geometry_project_vertices((uint8_t *)textured + 32u, 2,
                                         &stream, &transform, &result) == -1);
    assert(errno == EINVAL && result.produced_vertices == 0);

    stream.vertices = &modifier;
    stream.vertex_count = 1;
    stream.stride = sizeof(modifier);
    stream.format = PVR_GEOMETRY_VERTEX_MODIFIER;
    assert(pvr_geometry_project_vertices(&modifier_output, 1, &stream,
                                         &transform, &result) == 0);
    assert(modifier_output.ax == 12.0f && modifier_output.ay == 26.0f &&
           modifier_output.az == 1.0f);
    assert(modifier_output.bx == 18.0f && modifier_output.by == 35.0f &&
           modifier_output.bz == 1.0f);
    assert(modifier_output.cx == 24.0f && modifier_output.cy == 44.0f &&
           modifier_output.cz == 1.0f);
    assert(modifier_output.d1 == 1 && modifier_output.d6 == 6);
}

static void test_vertex_sinks(void) {
    alignas(32) pvr_vertex_tpcm_t textured[3] = {
        { .flags = PVR_CMD_VERTEX, .argb0 = UINT32_C(0xff010203),
          .argb1 = UINT32_C(0xff040506) },
        { .flags = PVR_CMD_VERTEX, .argb0 = UINT32_C(0xff070809),
          .argb1 = UINT32_C(0xff0a0b0c) },
        { .flags = PVR_CMD_VERTEX_EOL, .argb0 = UINT32_C(0xff0d0e0f),
          .argb1 = UINT32_C(0xff101112) }
    };
    alignas(32) pvr_vertex_tpcm_t memory[3];
    alignas(32) pvr_vertex_tpcm_t unchanged[3];
    alignas(32) pvr_vertex_pcm_t colors[2] = {
        { .flags = PVR_CMD_VERTEX, .argb0 = UINT32_C(0xff112233),
          .argb1 = UINT32_C(0xff445566) },
        { .flags = PVR_CMD_VERTEX_EOL, .argb0 = UINT32_C(0xff778899),
          .argb1 = UINT32_C(0xffaabbcc) }
    };
    alignas(32) pvr_vertex_pcm_t color_memory[2];
    pvr_geometry_vertex_sink_t sink;

    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, memory, 3) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, textured, 2) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, textured + 2, 1) == 0);
    assert(sink.emitted_vertices == 3);
    assert(!memcmp(memory, textured, sizeof(textured)));

    errno = 0;
    assert(pvr_geometry_vertex_sink_emit(&sink, textured, 1) == -1);
    assert(errno == ENOSPC && sink.emitted_vertices == 3);

    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR,
        color_memory, 2) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, colors, 2) == 0);
    assert(!memcmp(color_memory, colors, sizeof(colors)));

    memset(memory, 0x5a, sizeof(memory));
    memcpy(unchanged, memory, sizeof(memory));
    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED, memory, 3) == 0);
    textured[1].flags = 0;
    errno = 0;
    assert(pvr_geometry_vertex_sink_emit(&sink, textured, 3) == -1);
    assert(errno == EILSEQ && sink.emitted_vertices == 0);
    assert(!memcmp(memory, unchanged, sizeof(memory)));
    textured[1].flags = PVR_CMD_VERTEX;

    submitted_count = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, textured, 2) == 0);
    assert(submitted_count == 4 && sink.emitted_vertices == 2);
    assert(!memcmp(submitted, textured, 2u * sizeof(textured[0])));

    submitted_count = 0;
    assert(pvr_geometry_vertex_sink_init_buffered(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR,
        PVR_LIST_TR_POLY) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, colors, 2) == 0);
    assert(submitted_count == 2 && submitted_list == PVR_LIST_TR_POLY);

    errno = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, (pvr_geometry_vertex_format_t)99) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_geometry_vertex_sink_init_buffered(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR,
        PVR_LIST_OP_MOD) == -1);
    assert(errno == EINVAL);

    assert(pvr_geometry_vertex_sink_init_buffered(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, PVR_LIST_OP_MOD) == 0);
    errno = 0;
    assert(pvr_geometry_vertex_sink_init_buffered(
        &sink, PVR_GEOMETRY_VERTEX_MODIFIER, PVR_LIST_OP_POLY) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_geometry_vertex_sink_init_buffered(
        &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR,
        PVR_LIST_OP_MOD) == -1);
    assert(errno == EINVAL);
}

static const matrix_t identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static void test_frustum_classification(void) {
    pvr_frustum_t frustum;
    point_t minimum;
    point_t maximum;
    pvr_frustum_classification_t result;
    pvr_frustum_t unchanged;

    assert(pvr_frustum_init(&frustum, &identity, -1.0f, -1.0f,
                            1.0f, 1.0f, 0.5f, 2.0f) == 0);

    minimum = (point_t){ -0.5f, -0.5f, -0.5f, 1.0f };
    maximum = (point_t){ 0.5f, 0.5f, 0.5f, 1.0f };
    assert(pvr_frustum_classify_aabb(&frustum, &minimum, &maximum,
                                     &result) == 0);
    assert(result == PVR_FRUSTUM_INSIDE);

    minimum.x = 1.5f;
    maximum.x = 2.5f;
    assert(pvr_frustum_classify_aabb(&frustum, &minimum, &maximum,
                                     &result) == 0);
    assert(result == PVR_FRUSTUM_OUTSIDE);

    minimum.x = 0.5f;
    maximum.x = 1.5f;
    assert(pvr_frustum_classify_aabb(&frustum, &minimum, &maximum,
                                     &result) == 0);
    assert(result == PVR_FRUSTUM_INTERSECT);

    unchanged = frustum;
    errno = 0;
    assert(pvr_frustum_init(&frustum, &identity, 1.0f, -1.0f,
                            -1.0f, 1.0f, 0.5f, 2.0f) == -1);
    assert(errno == EDOM);
    assert(memcmp(&frustum, &unchanged, sizeof(frustum)) == 0);

    minimum.x = NAN;
    result = PVR_FRUSTUM_OUTSIDE;
    errno = 0;
    assert(pvr_frustum_classify_aabb(&frustum, &minimum, &maximum,
                                     &result) == -1);
    assert(errno == EDOM && result == PVR_FRUSTUM_OUTSIDE);
}

static void test_frustum_clipping(void) {
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t input[3];
    alignas(32) pvr_vertex_t output[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_vertex_t unchanged[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_frustum_clip_result_t result;

    assert(pvr_frustum_init(&frustum, &identity, -1.0f, -1.0f,
                            1.0f, 1.0f, 0.5f, 2.0f) == 0);
    input[0] = make_vertex(-0.5f, -0.5f, 0.0f, PVR_CMD_VERTEX,
                           UINT32_C(0xff000000));
    input[1] = make_vertex(0.5f, -0.5f, 0.0f, PVR_CMD_VERTEX,
                           UINT32_C(0xffffffff));
    input[2] = make_vertex(0.0f, 0.5f, 0.0f, PVR_CMD_VERTEX_EOL,
                           UINT32_C(0xff808080));

    assert(pvr_frustum_clip_triangle(output,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     input, &frustum,
                                     PVR_FRUSTUM_CLIP_ALL, &result) == 0);
    assert(result.polygon_vertices == 3 && result.output_vertices == 3);
    assert(output[0].flags == PVR_CMD_VERTEX);
    assert(output[1].flags == PVR_CMD_VERTEX);
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
    assert(close_enough(output[0].x, -0.5f));
    assert(close_enough(output[0].z, 1.0f));

    /* Crossing one side produces a clipped quad and two independent
       triangles, without requiring the caller to repair strip topology. */
    input[0].x = -2.0f;
    input[0].u = 0.0f;
    input[1].u = 1.0f;
    input[2].u = 1.0f;
    assert(pvr_frustum_clip_triangle(output,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     input, &frustum,
                                     PVR_FRUSTUM_CLIP_ALL, &result) == 0);
    assert(result.polygon_vertices == 4 && result.output_vertices == 6);
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);
    assert(output[5].flags == PVR_CMD_VERTEX_EOL);
    assert(output[0].x >= -1.0f && output[1].x >= -1.0f &&
           output[2].x >= -1.0f && output[3].x >= -1.0f &&
           output[4].x >= -1.0f && output[5].x >= -1.0f);

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_frustum_clip_triangle(output, 5, input, &frustum,
                                     PVR_FRUSTUM_CLIP_ALL, &result) == -1);
    assert(errno == ENOSPC && result.output_vertices == 6);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);

    input[0].x = -3.0f;
    input[1].x = -2.0f;
    input[2].x = -2.5f;
    assert(pvr_frustum_clip_triangle(output,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     input, &frustum, 0, &result) == 0);
    assert(result.polygon_vertices == 0 && result.output_vertices == 0);
    assert(memcmp(output, unchanged, sizeof(output)) == 0);

    input[0].flags = UINT32_C(0xd0000000);
    errno = 0;
    assert(pvr_frustum_clip_triangle(output,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     input, &frustum, 0, &result) == -1);
    assert(errno == EILSEQ);
}

static void test_example_frustum(void) {
    alignas(32) const matrix_t projection = {
        { 240.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 240.0f, 0.0f, 0.0f },
        { -320.0f, -240.0f, -1.02020202f, -1.0f },
        { 320.0f, 240.0f, -2.02020202f, 1.0f }
    };
    alignas(32) pvr_vertex_t input[3] = {
        { .flags = PVR_CMD_VERTEX, .x = -5.0f, .y = -0.6f, .z = -2.0f,
          .argb = UINT32_C(0xffff4040) },
        { .flags = PVR_CMD_VERTEX, .x = 0.0f, .y = 0.8f, .z = -2.0f,
          .argb = UINT32_C(0xff40ff40) },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 0.8f, .y = -0.6f, .z = -2.0f,
          .argb = UINT32_C(0xff4040ff) }
    };
    alignas(32) pvr_vertex_t output[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    pvr_frustum_t frustum;
    pvr_frustum_clip_result_t result;

    assert(pvr_frustum_init(&frustum, &projection, 0.0f, 0.0f,
                            640.0f, 480.0f, 2.0f, 101.0f) == 0);
    assert(pvr_frustum_clip_triangle(output,
                                     PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                     input, &frustum,
                                     PVR_FRUSTUM_CLIP_ALL, &result) == 0);
    assert(result.polygon_vertices == 4 && result.output_vertices == 6);
    assert(close_enough(output[0].x, 0.0f));
    assert(close_enough(output[0].z, 1.0f / 3.0f));
}

int main(void) {
    test_projection();
    test_projection_failures();
    test_sinks();
    test_format_projection();
    test_vertex_sinks();
    test_frustum_classification();
    test_frustum_clipping();
    test_example_frustum();
    puts("PVR geometry tests passed");
    return 0;
}
