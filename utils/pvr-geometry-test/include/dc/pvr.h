#ifndef TEST_DC_PVR_H
#define TEST_DC_PVR_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define PVR_CMD_VERTEX     UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)

typedef enum pvr_list {
    PVR_LIST_OP_POLY = 0,
    PVR_LIST_OP_MOD,
    PVR_LIST_TR_POLY,
    PVR_LIST_TR_MOD,
    PVR_LIST_PT_POLY
} pvr_list_t;

typedef struct pvr_vertex {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    union {
        struct {
            float u;
            float v;
        };
        struct {
            uint32_t argb0;
            uint32_t argb1;
        };
    };
    uint32_t argb;
    uint32_t oargb;
} pvr_vertex_t;

typedef struct pvr_vertex_pcm {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    uint32_t argb0;
    uint32_t argb1;
    uint32_t d1;
    uint32_t d2;
} pvr_vertex_pcm_t;

typedef struct pvr_vertex_tpcm {
    alignas(32) uint32_t flags;
    float x;
    float y;
    float z;
    float u0;
    float v0;
    uint32_t argb0;
    uint32_t oargb0;
    float u1;
    float v1;
    uint32_t argb1;
    uint32_t oargb1;
    uint32_t d1;
    uint32_t d2;
    uint32_t d3;
    uint32_t d4;
} pvr_vertex_tpcm_t;

int pvr_prim(const void *data, size_t size);
int pvr_list_prim(pvr_list_t list, const void *data, size_t size);

#endif
