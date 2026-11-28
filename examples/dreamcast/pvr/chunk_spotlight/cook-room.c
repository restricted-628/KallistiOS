/* Build a five-sided room. Shared positions on different walls have their own normals. */
#include <dc/pvr_chunk_cache_asset.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COLS = 13, ROWS = 9, VERTICES = COLS*ROWS + 4*(COLS+ROWS),
       STRIPS = ROWS-1 + 4, REFS = (ROWS-1)*2*COLS + 4*(COLS+ROWS) };
static uint32_t vertices[3 + 6 * VERTICES];
static uint16_t polygons[8 + STRIPS + REFS];
static size_t vcount, cursor;

static void wall(unsigned cols, unsigned rows, const float origin[3],
                 const float along[3], const float down[3], const float normal[3]) {
    size_t first = vcount;
    for(unsigned y = 0; y < rows; ++y) {
        for(unsigned x = 0; x < cols; ++x) {
            float point[3];
            for(unsigned k = 0; k < 3; ++k)
                point[k] = origin[k] + x * along[k] + y * down[k];
            memcpy(vertices + 2 + 6*vcount, point, sizeof(point));
            memcpy(vertices + 5 + 6*vcount, normal, 3*sizeof(float));
            ++vcount;
        }
    }
    for(unsigned y = 0; y + 1 < rows; ++y) {
        polygons[cursor++] = 2*cols;
        for(unsigned x = 0; x < cols; ++x) {
            polygons[cursor++] = first + (y+1)*cols + x;
            polygons[cursor++] = first + y*cols + x;
        }
    }
}
#define V(x,y,z) ((const float[3]){x,y,z})

int main(void) {
    polygons[0] = PVR_CHUNK_MATERIAL_DIFFUSE;
    polygons[1] = 2;
    polygons[2] = polygons[3] = 0xffff;
    polygons[4] = PVR_CHUNK_STRIP_INDEX;
    polygons[5] = 1 + STRIPS + REFS;
    polygons[6] = STRIPS;
    cursor = 7;
    wall(COLS, ROWS, V(-3,-2,0), V(.5,0,0), V(0,.5,0), V(0,0,-1));
    wall(ROWS, 2, V(-3,-2,0), V(0,.5,0), V(0,0,-1), V(1,0,0));
    wall(ROWS, 2, V(3,-2,0), V(0,.5,0), V(0,0,-1), V(-1,0,0));
    wall(COLS, 2, V(-3,-2,0), V(.5,0,0), V(0,0,-1), V(0,1,0));
    wall(COLS, 2, V(-3,2,0), V(.5,0,0), V(0,0,-1), V(0,-1,0));
    polygons[cursor++] = 0xff;
    if(vcount != VERTICES || cursor != sizeof(polygons)/sizeof(*polygons))
        return EXIT_FAILURE;
    vertices[0] = PVR_CHUNK_VERTEX_XYZ_NORMAL | ((1u + 6u*VERTICES) << 16);
    vertices[1] = VERTICES << 16;
    vertices[2 + 6*VERTICES] = 0xff;
    pvr_chunk_model_t model = {vertices, 3 + 6*VERTICES, polygons, cursor, {0,0,-.5f}, 4};
    pvr_chunk_model_view_t view;
    pvr_chunk_vertex_index_entry_t index[PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE];
    pvr_chunk_model_plan_t plan;
    pvr_chunk_model_cache_t cache;
    static alignas(32) unsigned char storage[64*1024];
    size_t bytes;
    if(pvr_chunk_model_open(&model, &view) ||
       pvr_chunk_model_plan_build(&view, index, PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE, &plan) ||
       pvr_chunk_model_cache_build(&plan, storage, sizeof(storage), NULL, NULL, &cache) ||
       pvr_chunk_model_cache_section_query(&cache, &bytes)) {
        perror("cook room");
        return EXIT_FAILURE;
    }
    unsigned char *section = malloc(bytes);
    if(!section || pvr_chunk_model_cache_section_serialize(&cache, section, bytes)) {
        free(section);
        return EXIT_FAILURE;
    }
    puts("static const unsigned char room_section[] = {");
    for(size_t i = 0; i < bytes; ++i)
        printf("0x%02x,%c", section[i], i%16 == 15 ? '\n' : ' ');
    puts("\n};");
    free(section);
    return fflush(stdout) || ferror(stdout) ? EXIT_FAILURE : EXIT_SUCCESS;
}
