/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_geometry.h>
#include <errno.h>

int sh4der_kos_bloom_vertices(const sh4der_bloom_mesh_t *mesh,
    const float edge[3], const float core[3], uint8_t alpha,
    pvr_vertex_t *out, size_t capacity) {
    if(!mesh || !edge || !core || !out) { errno = EINVAL; return -1; }
    if(capacity < SH4DER_KOS_BLOOM_VERTICES) { errno = ENOSPC; return -1; }
    uint32_t colors[SH4DER_BLOOM_VERTS];
    sh4der_bloom_argb(mesh, edge, core, alpha, colors, sizeof(colors[0]));
    for(unsigned t = 0; t < SH4DER_BLOOM_TRIS; ++t)
        for(unsigned c = 0; c < 3; ++c) {
            unsigned i = sh4der_bloom_tris[t][c];
            out[t * 3 + c] = (pvr_vertex_t){
                .flags = c == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                .x = mesh->xyz[i][0], .y = mesh->xyz[i][1],
                .z = mesh->xyz[i][2], .argb = colors[i]
            };
        }
    return SH4DER_KOS_BLOOM_VERTICES;
}

int sh4der_kos_foot_vertices(const sh4dow_foot_t *foot,
    const sh4dow_foot_params_t *params, const float plane[3],
    const float normal[3], const float up[3], const float forward[3],
    uint8_t opacity, pvr_vertex_t *out, size_t capacity) {
    if(!foot || !plane || !normal || !up || !forward || !out) {
        errno = EINVAL; return -1;
    }
    if(capacity < 4) { errno = ENOSPC; return -1; }
    float quad[4][3];
    float fade = sh4dow_foot_cone(foot, params, plane, normal, up, forward, quad);
    if(!(fade > 0)) return 0;
    uint32_t color = (uint32_t)(opacity * fade + 0.5f) << 24;
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (pvr_vertex_t){
            .flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
            .x = quad[i][0], .y = quad[i][1], .z = quad[i][2],
            .u = (float)(i & 1), .v = (float)(i >> 1), .argb = color
        };
    return 4;
}

int sh4der_kos_foot_alpha4444(uint16_t *out, size_t capacity, unsigned side) {
    if(!out || !side) { errno = EINVAL; return -1; }
    if((size_t)side > SIZE_MAX / side || (size_t)side * side > capacity) {
        errno = ENOSPC; return -1;
    }
    float step = 1.0f / side;
    for(unsigned y = 0; y < side; ++y)
        for(unsigned x = 0; x < side; ++x) {
            float strength = sh4dow_foot_texel((x + 0.5f) * step, (y + 0.5f) * step);
            out[(size_t)y * side + x] = (uint16_t)((unsigned)(strength * 15 + 0.5f) << 12);
        }
    return 0;
}
