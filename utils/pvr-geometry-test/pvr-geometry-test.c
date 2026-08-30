/* KallistiOS ##version##

   Host-side PVR geometry contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_geometry.h>
#include <dc/pvr_frustum.h>
#include <dc/pvr_sprite_geometry.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct extended_vertex {
    pvr_vertex_t vertex;
    uint32_t application_data[8];
} extended_vertex_t;

alignas(32) static pvr_vertex_t submitted[8];
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

static void test_line_geometry(void) {
    alignas(32) pvr_vertex_t endpoints[2] = {
        {
            .flags = PVR_CMD_VERTEX,
            .x = 10.0f, .y = 20.0f, .z = 0.5f,
            .u = 0.0f, .v = 0.0f,
            .argb = UINT32_C(0xff102030),
            .oargb = UINT32_C(0xff010203)
        },
        {
            .flags = PVR_CMD_VERTEX_EOL,
            .x = 20.0f, .y = 20.0f, .z = 0.25f,
            .u = 1.0f, .v = 1.0f,
            .argb = UINT32_C(0xff405060),
            .oargb = UINT32_C(0xff040506)
        }
    };
    alignas(32) pvr_vertex_t output[PVR_GEOMETRY_LINE_VERTICES];
    alignas(32) pvr_vertex_t unchanged[PVR_GEOMETRY_LINE_VERTICES];
    pvr_geometry_result_t result;

    assert(pvr_geometry_expand_line(output, endpoints, 4.0f, &result) == 0);
    assert(result.consumed_vertices == 2 && result.produced_vertices == 4);
    assert(output[0].x == 10.0f && output[0].y == 22.0f &&
           output[1].x == 10.0f && output[1].y == 18.0f &&
           output[2].x == 20.0f && output[2].y == 22.0f &&
           output[3].x == 20.0f && output[3].y == 18.0f);
    assert(output[0].z == 0.5f && output[1].argb == endpoints[0].argb &&
           output[2].z == 0.25f && output[3].argb == endpoints[1].argb);
    assert(output[0].flags == PVR_CMD_VERTEX &&
           output[2].flags == PVR_CMD_VERTEX &&
           output[3].flags == PVR_CMD_VERTEX_EOL);

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    endpoints[1].x = endpoints[0].x;
    endpoints[1].y = endpoints[0].y;
    assert(pvr_geometry_expand_line(output, endpoints, 4.0f, &result) == 0);
    assert(result.consumed_vertices == 2 && result.produced_vertices == 0 &&
           !memcmp(output, unchanged, sizeof(output)));

    errno = 0;
    assert(pvr_geometry_expand_line(output, endpoints, 0.0f, &result) == -1);
    assert(errno == EINVAL && result.consumed_vertices == 0 &&
           !memcmp(output, unchanged, sizeof(output)));
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
    alignas(32) pvr_sprite_txr_t sprite = {
        .flags = PVR_CMD_VERTEX_EOL,
        .ax = 1.0f, .ay = 2.0f, .az = 3.0f,
        .bx = 1.0f, .by = 4.0f, .bz = 3.0f,
        .cx = 5.0f, .cy = 4.0f, .cz = 3.0f,
        .dx = 5.0f, .dy = 2.0f,
        .dummy = UINT32_C(0x11223344),
        .auv = UINT32_C(0x01020304),
        .buv = UINT32_C(0x05060708),
        .cuv = UINT32_C(0x090a0b0c)
    };
    alignas(32) pvr_sprite_txr_t sprite_output;

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

    stream.vertices = &sprite;
    stream.vertex_count = 1;
    stream.stride = sizeof(sprite);
    stream.format = PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED;
    assert(pvr_geometry_project_vertices(&sprite_output, 1, &stream,
                                         &transform, &result) == 0);
    assert(result.consumed_vertices == 1 && result.produced_vertices == 1);
    assert(sprite_output.ax == 12.0f && sprite_output.ay == 26.0f &&
           sprite_output.az == 1.0f);
    assert(sprite_output.bx == 12.0f && sprite_output.by == 32.0f &&
           sprite_output.bz == 1.0f);
    assert(sprite_output.cx == 20.0f && sprite_output.cy == 32.0f &&
           sprite_output.cz == 1.0f);
    assert(sprite_output.dx == 20.0f && sprite_output.dy == 26.0f);
    assert(sprite_output.dummy == sprite.dummy &&
           sprite_output.auv == sprite.auv &&
           sprite_output.buv == sprite.buv &&
           sprite_output.cuv == sprite.cuv);

    memcpy(&sprite_output, &sprite, sizeof(sprite));
    stream.vertices = &sprite_output;
    assert(pvr_geometry_project_vertices(&sprite_output, 1, &stream,
                                         &transform, NULL) == 0);
    assert(sprite_output.ax == 12.0f && sprite_output.dx == 20.0f);
}

static void test_sprite_cells(void) {
    alignas(32) pvr_sprite_cell_t cells[2] = {
        { 20.0f, 10.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f },
        { 8.0f, 12.0f, 0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 1.0f }
    };
    const pvr_sprite_atlas_t atlas = { cells, 2 };
    pvr_sprite_instance_t instances[3] = {
        {
            .cell_index = 0,
            .position = { 100.0f, 80.0f, 0.5f, 1.0f },
            .rotation = 0.0f,
            .scale_x = 2.0f,
            .scale_y = 1.0f,
            .flags = PVR_SPRITE_INSTANCE_NONE
        },
        {
            .cell_index = 1,
            .position = { 0.0f, 0.0f, 0.5f, 1.0f },
            .rotation = 0.0f,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .flags = PVR_SPRITE_INSTANCE_HIDDEN
        },
        {
            .cell_index = 1,
            .position = { 200.0f, 100.0f, 0.25f, 1.0f },
            .rotation = 1.57079632679489661923f,
            .scale_x = 1.0f,
            .scale_y = 0.5f,
            .flags = PVR_SPRITE_INSTANCE_FLIP_U |
                     PVR_SPRITE_INSTANCE_FLIP_V
        }
    };
    pvr_sprite_instance_stream_t stream = {
        instances, 3, sizeof(instances[0])
    };
    alignas(32) pvr_sprite_txr_t output[3];
    alignas(32) pvr_sprite_txr_t unchanged[3];
    pvr_sprite_batch_result_t result;
    const pvr_sprite_billboard_basis_t basis = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f }
    };
    alignas(32) const matrix_t projection = {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 10.0f, 20.0f, 0.0f, 1.0f }
    };

    memset(output, 0x5a, sizeof(output));
    assert(pvr_sprite_batch_compile_2d(output, 3, &atlas, &stream,
                                       &result) == 0);
    assert(result.examined_instances == 3 && result.produced_sprites == 2);
    assert(output[0].flags == PVR_CMD_VERTEX_EOL);
    assert(output[0].ax == 80.0f && output[0].ay == 85.0f &&
           output[0].az == 0.5f);
    assert(output[0].bx == 80.0f && output[0].by == 75.0f &&
           output[0].bz == 0.5f);
    assert(output[0].cx == 120.0f && output[0].cy == 75.0f &&
           output[0].cz == 0.5f);
    assert(output[0].dx == 120.0f && output[0].dy == 85.0f);
    assert(output[0].auv == PVR_PACK_16BIT_UV(0.0f, 0.5f));
    assert(output[0].buv == PVR_PACK_16BIT_UV(0.0f, 0.0f));
    assert(output[0].cuv == PVR_PACK_16BIT_UV(0.5f, 0.0f));

    assert(close_enough(output[1].ax, 194.0f));
    assert(close_enough(output[1].ay, 100.0f));
    assert(close_enough(output[1].bx, 200.0f));
    assert(close_enough(output[1].by, 100.0f));
    assert(close_enough(output[1].cx, 200.0f));
    assert(close_enough(output[1].cy, 108.0f));
    assert(close_enough(output[1].dx, 194.0f));
    assert(close_enough(output[1].dy, 108.0f));
    assert(output[1].auv == PVR_PACK_16BIT_UV(1.0f, 0.25f));
    assert(output[1].buv == PVR_PACK_16BIT_UV(1.0f, 1.0f));
    assert(output[1].cuv == PVR_PACK_16BIT_UV(0.5f, 1.0f));

    /* A fully hidden batch needs no destination storage and publishes
       nothing, even if the aligned placeholder aliases read-only inputs. */
    assert(pvr_sprite_batch_compile_2d((pvr_sprite_txr_t *)(void *)cells, 0,
        &atlas,
        &(pvr_sprite_instance_stream_t){ instances + 1, 1,
                                         sizeof(instances[0]) },
        &result) == 0);
    assert(result.examined_instances == 1 && result.produced_sprites == 0);

    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_sprite_batch_compile_2d(output, 1, &atlas, &stream,
                                       &result) == -1);
    assert(errno == ENOSPC && result.examined_instances == 0 &&
           result.produced_sprites == 0);
    assert(!memcmp(output, unchanged, sizeof(output)));

    instances[2].cell_index = 2;
    errno = 0;
    assert(pvr_sprite_batch_compile_2d(output, 3, &atlas, &stream,
                                       &result) == -1);
    assert(errno == EINVAL && !memcmp(output, unchanged, sizeof(output)));
    instances[2].cell_index = 1;

    instances[2].scale_x = FLT_MAX;
    errno = 0;
    assert(pvr_sprite_batch_compile_2d(output, 3, &atlas, &stream,
                                       &result) == -1);
    assert(errno == ERANGE && !memcmp(output, unchanged, sizeof(output)));
    instances[2].scale_x = 1.0f;

    /* Pivots outside a cell are useful for orbiting attached sprites. */
    cells[0].origin_x = 1.5f;
    assert(pvr_sprite_batch_compile_2d(output, 3, &atlas, &stream,
                                       &result) == 0);
    cells[0].origin_x = 0.5f;

    assert(pvr_sprite_batch_compile_3d(output, 3, &atlas, &stream, &basis,
                                       &projection, &result) == 0);
    assert(result.examined_instances == 3 && result.produced_sprites == 2);
    assert(output[0].ax == 170.0f && output[0].ay == 275.0f &&
           output[0].az == 1.0f);
    assert(output[0].dx == 250.0f && output[0].dy == 275.0f);
    assert(close_enough(output[1].ax, 398.0f));
    assert(close_enough(output[1].dy, 344.0f));

    errno = 0;
    assert(pvr_sprite_batch_compile_2d(output, 3, &atlas,
        &(pvr_sprite_instance_stream_t){ output, 1, sizeof(output[0]) },
        &result) == -1);
    assert(errno == EINVAL);
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
    alignas(32) pvr_sprite_txr_t sprites[2] = {
        { .flags = PVR_CMD_VERTEX_EOL, .ax = 1.0f, .auv = 1 },
        { .flags = PVR_CMD_VERTEX_EOL, .ax = 2.0f, .auv = 2 }
    };
    alignas(32) pvr_sprite_txr_t sprite_memory[2];
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

    assert(pvr_geometry_vertex_sink_init_memory(
        &sink, PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED,
        sprite_memory, 2) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, sprites, 2) == 0);
    assert(!memcmp(sprite_memory, sprites, sizeof(sprites)));

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

    submitted_count = 0;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, sprites, 2) == 0);
    assert(submitted_count == 4 && sink.emitted_vertices == 2);
    assert(!memcmp(submitted, sprites, sizeof(sprites)));

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
    point_t center;
    alignas(32) pvr_vertex_t triangle[3];

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

    center = (point_t){ 0.0f, 0.0f, 0.0f, 1.0f };
    assert(pvr_frustum_classify_sphere(&frustum, &center, 0.4f,
                                       &result) == 0);
    assert(result == PVR_FRUSTUM_INSIDE);

    center.x = 1.5f;
    assert(pvr_frustum_classify_sphere(&frustum, &center, 0.4f,
                                       &result) == 0);
    assert(result == PVR_FRUSTUM_OUTSIDE);

    center.x = 0.8f;
    assert(pvr_frustum_classify_sphere(&frustum, &center, 0.4f,
                                       &result) == 0);
    assert(result == PVR_FRUSTUM_INTERSECT);

    center.x = 0.0f;
    result = PVR_FRUSTUM_OUTSIDE;
    errno = 0;
    assert(pvr_frustum_classify_sphere(&frustum, &center, -1.0f,
                                       &result) == -1);
    assert(errno == EDOM && result == PVR_FRUSTUM_OUTSIDE);

    triangle[0] = make_vertex(-0.5f, -0.5f, 0.0f, PVR_CMD_VERTEX,
                              UINT32_C(0xffffffff));
    triangle[1] = make_vertex(0.5f, -0.5f, 0.0f, PVR_CMD_VERTEX,
                              UINT32_C(0xffffffff));
    triangle[2] = make_vertex(0.0f, 0.5f, 0.0f, PVR_CMD_VERTEX_EOL,
                              UINT32_C(0xffffffff));
    assert(pvr_frustum_classify_triangle(triangle, &frustum, &result) == 0);
    assert(result == PVR_FRUSTUM_INSIDE);

    triangle[2].x = 1.5f;
    assert(pvr_frustum_classify_triangle(triangle, &frustum, &result) == 0);
    assert(result == PVR_FRUSTUM_INTERSECT);

    triangle[0].x = 1.5f;
    triangle[1].x = 1.5f;
    assert(pvr_frustum_classify_triangle(triangle, &frustum, &result) == 0);
    assert(result == PVR_FRUSTUM_OUTSIDE);

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

static void test_frustum_segment_clipping(void) {
    pvr_frustum_t frustum;
    alignas(32) pvr_vertex_t input[2];
    alignas(32) pvr_vertex_t output[2];
    alignas(32) pvr_vertex_t unchanged[2];
    pvr_frustum_segment_result_t result;

    assert(pvr_frustum_init(&frustum, &identity, -1.0f, -1.0f,
                            1.0f, 1.0f, 0.5f, 2.0f) == 0);
    input[0] = make_vertex(-2.0f, 0.0f, 0.0f, PVR_CMD_VERTEX,
                           UINT32_C(0xff000000));
    input[1] = make_vertex(0.0f, 0.0f, 0.0f, PVR_CMD_VERTEX_EOL,
                           UINT32_C(0xffffffff));
    input[0].u = 0.0f;
    input[1].u = 1.0f;
    assert(pvr_frustum_clip_segment(output, input, &frustum,
                                    PVR_FRUSTUM_CLIP_ALL, &result) == 0);
    assert(result.visible == 1 && result.clipped == 1);
    assert(output[0].x == -1.0f && output[1].x == 0.0f &&
           output[0].z == 1.0f && output[1].z == 1.0f);
    assert(close_enough(output[0].u, 0.5f));
    assert(output[0].argb == UINT32_C(0xff808080));
    assert(output[0].flags == PVR_CMD_VERTEX &&
           output[1].flags == PVR_CMD_VERTEX_EOL);

    input[0].x = -3.0f;
    input[1].x = -2.0f;
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_frustum_clip_segment(output, input, &frustum, 0,
                                    &result) == 0);
    assert(result.visible == 0 && result.clipped == 0 &&
           !memcmp(output, unchanged, sizeof(output)));

    input[0].x = -0.5f;
    input[1].x = 0.5f;
    assert(pvr_frustum_clip_segment(output, input, &frustum, 0,
                                    &result) == 0);
    assert(result.visible == 1 && result.clipped == 0 &&
           output[0].x == -0.5f && output[1].x == 0.5f);
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

static void test_modifier_near_warp(void) {
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    alignas(32) pvr_modifier_vol_t input = {
        .flags = PVR_CMD_VERTEX_EOL,
        .ax = 2.0f, .ay = 4.0f, .az = 0.0f,
        .bx = -2.0f, .by = 6.0f, .bz = 0.0f,
        .cx = 8.0f, .cy = -4.0f, .cz = 0.0f,
        .d1 = 1, .d2 = 2, .d3 = 3, .d4 = 4, .d5 = 5, .d6 = 6
    };
    alignas(32) pvr_modifier_vol_t output;
    alignas(32) pvr_modifier_vol_t unchanged;
    pvr_frustum_t frustum;

    assert(pvr_frustum_init(&frustum, &identity, -100.0f, -100.0f,
                            100.0f, 100.0f, 2.0f, 10.0f) == 0);
    assert(pvr_frustum_project_modifier_warp(&output, &input, &frustum) == 0);
    assert(close_enough(output.ax, 1.0f) &&
           close_enough(output.ay, 2.0f) &&
           close_enough(output.az, 0.5f));
    assert(close_enough(output.bx, -1.0f) &&
           close_enough(output.by, 3.0f) &&
           close_enough(output.bz, 0.5f));
    assert(close_enough(output.cx, 4.0f) &&
           close_enough(output.cy, -2.0f) &&
           close_enough(output.cz, 0.5f));
    assert(output.flags == input.flags && output.d1 == 1 && output.d6 == 6);

    input.flags = UINT32_C(0xd0000000);
    memset(&output, 0x5a, sizeof(output));
    unchanged = output;
    errno = 0;
    assert(pvr_frustum_project_modifier_warp(&output, &input, &frustum) == -1);
    assert(errno == EILSEQ && !memcmp(&output, &unchanged, sizeof(output)));
}

int main(void) {
    test_projection();
    test_projection_failures();
    test_line_geometry();
    test_sinks();
    test_format_projection();
    test_sprite_cells();
    test_vertex_sinks();
    test_frustum_classification();
    test_frustum_clipping();
    test_frustum_segment_clipping();
    test_example_frustum();
    test_modifier_near_warp();
    puts("PVR geometry tests passed");
    return 0;
}
