/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_screen.h>
#include <errno.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(sh4der_kos_screen_packet_t) == 160, "packet layout");

int sh4der_kos_grade_packets(const sh4der_look_grade_t *g,
    const float rect[4], float z0, float z_step,
    sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(!g || !rect || !out || ((uintptr_t)out & 31u)) {
        errno = EINVAL; return -1;
    }
    if(!isfinite(g->contrast) || !isfinite(g->exposure) ||
       !isfinite(z0) || !isfinite(z_step) || z0 <= 0 || z_step <= 0) {
        errno = EDOM; return -1;
    }
    for(unsigned i = 0; i < 4; ++i)
        if(!isfinite(rect[i])) { errno = EDOM; return -1; }
    if(rect[2] <= 0 || rect[3] <= 0 || !isfinite(rect[0] + rect[2]) ||
       !isfinite(rect[1] + rect[3])) { errno = EDOM; return -1; }
    for(unsigned i = 0; i < 3; ++i)
        if(!isfinite(g->gain[i]) || !isfinite(g->lift[i]) ||
           !isfinite(g->gain[i] * g->exposure * g->contrast) ||
           !isfinite(g->lift[i] + 0.5f * (1.0f - g->contrast))) {
            errno = EDOM; return -1;
        }
    sh4der_look_layer_t layers[SH4DER_LOOK_LAYERS];
    int n = sh4der_look_solve(g, layers);
    if((size_t)n > capacity) { errno = ENOSPC; return -1; }
    float depths[SH4DER_LOOK_LAYERS];
    for(int i = 0; i < n; ++i) {
        depths[i] = z0 + i * z_step;
        if(!isfinite(depths[i]) || (i && depths[i] <= depths[i - 1])) {
            errno = ERANGE; return -1;
        }
    }
    static const pvr_blend_mode_t source[] = {
        PVR_BLEND_ZERO, PVR_BLEND_DESTCOLOR, PVR_BLEND_ONE, PVR_BLEND_INVDESTCOLOR
    };
    static const pvr_blend_mode_t destination[] = {
        PVR_BLEND_DESTCOLOR, PVR_BLEND_ONE, PVR_BLEND_ONE, PVR_BLEND_ZERO
    };
    for(int i = 0; i < n; ++i) {
        pvr_poly_cxt_t c;
        pvr_poly_cxt_col(&c, PVR_LIST_TR_POLY);
        c.gen.culling = PVR_CULLING_NONE;
        c.gen.fog_type = PVR_FOG_DISABLE;
        c.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        c.depth.write = PVR_DEPTHWRITE_DISABLE;
        c.blend.src = source[layers[i].kind];
        c.blend.dst = destination[layers[i].kind];
        pvr_poly_compile(&out[i].header, &c);
        uint32_t argb = sh4der_look_argb(&layers[i]);
        for(unsigned k = 0; k < 4; ++k)
            out[i].vertices[k] = (pvr_vertex_t){
                .flags = k == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                .x = rect[0] + ((k & 1) ? rect[2] : 0),
                .y = rect[1] + ((k & 2) ? rect[3] : 0),
                .z = depths[i], .argb = argb
            };
    }
    return n;
}
