/* KallistiOS ##version##
   Original caller-owned integration example. Copyright (C) 2026 Joseph Black.
   Dependency licenses/credits: addons/libsh4der/README.md.
*/
#ifndef EXAMPLE_SH4DER_EFFECTS_H
#define EXAMPLE_SH4DER_EFFECTS_H

#include <dc/pvr_chunk_render.h>
#include <sh4der/sh4der_blade.h>
#include <sh4der/sh4der_bloom.h>
#include <sh4der/sh4der_holo.h>

typedef struct effect_state {
    sh4der_blade_frame_t blade;
    sh4der_holo_frame_t holo;
    const uint8_t *weights; /* Admitted sidecar indexed by MODEL vertex index. */
} effect_state_t;

/* These are emissive passes: replace base color, preserve UV/offset/command.
   Positions are already in the effect's object space; no matrix state changes.
   For a lit material, compose the effect with lighting explicitly instead. */
static inline int blade_vertex(const pvr_chunk_render_state_t *state,
                               const pvr_chunk_vertex_attributes_t *attributes,
                               const pvr_chunk_strip_attributes_t *strip,
                               pvr_vertex_t *vertex, void *data) {
    const effect_state_t *effect = data;
    const float color[3] = { 0.15f, 0.65f, 1.0f };
    const float xyz[3] = { vertex->x, vertex->y, vertex->z };
    (void)state; (void)attributes; (void)strip;
    sh4der_blade_argb(&effect->blade, color, 255, xyz, sizeof(xyz),
                      &vertex->argb, sizeof(vertex->argb), 1);
    return 0;
}

static inline int holo_vertex(const pvr_chunk_render_state_t *state,
                              const pvr_chunk_vertex_attributes_t *attributes,
                              const pvr_chunk_strip_attributes_t *strip,
                              pvr_vertex_t *vertex, void *data) {
    const effect_state_t *effect = data;
    (void)state; (void)strip;
    vertex->argb = sh4der_holo_argb(&effect->holo,
                                   effect->weights[attributes->index]);
    return 0;
}

#define BLOOM_VERTEX_COUNT (SH4DER_BLOOM_TRIS * 3)

/* Fixed topology, caller-owned storage. No submission or allocation. */
static inline void bloom_vertices(const sh4der_bloom_mesh_t *mesh,
                                  pvr_vertex_t out[BLOOM_VERTEX_COUNT]) {
    const float edge[3] = { 0.05f, 0.3f, 1.0f };
    const float core[3] = { 0.7f, 0.9f, 1.0f };
    uint32_t colors[SH4DER_BLOOM_VERTS];
    sh4der_bloom_argb(mesh, edge, core, 160, colors, sizeof(colors[0]));
    for(unsigned t = 0; t < SH4DER_BLOOM_TRIS; ++t) {
        for(unsigned c = 0; c < 3; ++c) {
            const unsigned i = sh4der_bloom_tris[t][c];
            out[t * 3 + c] = (pvr_vertex_t) {
                .flags = c == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                .x = mesh->xyz[i][0], .y = mesh->xyz[i][1],
                .z = mesh->xyz[i][2], .argb = colors[i]
            };
        }
    }
}
#endif
