/* Build the fixed two-volume receiver with the production Compact codec. */
#include <dc/pvr_chunk_cache_asset.h>
#include <stdio.h>
#include <stdlib.h>

enum { RECEIVER_VERTICES = 4 };

/* Authored receiver geometry and independent volume UVs. XYZ/normal words are
   IEEE binary32. Left/right = 8/56, top/bottom = 8/56, normal = (0,0,1). */
#define RECEIVER_VERTEX(x, y) x, y, 0, 0, 0, 0x3f800000u
static const uint32_t vertex_words[] = {
    PVR_CHUNK_VERTEX_XYZ_NORMAL | (25u << 16), RECEIVER_VERTICES << 16,
    RECEIVER_VERTEX(0x41000000u, 0x42600000u),
    RECEIVER_VERTEX(0x41000000u, 0x41000000u),
    RECEIVER_VERTEX(0x42600000u, 0x42600000u),
    RECEIVER_VERTEX(0x42600000u, 0x41000000u),
    0xff
};
#undef RECEIVER_VERTEX

static const uint16_t polygon_words[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0xffff, 0xffff,
    PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 2, 0xffff, 0xffff,
    PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME, 22, 1, RECEIVER_VERTICES,
    0,   0, 256, 256, 256,
    1,   0,   0, 256,   0,
    2, 256, 256,   0, 256,
    3, 256,   0,   0,   0,
    0xff
};

static const pvr_chunk_model_t receiver = {
    vertex_words, sizeof(vertex_words) / sizeof(*vertex_words),
    polygon_words, sizeof(polygon_words) / sizeof(*polygon_words),
    {32.0f, 32.0f, 0.0f}, 34.0f
};

int main(void) {
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t index[PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_two_volume_cache_t cache;
    alignas(32) unsigned char storage[2048];
    size_t bytes;
    if(pvr_chunk_model_open(&receiver, &view) ||
       pvr_chunk_model_plan_build(&view, index, PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE, &plan) ||
       pvr_chunk_model_two_volume_cache_build(&plan, storage, sizeof(storage),
                                              NULL, NULL, &cache) ||
       pvr_chunk_two_volume_cache_section_query(&cache, &bytes)) {
        perror("build receiver");
        return EXIT_FAILURE;
    }
    unsigned char *section = malloc(bytes);
    if(!section || pvr_chunk_two_volume_cache_section_serialize(&cache, section, bytes)) {
        perror("serialize receiver");
        free(section);
        return EXIT_FAILURE;
    }
    puts("static const unsigned char receiver_section[] = {");
    for(size_t i = 0; i < bytes; ++i)
        printf("0x%02x,%c", (unsigned)section[i], i % 16 == 15 ? '\n' : ' ');
    puts("\n};");
    free(section);
    return fflush(stdout) || ferror(stdout) ? EXIT_FAILURE : EXIT_SUCCESS;
}
