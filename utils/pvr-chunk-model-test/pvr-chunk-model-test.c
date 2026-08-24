/* KallistiOS ##version##

   Host-side compact PVR model validation tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t valid_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0x00000000),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000), UINT32_C(0x00000000),
    UINT32_C(0x000000ff)
};

static const uint16_t valid_polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0xffff), UINT16_C(0xffff),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static pvr_chunk_model_t model_with(const uint32_t *vertices,
                                    size_t vertex_words,
                                    const uint16_t *polygons,
                                    size_t polygon_words) {
    pvr_chunk_model_t model = {
        .vertex_words = vertices,
        .vertex_word_count = vertex_words,
        .polygon_words = polygons,
        .polygon_word_count = polygon_words,
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 1.5f
    };

    return model;
}

static void test_valid_model(void) {
    pvr_chunk_model_t model = model_with(
        valid_vertices, sizeof(valid_vertices) / sizeof(valid_vertices[0]),
        valid_polygons, sizeof(valid_polygons) / sizeof(valid_polygons[0]));
    pvr_chunk_model_info_t info;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;

    assert(pvr_chunk_model_validate(&model, &info) == 0);
    assert(info.vertex_records == 1);
    assert(info.vertex_entries == 3);
    assert(info.polygon_records == 2);
    assert(info.material_records == 1);
    assert(info.strip_records == 1);
    assert(info.strips == 1);
    assert(info.triangles == 1);
    assert(info.index_references == 3);
    assert(info.maximum_vertex_index == 2);

    assert(pvr_chunk_vertex_iterator_init(&iterator, valid_vertices,
        sizeof(valid_vertices) / sizeof(valid_vertices[0])) == 0);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_VERTEX);
    assert(record.type == PVR_CHUNK_VERTEX_XYZ);
    assert(record.word_count == 11 && record.payload_word_count == 10);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_END);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 0);
}

static void expect_invalid(const pvr_chunk_model_t *model, int error) {
    pvr_chunk_model_info_t info;

    memset(&info, 0x5a, sizeof(info));
    errno = 0;
    assert(pvr_chunk_model_validate(model, &info) == -1);
    assert(errno == error);
    assert(info.vertex_records == 0 && info.triangles == 0);
}

static void test_bad_streams(void) {
    uint32_t vertices[sizeof(valid_vertices) / sizeof(valid_vertices[0])];
    uint16_t polygons[sizeof(valid_polygons) / sizeof(valid_polygons[0])];
    pvr_chunk_model_t model;

    memcpy(vertices, valid_vertices, sizeof(vertices));
    memcpy(polygons, valid_polygons, sizeof(polygons));
    model = model_with(vertices, sizeof(vertices) / sizeof(vertices[0]),
                       polygons, sizeof(polygons) / sizeof(polygons[0]));

    vertices[0] = VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 9);
    expect_invalid(&model, EILSEQ);
    vertices[0] = valid_vertices[0];

    vertices[2] = UINT32_C(0x7fc00000);
    expect_invalid(&model, EILSEQ);
    vertices[2] = valid_vertices[2];

    polygons[10] = UINT16_C(3);
    expect_invalid(&model, EILSEQ);
    polygons[10] = valid_polygons[10];

    polygons[5] = UINT16_C(4);
    expect_invalid(&model, EILSEQ);
    polygons[5] = valid_polygons[5];

    polygons[1] = UINT16_C(1);
    expect_invalid(&model, EILSEQ);
    polygons[1] = valid_polygons[1];

    vertices[sizeof(vertices) / sizeof(vertices[0]) - 1u] = UINT32_C(0);
    expect_invalid(&model, EILSEQ);
    vertices[sizeof(vertices) / sizeof(vertices[0]) - 1u] = UINT32_C(0xff);

    model.vertex_word_count--;
    expect_invalid(&model, EILSEQ);
    model.vertex_word_count++;

    model.radius = NAN;
    expect_invalid(&model, EILSEQ);
}

static void test_user_flags_and_reverse_strip(void) {
    uint16_t polygons[] = {
        PVR_CHUNK_STRIP_UV8, UINT16_C(16), UINT16_C(0x4001),
        UINT16_C(0x8003),
        UINT16_C(0), UINT16_C(0), UINT16_C(0),
        UINT16_C(1), UINT16_C(128), UINT16_C(0),
        UINT16_C(2), UINT16_C(64), UINT16_C(255), UINT16_C(0xbeef),
        UINT16_C(0x00ff)
    };
    pvr_chunk_model_t model = model_with(
        valid_vertices, sizeof(valid_vertices) / sizeof(valid_vertices[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]));
    pvr_chunk_model_info_t info;

    /* The declared payload is intentionally one word too long. */
    expect_invalid(&model, EILSEQ);

    polygons[1] = UINT16_C(12);
    assert(pvr_chunk_model_validate(&model, &info) == 0);
    assert(info.strips == 1 && info.triangles == 1);
}

static uint32_t random_state = UINT32_C(0x9e3779b9);

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_bounded_random_streams(void) {
    uint32_t vertices[24];
    uint16_t polygons[48];
    size_t iteration;

    for(iteration = 0; iteration < 5000; ++iteration) {
        pvr_chunk_model_t model;
        pvr_chunk_model_info_t info;
        size_t i;

        for(i = 0; i < sizeof(vertices) / sizeof(vertices[0]); ++i)
            vertices[i] = next_random();
        for(i = 0; i < sizeof(polygons) / sizeof(polygons[0]); ++i)
            polygons[i] = (uint16_t)next_random();

        model = model_with(vertices,
                           1u + next_random() %
                           (sizeof(vertices) / sizeof(vertices[0])),
                           polygons,
                           1u + next_random() %
                           (sizeof(polygons) / sizeof(polygons[0])));
        (void)pvr_chunk_model_validate(&model, &info);
    }
}

int main(void) {
    test_valid_model();
    test_bad_streams();
    test_user_flags_and_reverse_strip();
    test_bounded_random_streams();
    puts("pvr chunk model tests: PASS");
    return 0;
}
