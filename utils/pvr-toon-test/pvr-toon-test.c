/* KallistiOS ##version##

   pvr-toon-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_toon.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLOSE(a, b) (fabsf((a) - (b)) < 1.0e-5f)

static pvr_toon_vertex_t vertex(float x, float y, float shade,
                                uint32_t color, uint32_t index) {
    pvr_toon_vertex_t result;

    memset(&result, 0, sizeof(result));
    result.position.x = x;
    result.position.y = y;
    result.position.w = 1.0f;
    result.normal.x = x;
    result.normal.y = y;
    result.normal.z = 1.0f;
    result.u = x;
    result.v = y;
    result.shade = shade;
    result.argb = color;
    result.oargb = color ^ UINT32_C(0x00ffffff);
    result.source_index = index;
    return result;
}

static float area(const pvr_toon_vertex_t vertices[3]) {
    float ax = vertices[1].position.x - vertices[0].position.x;
    float ay = vertices[1].position.y - vertices[0].position.y;
    float bx = vertices[2].position.x - vertices[0].position.x;
    float by = vertices[2].position.y - vertices[0].position.y;

    return fabsf(ax * by - ay * bx) * 0.5f;
}

static void test_light_and_bands(void) {
    vector_t direction = { 0.0f, 0.0f, 4.0f, 7.0f };
    vector_t normal = { 0.0f, 0.0f, 2.0f, 0.0f };
    pvr_toon_light_t light;
    pvr_toon_light_t unchanged;
    float thresholds[] = { -0.25f, 0.5f, 2.0f };
    float shade;
    size_t band;
    size_t capacity;

    assert(pvr_toon_light_init(&light, &direction, 0.75f, 0.25f) == 0);
    assert(light.direction.z == 1.0f && light.direction.w == 0.0f);
    assert(pvr_toon_shade_evaluate(&shade, &normal, &light,
                                   PVR_TOON_SHADE_DOT) == 0);
    assert(shade == 1.0f);
    assert(pvr_toon_shade_evaluate(&shade, &normal, &light,
                                   PVR_TOON_SHADE_INVERTED_DOT) == 0);
    assert(shade == -0.5f);
    assert(pvr_toon_shade_evaluate(&shade, &normal, &light,
                                   PVR_TOON_SHADE_HALF_LAMBERT) == 0);
    assert(shade == 1.0f);

    assert(pvr_toon_band_index(&band, -0.25f, thresholds, 3) == 0 &&
           band == 1);
    assert(pvr_toon_band_index(&band, 10.0f, thresholds, 3) == 0 &&
           band == 3);
    assert(pvr_toon_triangle_capacity(3, &capacity) == 0 && capacity == 7);

    memset(&light, 0x5a, sizeof(light));
    unchanged = light;
    direction.x = direction.y = direction.z = 0.0f;
    errno = 0;
    assert(pvr_toon_light_init(&light, &direction, 1.0f, 0.0f) == -1);
    assert(errno == EDOM && memcmp(&light, &unchanged, sizeof(light)) == 0);
}

static void test_binary_split(void) {
    const float threshold = 0.0f;
    pvr_toon_vertex_t input[3] = {
        vertex(0.0f, 1.0f, 1.0f, UINT32_C(0xffff0000), 0),
        vertex(-1.0f, -1.0f, -1.0f, UINT32_C(0xff00ff00), 1),
        vertex(1.0f, -1.0f, -1.0f, UINT32_C(0xff0000ff), 2)
    };
    pvr_toon_triangle_t output[3];
    pvr_toon_split_result_t result;
    float total_area = 0.0f;
    size_t band_count[2] = { 0, 0 };
    size_t triangle;

    assert(pvr_toon_split_triangle(output, 3, input, &threshold, 1,
                                   1.0e-6f, &result) == 0);
    assert(result.output_triangles == 3 && result.crossed_thresholds == 1);
    assert(result.generated_vertices == 5);
    for(triangle = 0; triangle < result.output_triangles; ++triangle) {
        size_t corner;

        assert(output[triangle].band < 2);
        ++band_count[output[triangle].band];
        total_area += area(output[triangle].vertices);
        for(corner = 0; corner < 3; ++corner) {
            const pvr_toon_vertex_t *v = &output[triangle].vertices[corner];
            float normal_length = sqrtf(v->normal.x * v->normal.x +
                                        v->normal.y * v->normal.y +
                                        v->normal.z * v->normal.z);

            assert(CLOSE(normal_length, 1.0f));
            if(v->source_index == PVR_TOON_GENERATED_INDEX)
                assert(v->shade == 0.0f);
        }
    }
    assert(band_count[0] == 2 && band_count[1] == 1);
    assert(CLOSE(total_area, area(input)));
}

static void test_shared_edge_and_multiband(void) {
    const float binary = 0.0f;
    const float thresholds[] = { -0.5f, 0.0f, 0.5f };
    pvr_toon_vertex_t first[3] = {
        vertex(-1.0f, 0.0f, -1.0f, UINT32_C(0xff000000), 7),
        vertex(1.0f, 0.0f, 1.0f, UINT32_C(0xffffffff), 8),
        vertex(0.0f, 1.0f, 1.0f, UINT32_C(0xffff0000), 9)
    };
    pvr_toon_vertex_t second[3] = {
        first[1], first[0],
        vertex(0.0f, -1.0f, -1.0f, UINT32_C(0xff00ff00), 10)
    };
    pvr_toon_triangle_t first_output[3];
    pvr_toon_triangle_t second_output[3];
    pvr_toon_triangle_t multiband[7];
    pvr_toon_split_result_t result;
    point_t first_edge = { 0 };
    point_t second_edge = { 0 };
    int first_found = 0;
    int second_found = 0;
    size_t triangle;
    size_t corner;

    assert(pvr_toon_split_triangle(first_output, 3, first, &binary, 1,
                                   0.0f, NULL) == 0);
    assert(pvr_toon_split_triangle(second_output, 3, second, &binary, 1,
                                   0.0f, NULL) == 0);
    for(triangle = 0; triangle < 3; ++triangle) {
        for(corner = 0; corner < 3; ++corner) {
            const pvr_toon_vertex_t *v = &first_output[triangle].vertices[corner];

            if(v->source_index == PVR_TOON_GENERATED_INDEX &&
               v->position.y == 0.0f) {
                first_edge = v->position;
                first_found = 1;
            }
            v = &second_output[triangle].vertices[corner];
            if(v->source_index == PVR_TOON_GENERATED_INDEX &&
               v->position.y == 0.0f) {
                second_edge = v->position;
                second_found = 1;
            }
        }
    }
    assert(first_found && second_found &&
           memcmp(&first_edge, &second_edge, sizeof(first_edge)) == 0);

    assert(pvr_toon_split_triangle(multiband, 7, first, thresholds, 3,
                                   0.0f, &result) == 0);
    assert(result.output_triangles == 7 && result.crossed_thresholds == 3);
    for(triangle = 0; triangle < result.output_triangles; ++triangle)
        assert(multiband[triangle].band < 4);
}

static void test_boundaries_capacity_and_color(void) {
    const float threshold = 0.0f;
    pvr_toon_vertex_t boundary[3] = {
        vertex(0.0f, 0.0f, 0.0f, UINT32_C(0xffffffff), 0),
        vertex(1.0f, 0.0f, 0.0f, UINT32_C(0xffffffff), 1),
        vertex(0.0f, 1.0f, 0.0f, UINT32_C(0xffffffff), 2)
    };
    pvr_toon_triangle_t output[3];
    pvr_toon_triangle_t unchanged[3];
    pvr_toon_split_result_t result;
    uint32_t color;

    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    assert(pvr_toon_split_triangle(output, 3, boundary, &threshold, 1,
                                   1.0e-6f, &result) == 0);
    assert(result.output_triangles == 1 && output[0].band == 1);

    boundary[0].shade = 1.0f;
    boundary[1].shade = -1.0f;
    boundary[2].shade = -1.0f;
    memset(output, 0x5a, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_toon_split_triangle(output, 2, boundary, &threshold, 1,
                                   0.0f, &result) == -1);
    assert(errno == ENOSPC && result.output_triangles == 3 &&
           memcmp(output, unchanged, sizeof(output)) == 0);

    assert(pvr_toon_color_modulate(&color, UINT32_C(0x80402010),
                                   UINT32_C(0x80ff8040)) == 0);
    assert(color == UINT32_C(0x40401004));
}

static uint32_t random_state = UINT32_C(0x6d2b79f5);

static float random_signed(void) {
    random_state = random_state * UINT32_C(1664525) +
                   UINT32_C(1013904223);
    return ((float)(random_state >> 8) / 8388607.5f) - 1.0f;
}

static void test_random_partitions(void) {
    const float thresholds[] = { -0.5f, 0.0f, 0.5f };
    size_t iteration;

    for(iteration = 0; iteration < 5000; ++iteration) {
        pvr_toon_vertex_t input[3] = {
            vertex(0.0f, 0.0f, random_signed(), UINT32_C(0xff102030), 0),
            vertex(1.0f, 0.0f, random_signed(), UINT32_C(0xff405060), 1),
            vertex(0.0f, 1.0f, random_signed(), UINT32_C(0xff708090), 2)
        };
        pvr_toon_triangle_t output[7];
        pvr_toon_split_result_t result;
        float total_area = 0.0f;
        size_t triangle;

        assert(pvr_toon_split_triangle(output, 7, input, thresholds, 3,
                                       1.0e-6f, &result) == 0);
        assert(result.output_triangles >= 1 && result.output_triangles <= 7);
        for(triangle = 0; triangle < result.output_triangles; ++triangle) {
            size_t corner;
            float triangle_area = area(output[triangle].vertices);

            assert(triangle_area > 0.0f);
            total_area += triangle_area;
            for(corner = 0; corner < 3; ++corner) {
                float shade = output[triangle].vertices[corner].shade;
                size_t band = output[triangle].band;

                assert((band == 0 || shade >= thresholds[band - 1u]) &&
                       (band == 3 || shade <= thresholds[band]));
            }
        }
        assert(fabsf(total_area - 0.5f) < 1.0e-4f);
    }
}

int main(void) {
    test_light_and_bands();
    test_binary_split();
    test_shared_edge_and_multiband();
    test_boundaries_capacity_and_color();
    test_random_partitions();
    puts("pvr toon tests: PASS");
    return 0;
}
