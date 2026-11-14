/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_focus.h>
#include <errno.h>
#include <math.h>

int sh4der_kos_focus_prepare(sh4der_kos_focus_plan_t *out,
    const sh4der_focus_mask_t *m) {
    if(!out || !m || m->shape < SH4DER_FOCUS_DISC || m->shape > SH4DER_FOCUS_RECT) {
        errno = EINVAL; return -1;
    }
    if(!isfinite(m->cx) || !isfinite(m->cy) || !isfinite(m->hw) ||
       !isfinite(m->hh) || !isfinite(m->feather) || m->hw < 0 ||
       m->hh < 0 || m->feather <= 0) { errno = EDOM; return -1; }
    sh4der_kos_focus_plan_t next = {.mask = *m};
    float extent[2] = {m->hw, m->hh};
    for(unsigned i = 0; i < 2; ++i) {
        float outer = extent[i] + m->feather;
        next.outer2[i] = outer * outer;
        if(outer <= extent[i] || !isfinite(next.outer2[i])) { errno = ERANGE; return -1; }
        next.inverse[i] = sh4der_recip_pos_(outer - extent[i]);
        if(!isfinite(next.inverse[i])) { errno = ERANGE; return -1; }
    }
    float dx = fabsf(m->cx) + 640, dy = fabsf(m->cy) + 480;
    if(!isfinite(dx * dx + dy * dy)) { errno = ERANGE; return -1; }
    *out = next;
    return 0;
}
int sh4der_kos_focus_blur(const uint16_t *grid, size_t count,
    uint16_t *out, size_t capacity) {
    if(!grid || !out || ((uintptr_t)grid & 1u) || ((uintptr_t)out & 1u)) {
        errno = EINVAL; return -1;
    }
    if(count < SH4DER_FOCUS_SRC_W * SH4DER_FOCUS_SRC_H ||
       capacity < SH4DER_FOCUS_TEX_W * SH4DER_FOCUS_TEX_H) { errno = ENOSPC; return -1; }
    sh4der_focus_box565(grid, out);
    return 0;
}
static float feather(const sh4der_kos_focus_plan_t *p, float d2, unsigned axis) {
    if(d2 >= p->outer2[axis]) return 0;
    return sh4der_falloff_(d2, axis ? p->mask.hh : p->mask.hw, p->inverse[axis]);
}
int sh4der_kos_focus_mesh(const sh4der_kos_focus_plan_t *p,
    const float rect[4], float z, pvr_vertex_t *out, size_t capacity) {
    if(!p || !rect || !out || ((uintptr_t)out & 31u)) { errno = EINVAL; return -1; }
    if(capacity < SH4DER_FOCUS_VERTS) { errno = ENOSPC; return -1; }
    for(unsigned i = 0; i < 4; ++i)
        if(!isfinite(rect[i])) { errno = EDOM; return -1; }
    if(!isfinite(z) || z <= 0 || rect[2] <= 0 || rect[3] <= 0 ||
       !isfinite(rect[0] + rect[2]) || !isfinite(rect[1] + rect[3])) {
        errno = EDOM; return -1;
    }
    unsigned n = 0;
    for(unsigned j = 0; j < SH4DER_FOCUS_GRID_Y; ++j)
        for(unsigned i = 0; i <= SH4DER_FOCUS_GRID_X; ++i)
            for(unsigned k = 0; k < 2; ++k) {
                float u = i * (1.0f / SH4DER_FOCUS_GRID_X);
                float v = (j + k) * (1.0f / SH4DER_FOCUS_GRID_Y);
                float dx = u * 640 - p->mask.cx, dy = v * 480 - p->mask.cy, sharp;
                if(p->mask.shape == SH4DER_FOCUS_DISC) sharp = feather(p, dx * dx + dy * dy, 0);
                else if(p->mask.shape == SH4DER_FOCUS_BAND) sharp = feather(p, dy * dy, 1);
                else sharp = feather(p, dx * dx, 0) * feather(p, dy * dy, 1);
                uint32_t a = (uint32_t)(sh4der_sat_(SH4DER_FOCUS_MAX * (1 - sharp)) * 255 + .5f);
                out[n++] = (pvr_vertex_t){
                    .flags = i == SH4DER_FOCUS_GRID_X && k ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                    .x = rect[0] + u * rect[2], .y = rect[1] + v * rect[3], .z = z,
                    .u = u * (80.0f / 128), .v = v * (60.0f / 64),
                    .argb = (a << 24) | 0xffffffu
                };
            }
    return (int)n;
}
