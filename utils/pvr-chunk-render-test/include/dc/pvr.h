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

typedef enum pvr_blend_mode {
    PVR_BLEND_ZERO,
    PVR_BLEND_ONE,
    PVR_BLEND_DESTCOLOR,
    PVR_BLEND_INVDESTCOLOR,
    PVR_BLEND_SRCALPHA,
    PVR_BLEND_INVSRCALPHA,
    PVR_BLEND_DESTALPHA,
    PVR_BLEND_INVDESTALPHA
} pvr_blend_mode_t;

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

int pvr_prim(const void *data, size_t size);
int pvr_list_prim(pvr_list_t list, const void *data, size_t size);

#endif
