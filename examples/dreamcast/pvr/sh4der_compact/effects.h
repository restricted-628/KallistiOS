/* KallistiOS ##version##
   Original caller-owned integration example. Copyright (C) 2026 Joseph Black.
   Dependency licenses/credits: addons/libsh4der/README.md.
*/
#ifndef EXAMPLE_SH4DER_EFFECTS_H
#define EXAMPLE_SH4DER_EFFECTS_H

#include <sh4der_kos.h>
#include <sh4der_kos_geometry.h>
#include <sh4der/sh4der_blade.h>
#include <sh4der/sh4der_bloom.h>
#include <sh4der/sh4der_holo.h>

typedef struct effect_state {
    sh4der_blade_frame_t blade;
    sh4der_holo_frame_t holo;
    const uint8_t *weights; /* Admitted sidecar indexed by MODEL vertex index. */
} effect_state_t;

#define BLOOM_VERTEX_COUNT SH4DER_KOS_BLOOM_VERTICES

static inline int bloom_vertices(const sh4der_bloom_mesh_t *mesh,
                                 pvr_vertex_t out[BLOOM_VERTEX_COUNT]) {
    const float edge[3] = {0.05f, 0.3f, 1.0f};
    const float core[3] = {0.7f, 0.9f, 1.0f};
    return sh4der_kos_bloom_vertices(mesh, edge, core, 160, out, BLOOM_VERTEX_COUNT);
}
#endif
