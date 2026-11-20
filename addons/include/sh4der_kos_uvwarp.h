/* KallistiOS ##version## */
#ifndef KOS_SH4DER_UVWARP_H
#define KOS_SH4DER_UVWARP_H

#include <dc/pvr_chunk_render.h>
#include <sh4der/sh4der_uvwarp.h>

/* Compact prepare_vertex callback, also usable as sh4der_kos_effect_t's
   base_prepare. Prepare the scroll once per frame with sh4der_uvwarp_prepare.
   Select this callback only after prepare succeeds. Its table must remain
   readable (2*w*h bytes) for the draw. The owner admits
   UVs in [-32, 2^23-34] and weight in [0,1] before selecting this fast path.
   No allocation, PVR state change or mutable per-vertex history. */
typedef struct sh4der_kos_uvwarp {
    sh4der_uvwarp_prep_t prepared;
    float weight;
} sh4der_kos_uvwarp_t;

static inline int sh4der_kos_uvwarp_vertex(const pvr_chunk_render_state_t *state,
    const pvr_chunk_vertex_attributes_t *attributes,
    const pvr_chunk_strip_attributes_t *strip, pvr_vertex_t *vertex, void *data) {
    const sh4der_kos_uvwarp_t *warp = (const sh4der_kos_uvwarp_t *)data;
    float du, dv;
    (void)state; (void)attributes; (void)strip;
    sh4der_uvwarp_offset_pn(&warp->prepared, vertex->u, vertex->v,
                            warp->weight, &du, &dv);
    vertex->u += du;
    vertex->v += dv;
    return 0;
}
#endif
