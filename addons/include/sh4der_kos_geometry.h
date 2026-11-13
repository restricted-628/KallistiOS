/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_GEOMETRY_H
#define KOS_SH4DER_GEOMETRY_H
#include <dc/pvr_geometry.h>
#include <sh4der/sh4der_bloom.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_BLOOM_VERTICES (SH4DER_BLOOM_TRIS * 3u)
/* Output is unprojected, caller-owned. Use KOS projection/clipping and one
   geometry sink; no headers, submissions, allocations or matrix mutations.
   Return vertex count, or -1/errno without changing output. Mesh is prepared
   by sh4der_bloom_blade; edge/core are finite [0,1] colors. */
int sh4der_kos_bloom_vertices(const sh4der_bloom_mesh_t *mesh,
    const float edge[3], const float core[3], uint8_t alpha,
    pvr_vertex_t *out, size_t capacity);

/* Same posed world space for foot/plane/up/forward. Up is unit length;
   params and vectors meet sh4dow_foot_cone's finite domain. Plane comes from
   the caller's current receiver query, never a stale cached hit. Return 4,
   0 for a rejected surface/height (output untouched), or -1 on capacity/error.
   UVs cover a linear ARGB4444 alpha-mask texture from foot_alpha4444 below.
   Use black RGB, MODULATEALPHA, SRCALPHA/INVSRCALPHA, no depth writes, and
   caller-owned texture lifetime. Plane lift is controlled by params. */
int sh4der_kos_foot_vertices(const sh4dow_foot_t *foot,
    const sh4dow_foot_params_t *params, const float plane[3],
    const float normal[3], const float up[3], const float forward[3],
    uint8_t opacity, pvr_vertex_t *out, size_t capacity);

/* Host/load-time alpha texture bake, row-major black ARGB4444. Side must be
   nonzero; output capacity is in texels. PVR size/layout admission and upload
   are caller responsibilities. Failure leaves output untouched. */
int sh4der_kos_foot_alpha4444(uint16_t *out, size_t capacity, unsigned side);
#ifdef __cplusplus
}
#endif
#endif
