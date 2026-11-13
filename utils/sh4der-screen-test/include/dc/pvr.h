#ifndef SH4DER_SCREEN_TEST_PVR_H
#define SH4DER_SCREEN_TEST_PVR_H
#include "../../../pvr-material-test/include/dc/pvr.h"
#define PVR_CMD_VERTEX UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)
typedef struct pvr_vertex {
    alignas(32) uint32_t flags;
    float x, y, z, u, v;
    uint32_t argb, oargb;
} pvr_vertex_t;
void pvr_poly_cxt_col(pvr_poly_cxt_t *, pvr_list_t);
void pvr_poly_compile(pvr_poly_hdr_t *, const pvr_poly_cxt_t *);
#endif
