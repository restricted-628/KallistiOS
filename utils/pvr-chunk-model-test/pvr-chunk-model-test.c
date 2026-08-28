/* KallistiOS ##version##

   Host-side compact PVR model validation tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 16))

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00002f;
}

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

static const uint32_t sparse_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 4),
    UINT32_C(0x00010001),
    UINT32_C(0x3f800000), UINT32_C(0x40000000), UINT32_C(0x40400000),
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 4),
    UINT32_C(0x0001ffff),
    UINT32_C(0x40800000), UINT32_C(0x40a00000), UINT32_C(0x40c00000),
    UINT32_C(0x000000ff)
};

static const uint16_t sparse_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(1), UINT16_C(0xffff), UINT16_C(1),
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
    pvr_chunk_model_view_t view;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    pvr_chunk_vertex_batch_t batch;
    pvr_chunk_vertex_view_t vertex;
    pvr_chunk_vertex_attributes_t attributes;
    pvr_chunk_strip_iterator_t strip_iterator;
    pvr_chunk_strip_view_t strip;
    pvr_chunk_strip_vertex_view_t strip_vertex;

    assert(pvr_chunk_model_validate(&model, &info) == 0);
    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(memcmp(&view.info, &info, sizeof(info)) == 0);
    assert(info.vertex_records == 1);
    assert(info.vertex_entries == 3);
    assert(info.polygon_records == 2);
    assert(info.material_records == 1);
    assert(info.strip_records == 1);
    assert(info.strips == 1);
    assert(info.triangles == 1);
    assert(info.index_references == 3);
    assert(info.maximum_strip_vertices == 3);
    assert(info.maximum_vertex_index == 2);
    assert(pvr_chunk_model_vertex_attributes_get(&view, 1, &attributes) == 0);
    assert(attributes.index == 1 && attributes.position.x == 1.0f &&
           attributes.position.y == -1.0f);
    errno = 0;
    assert(pvr_chunk_model_vertex_attributes_get(&view, 3,
                                                  &attributes) == -1);
    assert(errno == ENOENT && attributes.present == 0);

    assert(pvr_chunk_vertex_iterator_init(&iterator, valid_vertices,
        sizeof(valid_vertices) / sizeof(valid_vertices[0])) == 0);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_VERTEX);
    assert(record.type == PVR_CHUNK_VERTEX_XYZ);
    assert(record.word_count == 11 && record.payload_word_count == 10);
    assert(pvr_chunk_vertex_batch_decode(&record, &batch) == 0);
    assert(batch.first_index == 0 && batch.entry_count == 3);
    assert(batch.entry_word_count == 3);
    assert(pvr_chunk_vertex_batch_get(&batch, 0, &vertex) == 0);
    assert(vertex.index == 0 && vertex.position_components == 3);
    assert(vertex.position[0] == -1.0f && vertex.position[1] == -1.0f &&
           vertex.position[2] == 0.0f && vertex.position[3] == 1.0f);
    assert(pvr_chunk_vertex_batch_get(&batch, 2, &vertex) == 0);
    assert(vertex.index == 2 && vertex.position[0] == 0.0f &&
           vertex.position[1] == 1.0f);
    errno = 0;
    assert(pvr_chunk_vertex_batch_get(&batch, 3, &vertex) == -1 &&
           errno == EINVAL && vertex.words == NULL);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_END);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 0);

    assert(pvr_chunk_polygon_iterator_init(&iterator, valid_polygons,
        sizeof(valid_polygons) / sizeof(valid_polygons[0])) == 0);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_MATERIAL);
    assert(pvr_chunk_iterator_next(&iterator, &record) == 1);
    assert(record.record_class == PVR_CHUNK_RECORD_STRIP);
    assert(pvr_chunk_strip_iterator_init(&strip_iterator, &record) == 0);
    assert(pvr_chunk_strip_iterator_next(&strip_iterator, &strip) == 1);
    assert(strip.vertex_count == 3 && strip.vertex_word_count == 1 &&
           strip.user_word_count == 0 && !strip.reversed);
    assert(pvr_chunk_strip_vertex_get(&strip, 0, &strip_vertex) == 0);
    assert(strip_vertex.index == 0 &&
           strip_vertex.attribute_word_count == 0);
    assert(pvr_chunk_strip_vertex_get(&strip, 2, &strip_vertex) == 0);
    assert(strip_vertex.index == 2 &&
           strip_vertex.triangle_user_word_count == 0);
    assert(pvr_chunk_strip_iterator_next(&strip_iterator, &strip) == 0);
}

static void test_prepared_plan(void) {
    pvr_chunk_model_t model = model_with(
        valid_vertices, sizeof(valid_vertices) / sizeof(valid_vertices[0]),
        valid_polygons, sizeof(valid_polygons) / sizeof(valid_polygons[0]));
    pvr_chunk_model_t sparse = model_with(
        sparse_vertices, sizeof(sparse_vertices) / sizeof(sparse_vertices[0]),
        sparse_polygons, sizeof(sparse_polygons) / sizeof(sparse_polygons[0]));
    pvr_chunk_model_view_t view;
    pvr_chunk_model_view_t sparse_view;
    pvr_chunk_model_plan_requirements_t requirements;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t entries[512];
    pvr_chunk_vertex_attributes_t immediate;
    pvr_chunk_vertex_attributes_t prepared;
    uint16_t saved_page;
    uint16_t saved_reserved;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &requirements) == 0);
    assert(requirements.indexed_pages == 1);
    assert(requirements.vertex_index_entries ==
           PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE);
    assert(requirements.vertex_index_bytes ==
           requirements.vertex_index_entries * sizeof(entries[0]));

    errno = 0;
    assert(pvr_chunk_model_plan_build(
               &view, entries, requirements.vertex_index_entries - 1u,
               &plan) == -1);
    assert(errno == ENOSPC);

    assert(pvr_chunk_model_plan_build(
               &view, entries, requirements.vertex_index_entries,
               &plan) == 0);
    assert(plan.indexed_page_count == 1);
    assert(plan.vertex_index_count == requirements.vertex_index_entries);
    assert(plan.vertex_page_slots[0] == 1);
    assert(plan.vertex_page_slots[1] == 0);
    assert(pvr_chunk_model_vertex_attributes_get(&view, 2, &immediate) == 0);
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, 2, &prepared) == 0);
    assert(prepared.index == immediate.index);
    assert(prepared.position.x == immediate.position.x);
    assert(prepared.position.y == immediate.position.y);
    assert(prepared.position.z == immediate.position.z);
    errno = 0;
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, 3, &prepared) == -1);
    assert(errno == ENOENT && prepared.present == 0);

    saved_page = plan.vertex_page_slots[0];
    plan.vertex_page_slots[0] = 2;
    errno = 0;
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, 0, &prepared) == -1);
    assert(errno == EILSEQ);
    plan.vertex_page_slots[0] = saved_page;

    saved_reserved = entries[0].reserved;
    entries[0].reserved = 1;
    errno = 0;
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, 0, &prepared) == -1);
    assert(errno == EILSEQ);
    entries[0].reserved = saved_reserved;

    assert(pvr_chunk_model_open(&sparse, &sparse_view) == 0);
    assert(pvr_chunk_model_plan_query(&sparse_view, &requirements) == 0);
    assert(requirements.indexed_pages == 2);
    assert(requirements.vertex_index_entries == 512);
    assert(pvr_chunk_model_plan_build(&sparse_view, entries, 512,
                                      &plan) == 0);
    assert(plan.vertex_page_slots[0] == 1);
    assert(plan.vertex_page_slots[1] == 0);
    assert(plan.vertex_page_slots[255] == 2);
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, UINT16_C(0xffff), &prepared) == 0);
    assert(prepared.position.x == 4.0f && prepared.position.y == 5.0f &&
           prepared.position.z == 6.0f);
    errno = 0;
    assert(pvr_chunk_model_plan_vertex_attributes_get(
               &plan, UINT16_C(0x0101), &prepared) == -1);
    assert(errno == ENOENT);
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

    {
        static const uint32_t duplicate_vertices[] = {
            VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 4), UINT32_C(0x00010000),
            UINT32_C(0), UINT32_C(0), UINT32_C(0),
            VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 4), UINT32_C(0x00010000),
            UINT32_C(0), UINT32_C(0), UINT32_C(0),
            UINT32_C(0xff)
        };
        static const uint16_t empty_polygons[] = { UINT16_C(0xff) };
        pvr_chunk_model_t duplicate = model_with(
            duplicate_vertices,
            sizeof(duplicate_vertices) / sizeof(duplicate_vertices[0]),
            empty_polygons,
            sizeof(empty_polygons) / sizeof(empty_polygons[0]));

        expect_invalid(&duplicate, EILSEQ);
    }
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
    pvr_chunk_iterator_t record_iterator;
    pvr_chunk_record_t record;
    pvr_chunk_strip_iterator_t strip_iterator;
    pvr_chunk_strip_view_t strip;
    pvr_chunk_strip_vertex_view_t vertex;

    /* The declared payload is intentionally one word too long. */
    expect_invalid(&model, EILSEQ);

    polygons[1] = UINT16_C(12);
    assert(pvr_chunk_model_validate(&model, &info) == 0);
    assert(info.strips == 1 && info.triangles == 1);

    assert(pvr_chunk_polygon_iterator_init(&record_iterator, polygons,
        sizeof(polygons) / sizeof(polygons[0])) == 0);
    assert(pvr_chunk_iterator_next(&record_iterator, &record) == 1);
    assert(pvr_chunk_strip_iterator_init(&strip_iterator, &record) == 0);
    assert(pvr_chunk_strip_iterator_next(&strip_iterator, &strip) == 1);
    assert(strip.reversed && strip.vertex_count == 3 &&
           strip.vertex_word_count == 3 && strip.user_word_count == 1);
    assert(pvr_chunk_strip_vertex_get(&strip, 2, &vertex) == 0);
    assert(vertex.index == 2 && vertex.attribute_word_count == 2);
    assert(vertex.attribute_words[0] == 64 && vertex.attribute_words[1] == 255);
    assert(vertex.triangle_user_word_count == 1 &&
           vertex.triangle_user_words[0] == UINT16_C(0xbeef));

    polygons[5] = 256;
    expect_invalid(&model, EILSEQ);
    polygons[5] = 0;
}

static void test_decoded_attributes(void) {
    static const uint32_t normal_color_words[] = {
        UINT32_C(0x3f800000), UINT32_C(0x40000000), UINT32_C(0x40400000),
        UINT32_C(0x3f800000), UINT32_C(0), UINT32_C(0xbf800000),
        UINT32_C(0x80402010)
    };
    static const uint32_t packed_user_words[] = {
        UINT32_C(0), UINT32_C(0), UINT32_C(0),
        (UINT32_C(0x1ff) << 20) | (UINT32_C(0x201) << 10) |
            UINT32_C(0x200),
        UINT32_C(0x12345678)
    };
    static const uint32_t rgb565_words[] = {
        UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0xf80007e0)
    };
    static const uint32_t argb4444_words[] = {
        UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0x8f10001f)
    };
    static const uint32_t intensity_words[] = {
        UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0xffff8000)
    };
    static const uint32_t metadata_color_words[] = {
        UINT32_C(0), UINT32_C(0), UINT32_C(0),
        UINT32_C(0xa5a5a5a5), UINT32_C(0xff112233)
    };
    pvr_chunk_vertex_batch_t batch;
    pvr_chunk_vertex_attributes_t vertex;
    uint16_t uv_color_words[15] = {
        4, 128, 255, UINT16_C(0x8040), UINT16_C(0x20ff),
        5, 0, 0, 0, 0,
        6, 0, 0, 0, 0
    };
    uint16_t uv_normal_words[18] = {
        7, 512, 1023, UINT16_C(0x7fff), 0, UINT16_C(0x8000),
        8, 0, 0, 0, 0, 0,
        9, 0, 0, 0, 0, 0
    };
    uint16_t two_uv_words[15] = {
        10, 0, 255, 64, 128,
        11, 0, 0, 0, 0,
        12, 0, 0, 0, 0
    };
    uint16_t fixed_uv_words[9] = {
        13, UINT16_C(0xfd80), UINT16_C(0x7e00),
        14, 0, 0,
        15, 0, 0
    };
    uint16_t fixed_two_uv_words[15] = {
        16, UINT16_C(0xb600), UINT16_C(0x4600),
        UINT16_C(0xffff), UINT16_C(0x0100),
        17, 0, 0, 0, 0,
        18, 0, 0, 0, 0
    };
    pvr_chunk_strip_view_t strip;
    pvr_chunk_strip_attributes_t strip_attributes;

    memset(&batch, 0, sizeof(batch));
    batch.first_index = 7;
    batch.entry_count = 1;

    batch.type = PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB;
    batch.entries = normal_color_words;
    batch.entry_word_count = 7;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(vertex.index == 7 && vertex.position.x == 1.0f &&
           vertex.position.y == 2.0f && vertex.position.z == 3.0f &&
           vertex.position.w == 1.0f);
    assert(vertex.present == (PVR_CHUNK_VERTEX_ATTR_NORMAL |
                              PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR));
    assert(vertex.normal.x == 1.0f && vertex.normal.y == 0.0f &&
           vertex.normal.z == -1.0f && vertex.normal.w == 0.0f);
    assert(vertex.diffuse_argb == UINT32_C(0x80402010));

    batch.type = PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER;
    batch.entries = packed_user_words;
    batch.entry_word_count = 5;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(vertex.present == (PVR_CHUNK_VERTEX_ATTR_NORMAL |
                              PVR_CHUNK_VERTEX_ATTR_USER_DATA));
    assert(close_enough(vertex.normal.x, 1.0f));
    assert(close_enough(vertex.normal.y, -1.0f));
    assert(close_enough(vertex.normal.z, -1.0f));
    assert(vertex.user_data == UINT32_C(0x12345678));

    {
        uint32_t malformed[5];

        memcpy(malformed, packed_user_words, sizeof(malformed));
        malformed[3] |= UINT32_C(0x40000000);
        batch.entries = malformed;
        memset(&vertex, 0x5a, sizeof(vertex));
        errno = 0;
        assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == -1);
        assert(errno == EILSEQ && vertex.present == 0);
        batch.entries = packed_user_words;
    }

    batch.type = PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565;
    batch.entries = rgb565_words;
    batch.entry_word_count = 4;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(vertex.diffuse_argb == UINT32_C(0xffff0000));
    assert(vertex.specular_argb == UINT32_C(0xff00ff00));

    batch.type = PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444;
    batch.entries = argb4444_words;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(vertex.diffuse_argb == UINT32_C(0x88ff1100));
    assert(vertex.specular_argb == UINT32_C(0xff0000ff));

    batch.type = PVR_CHUNK_VERTEX_XYZ_INTENSITY;
    batch.entries = intensity_words;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(close_enough(vertex.diffuse_intensity, 1.0f));
    assert(close_enough(vertex.specular_intensity, 32768.0f / 65535.0f));

    batch.type = PVR_CHUNK_VERTEX_XYZ_METADATA_ARGB;
    batch.entries = metadata_color_words;
    batch.entry_word_count = 5;
    assert(pvr_chunk_vertex_attributes_get(&batch, 0, &vertex) == 0);
    assert(vertex.metadata == UINT32_C(0xa5a5a5a5));
    assert(vertex.diffuse_argb == UINT32_C(0xff112233));

    memset(&strip, 0, sizeof(strip));
    strip.vertex_count = 3;
    strip.user_word_count = 0;

    strip.type = PVR_CHUNK_STRIP_UV8_ARGB;
    strip.words = uv_color_words;
    strip.vertex_word_count = 5;
    strip.word_count = 15;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == 0);
    assert(strip_attributes.index == 4);
    assert(strip_attributes.present == (PVR_CHUNK_STRIP_ATTR_UV0 |
                                        PVR_CHUNK_STRIP_ATTR_COLOR));
    assert(close_enough(strip_attributes.uv[0][0], 128.0f / 255.0f));
    assert(close_enough(strip_attributes.uv[0][1], 1.0f));
    assert(strip_attributes.argb == UINT32_C(0x804020ff));

    strip.type = PVR_CHUNK_STRIP_UV10_NORMAL;
    strip.words = uv_normal_words;
    strip.vertex_word_count = 6;
    strip.word_count = 18;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == 0);
    assert(strip_attributes.present == (PVR_CHUNK_STRIP_ATTR_UV0 |
                                        PVR_CHUNK_STRIP_ATTR_NORMAL));
    assert(close_enough(strip_attributes.uv[0][0], 512.0f / 1023.0f));
    assert(close_enough(strip_attributes.normal.x, 1.0f));
    assert(close_enough(strip_attributes.normal.z, -1.0f));

    strip.type = PVR_CHUNK_STRIP_UV8_TWO_VOLUME;
    strip.words = two_uv_words;
    strip.vertex_word_count = 5;
    strip.word_count = 15;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == 0);
    assert(strip_attributes.present == (PVR_CHUNK_STRIP_ATTR_UV0 |
                                        PVR_CHUNK_STRIP_ATTR_UV1));
    assert(close_enough(strip_attributes.uv[0][1], 1.0f));
    assert(close_enough(strip_attributes.uv[1][0], 64.0f / 255.0f));
    assert(close_enough(strip_attributes.uv[1][1], 128.0f / 255.0f));

    strip.type = PVR_CHUNK_STRIP_UV10_FIXED;
    strip.words = fixed_uv_words;
    strip.vertex_word_count = 3;
    strip.word_count = 9;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == 0);
    assert(strip_attributes.present == PVR_CHUNK_STRIP_ATTR_UV0);
    assert(close_enough(strip_attributes.uv[0][0], -0.625f));
    assert(close_enough(strip_attributes.uv[0][1], 31.5f));

    strip.type = PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME;
    strip.words = fixed_two_uv_words;
    strip.vertex_word_count = 5;
    strip.word_count = 15;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == 0);
    assert(strip_attributes.present == (PVR_CHUNK_STRIP_ATTR_UV0 |
                                        PVR_CHUNK_STRIP_ATTR_UV1));
    assert(close_enough(strip_attributes.uv[0][0], -74.0f));
    assert(close_enough(strip_attributes.uv[0][1], 70.0f));
    assert(close_enough(strip_attributes.uv[1][0], -1.0f / 256.0f));
    assert(close_enough(strip_attributes.uv[1][1], 1.0f));

    strip.type = PVR_CHUNK_STRIP_UV8_ARGB;
    strip.words = uv_color_words;
    strip.vertex_word_count = 5;
    strip.word_count = 15;
    uv_color_words[1] = 256;
    memset(&strip_attributes, 0x5a, sizeof(strip_attributes));
    errno = 0;
    assert(pvr_chunk_strip_attributes_get(&strip, 0,
                                          &strip_attributes) == -1);
    assert(errno == EILSEQ && strip_attributes.present == 0);
}

static pvr_chunk_record_t volume_record(uint8_t type,
                                        const uint16_t *payload,
                                        size_t payload_word_count) {
    pvr_chunk_record_t record;

    memset(&record, 0, sizeof(record));
    record.stream = PVR_CHUNK_STREAM_POLYGON;
    record.record_class = PVR_CHUNK_RECORD_VOLUME;
    record.type = type;
    record.payload = payload;
    record.payload_word_count = payload_word_count;
    return record;
}

static void test_volume_iterator(void) {
    static const uint16_t triangle_payload[] = {
        UINT16_C(0xc002),
        0, 1, 2, UINT16_C(0xa001), UINT16_C(0xa002), UINT16_C(0xa003),
        3, 4, 5, UINT16_C(0xb001), UINT16_C(0xb002), UINT16_C(0xb003)
    };
    static const uint16_t quad_payload[] = {
        UINT16_C(0x4001), 10, 11, 12, 13, UINT16_C(0xc001)
    };
    static const uint16_t strip_payload[] = {
        UINT16_C(0x8002),
        UINT16_C(4),
        20, 21, 22, UINT16_C(0xd001), UINT16_C(0xd002),
        23, UINT16_C(0xe001), UINT16_C(0xe002),
        UINT16_C(0x8003),
        30, 31, 32, UINT16_C(0xf001), UINT16_C(0xf002)
    };
    pvr_chunk_record_t record;
    pvr_chunk_volume_iterator_t iterator;
    pvr_chunk_volume_triangle_t triangle;
    size_t count = SIZE_MAX;

    record = volume_record(PVR_CHUNK_VOLUME_TRIANGLES, triangle_payload,
                           sizeof(triangle_payload) /
                           sizeof(triangle_payload[0]));
    assert(pvr_chunk_volume_triangle_count(&record, &count) == 0 &&
           count == 2);
    assert(pvr_chunk_volume_iterator_init(&iterator, &record) == 0);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 0 && triangle.index[1] == 1 &&
           triangle.index[2] == 2 && triangle.user_word_count == 3 &&
           triangle.user_words[0] == UINT16_C(0xa001) &&
           triangle.user_words[2] == UINT16_C(0xa003) &&
           triangle.source_type == PVR_CHUNK_VOLUME_TRIANGLES &&
           !triangle.final_in_record);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 3 && triangle.index[1] == 4 &&
           triangle.index[2] == 5 &&
           triangle.user_words[0] == UINT16_C(0xb001) &&
           triangle.final_in_record);
    memset(&triangle, 0x5a, sizeof(triangle));
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 0);
    assert(triangle.index[0] == 0 && triangle.user_words == NULL);

    record = volume_record(PVR_CHUNK_VOLUME_QUADS, quad_payload,
                           sizeof(quad_payload) / sizeof(quad_payload[0]));
    assert(pvr_chunk_volume_triangle_count(&record, &count) == 0 &&
           count == 2);
    assert(pvr_chunk_volume_iterator_init(&iterator, &record) == 0);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 10 && triangle.index[1] == 11 &&
           triangle.index[2] == 12 && !triangle.final_in_record);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 12 && triangle.index[1] == 11 &&
           triangle.index[2] == 13 &&
           triangle.user_words[0] == UINT16_C(0xc001) &&
           triangle.final_in_record);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 0);

    record = volume_record(PVR_CHUNK_VOLUME_STRIPS, strip_payload,
                           sizeof(strip_payload) / sizeof(strip_payload[0]));
    assert(pvr_chunk_volume_triangle_count(&record, &count) == 0 &&
           count == 3);
    assert(pvr_chunk_volume_iterator_init(&iterator, &record) == 0);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 20 && triangle.index[1] == 21 &&
           triangle.index[2] == 22 && triangle.user_word_count == 2 &&
           triangle.user_words[0] == UINT16_C(0xd001));
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 22 && triangle.index[1] == 21 &&
           triangle.index[2] == 23 &&
           triangle.user_words[0] == UINT16_C(0xe001) &&
           !triangle.final_in_record);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 1);
    assert(triangle.index[0] == 31 && triangle.index[1] == 30 &&
           triangle.index[2] == 32 &&
           triangle.user_words[0] == UINT16_C(0xf001) &&
           triangle.final_in_record);
    assert(pvr_chunk_volume_iterator_next(&iterator, &triangle) == 0);

    record.payload_word_count--;
    count = SIZE_MAX;
    errno = 0;
    assert(pvr_chunk_volume_triangle_count(&record, &count) == -1);
    assert(errno == EILSEQ && count == 0);
    memset(&iterator, 0x5a, sizeof(iterator));
    errno = 0;
    assert(pvr_chunk_volume_iterator_init(&iterator, &record) == -1);
    assert(errno == EILSEQ && iterator.type == 0);

    record = volume_record(UINT8_C(59), triangle_payload,
                           sizeof(triangle_payload) /
                           sizeof(triangle_payload[0]));
    errno = 0;
    assert(pvr_chunk_volume_triangle_count(&record, &count) == -1);
    assert(errno == EILSEQ && count == 0);
}

static void translation(matrix_t *matrix, float x, float y, float z) {
    const matrix_t value = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { x, y, z, 1.0f }
    };

    memcpy(matrix, &value, sizeof(value));
}

typedef struct visit_log {
    size_t count;
    size_t stop_after;
    size_t fail_after;
    int fail_errno;
    float translation[3][3];
} visit_log_t;

static int log_visit(size_t node_index,
                     const pvr_chunk_hierarchy_node_t *node,
                     const matrix_t *world, void *data) {
    visit_log_t *log = data;

    assert(node_index == log->count);
    assert(node->user_data == (const void *)(uintptr_t)(node_index + 1u));
    log->translation[log->count][0] = (*world)[3][0];
    log->translation[log->count][1] = (*world)[3][1];
    log->translation[log->count][2] = (*world)[3][2];
    ++log->count;
    if(log->fail_after && log->count == log->fail_after) {
        errno = log->fail_errno;
        return -1;
    }
    return log->stop_after && log->count == log->stop_after;
}

static void test_hierarchy(void) {
    pvr_chunk_model_t model = model_with(
        valid_vertices, sizeof(valid_vertices) / sizeof(valid_vertices[0]),
        valid_polygons, sizeof(valid_polygons) / sizeof(valid_polygons[0]));
    pvr_chunk_model_view_t view;
    alignas(32) pvr_chunk_hierarchy_node_t nodes[3];
    pvr_chunk_hierarchy_t hierarchy = { nodes, 3 };
    alignas(32) matrix_t world[3];
    alignas(32) matrix_t animated[3];
    alignas(32) matrix_t unchanged[3];
    alignas(32) matrix_t root;
    pvr_chunk_hierarchy_result_t result;
    visit_log_t log = { 0 };

    assert(pvr_chunk_model_open(&model, &view) == 0);
    memset(nodes, 0, sizeof(nodes));
    nodes[0].model = &view;
    nodes[0].parent_index = PVR_CHUNK_NODE_NONE;
    nodes[0].user_data = (const void *)(uintptr_t)1;
    translation(&nodes[0].local_transform, 1.0f, 0.0f, 0.0f);
    nodes[1].parent_index = 0;
    nodes[1].user_data = (const void *)(uintptr_t)2;
    translation(&nodes[1].local_transform, 2.0f, 0.0f, 0.0f);
    nodes[2].model = &view;
    nodes[2].parent_index = 0;
    nodes[2].user_data = (const void *)(uintptr_t)3;
    translation(&nodes[2].local_transform, 0.0f, 3.0f, 0.0f);
    translation(&root, 0.0f, 0.0f, 4.0f);

    assert(pvr_chunk_hierarchy_traverse(&hierarchy, &root, world, 3,
                                        log_visit, &log, &result) == 0);
    assert(result.visited_nodes == 3 && log.count == 3);
    assert(log.translation[0][0] == 1.0f &&
           log.translation[0][1] == 0.0f &&
           log.translation[0][2] == 4.0f);
    assert(log.translation[1][0] == 3.0f &&
           log.translation[1][1] == 0.0f &&
           log.translation[1][2] == 4.0f);
    assert(log.translation[2][0] == 1.0f &&
           log.translation[2][1] == 3.0f &&
           log.translation[2][2] == 4.0f);

    translation(&animated[0], 10.0f, 0.0f, 0.0f);
    translation(&animated[1], 0.0f, 20.0f, 0.0f);
    translation(&animated[2], 0.0f, 0.0f, 30.0f);
    memset(&log, 0, sizeof(log));
    assert(pvr_chunk_hierarchy_traverse_transforms(
               &hierarchy, animated, 3, &root, world, 3,
               log_visit, &log, &result) == 0);
    assert(result.visited_nodes == 3 && log.count == 3);
    assert(log.translation[0][0] == 10.0f &&
           log.translation[0][1] == 0.0f &&
           log.translation[0][2] == 4.0f);
    assert(log.translation[1][0] == 10.0f &&
           log.translation[1][1] == 20.0f &&
           log.translation[1][2] == 4.0f);
    assert(log.translation[2][0] == 10.0f &&
           log.translation[2][1] == 0.0f &&
           log.translation[2][2] == 34.0f);

    /* Exact in-place composition is useful when the sampled local pose no
       longer needs to be retained after hierarchy evaluation. */
    assert(pvr_chunk_hierarchy_traverse_transforms(
               &hierarchy, animated, 3, NULL, animated, 3,
               NULL, NULL, &result) == 0);
    assert(animated[0][3][0] == 10.0f &&
           animated[1][3][0] == 10.0f &&
           animated[1][3][1] == 20.0f &&
           animated[2][3][0] == 10.0f &&
           animated[2][3][2] == 30.0f);

    errno = 0;
    assert(pvr_chunk_hierarchy_traverse_transforms(
               &hierarchy, NULL, 0, NULL, world, 3,
               NULL, NULL, &result) == -1);
    assert(errno == EINVAL && result.visited_nodes == 0);

    errno = 0;
    assert(pvr_chunk_hierarchy_traverse_transforms(
               &hierarchy, animated, 2, NULL, world, 3,
               NULL, NULL, &result) == -1);
    assert(errno == ENOSPC && result.visited_nodes == 0);

    memset(&log, 0, sizeof(log));
    log.stop_after = 2;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 3,
                                        log_visit, &log, &result) == 1);
    assert(result.visited_nodes == 2 && log.count == 2);

    memset(&log, 0, sizeof(log));
    log.fail_after = 2;
    errno = 0;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 3,
                                        log_visit, &log, &result) == -1);
    assert(errno == ECANCELED && result.visited_nodes == 2 && log.count == 2);

    memset(&log, 0, sizeof(log));
    log.fail_after = 1;
    log.fail_errno = EIO;
    errno = 0;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 3,
                                        log_visit, &log, &result) == -1);
    assert(errno == EIO && result.visited_nodes == 1 && log.count == 1);

    memset(world, 0x5a, sizeof(world));
    memcpy(unchanged, world, sizeof(world));
    nodes[0].parent_index = 1;
    errno = 0;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 3,
                                        NULL, NULL, &result) == -1);
    assert(errno == EILSEQ && result.visited_nodes == 0);
    assert(memcmp(world, unchanged, sizeof(world)) == 0);
    nodes[0].parent_index = PVR_CHUNK_NODE_NONE;

    errno = 0;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 2,
                                        NULL, NULL, &result) == -1);
    assert(errno == ENOSPC && result.visited_nodes == 0);

    nodes[1].local_transform[0][0] = NAN;
    errno = 0;
    assert(pvr_chunk_hierarchy_traverse(&hierarchy, NULL, world, 3,
                                        NULL, NULL, &result) == -1);
    assert(errno == EILSEQ && result.visited_nodes == 0);
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

        {
            pvr_chunk_iterator_t iterator;
            pvr_chunk_record_t record;
            int rv;

            if(pvr_chunk_vertex_iterator_init(&iterator, model.vertex_words,
                                               model.vertex_word_count) == 0) {
                while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
                    pvr_chunk_vertex_batch_t batch;
                    size_t entry;

                    if(pvr_chunk_vertex_batch_decode(&record, &batch) < 0)
                        continue;
                    for(entry = 0; entry < batch.entry_count; ++entry) {
                        pvr_chunk_vertex_view_t vertex;

                        (void)pvr_chunk_vertex_batch_get(&batch, entry,
                                                         &vertex);
                    }
                }
            }

            if(pvr_chunk_polygon_iterator_init(&iterator,
                                                model.polygon_words,
                                                model.polygon_word_count) == 0) {
                while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
                    pvr_chunk_strip_iterator_t strip_iterator;
                    pvr_chunk_strip_view_t strip;
                    int strip_rv;

                    if(pvr_chunk_strip_iterator_init(&strip_iterator,
                                                     &record) < 0)
                        continue;
                    while((strip_rv = pvr_chunk_strip_iterator_next(
                               &strip_iterator, &strip)) > 0) {
                        size_t vertex_index;

                        for(vertex_index = 0;
                            vertex_index < strip.vertex_count;
                            ++vertex_index) {
                            pvr_chunk_strip_vertex_view_t vertex;

                            (void)pvr_chunk_strip_vertex_get(
                                &strip, vertex_index, &vertex);
                        }
                    }
                }
            }
        }
    }
}

int main(void) {
    test_valid_model();
    test_prepared_plan();
    test_bad_streams();
    test_user_flags_and_reverse_strip();
    test_decoded_attributes();
    test_volume_iterator();
    test_hierarchy();
    test_bounded_random_streams();
    puts("pvr chunk model tests: PASS");
    return 0;
}
