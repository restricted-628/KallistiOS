/* KallistiOS ##version##

   Compact volume-section example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/pvr_chunk_volume_asset.h>

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t model_vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    0, 0, 0,
    UINT32_C(0x3f800000), 0, 0,
    0, UINT32_C(0x3f800000), 0,
    UINT32_C(0x000000ff)
};

static const uint16_t model_polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), 0, 1, 2,
    UINT16_C(0x00ff)
};

static const pvr_chunk_model_t model = {
    model_vertices, sizeof(model_vertices) / sizeof(model_vertices[0]),
    model_polygons, sizeof(model_polygons) / sizeof(model_polygons[0]),
    { 0.5f, 0.5f, 0.0f }, 1.0f
};

static alignas(2) const uint8_t volume_section[] = {
    0x50, 0x56, 0x4c, 0x31, 0x01, 0x00, 0x30, 0x00,
    0x4a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x3b, 0xe9, 0x86, 0x5a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xd4, 0xa2, 0x01, 0x9b,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x07, 0x00, 0x01, 0xc0, 0x00, 0x00,
    0x01, 0x00, 0x02, 0x00, 0x11, 0x11, 0x22, 0x22,
    0x33, 0x33
};

int main(int argc, char **argv) {
    pvr_chunk_model_view_t model_view;
    pvr_chunk_volume_section_view_t section;
    pvr_chunk_volume_section_iterator_t iterator;
    pvr_chunk_volume_triangle_t triangle;
    int rv;

    (void)argc;
    (void)argv;
    if(pvr_chunk_model_open(&model, &model_view) < 0 ||
       pvr_chunk_volume_section_open(
           volume_section, sizeof(volume_section), &section) < 0 ||
       pvr_chunk_volume_section_validate_model(
           &section, &model_view) < 0 ||
       pvr_chunk_volume_section_iterator_init(&iterator, &section) < 0) {
        perror("compact volume admission");
        return 1;
    }

    while((rv = pvr_chunk_volume_section_iterator_next(
               &iterator, &triangle)) > 0) {
        printf("triangle %u,%u,%u metadata %04x %04x %04x\n",
               triangle.index[0], triangle.index[1], triangle.index[2],
               triangle.user_words[0], triangle.user_words[1],
               triangle.user_words[2]);
    }
    if(rv < 0) {
        perror("compact volume iteration");
        return 1;
    }
    return 0;
}
