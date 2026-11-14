/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_SHAFTS_H
#define KOS_SH4DER_SHAFTS_H
#include <sh4der_kos_image.h>
#include <sh4der_kos_frame.h>
#include <sh4der/sh4der_shafts.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_SHAFTS_VERTS (2*(SH4DER_GLOW_MESH_X+1)*SH4DER_GLOW_MESH_Y)
typedef struct sh4der_kos_shafts_plan {
    float sun[2];
    float near[SH4DER_KOS_FRAME_SAMPLES];
} sh4der_kos_shafts_plan_t;
typedef struct sh4der_kos_shafts_stage {
    alignas(32) uint16_t pixels[SH4DER_GLOW_TEX_W*SH4DER_GLOW_TEX_H];
    sh4der_kos_frame_id_t source;
} sh4der_kos_shafts_stage_t;
/* Host/static-light preparation or changed light position; normalized sun[0,1].
   Immutable after prepare. Map updates reuse distance attenuation, no roots. */
int sh4der_kos_shafts_prepare(sh4der_kos_shafts_plan_t *out, const float sun[2]);
/*80x60 RGB565 pre-effect grid ->128x64 shade map, white padding. strength[0,3],
   facing[-1,1] is view/light cosine. previous optional, same generation and
   strictly earlier render; NULL resets to white. Fixed0.5 easing; out==previous
   allowed, other ranges disjoint. Failure preserves out. Easing is not feedback
   removal: retain pre-shafts source. Bind map image LINEAR RGB565128x64, content
   80x60, BILINEAR/centers/clamp; upload with image_upload after retiring all
   map readers. Source ID denotes sampled frame, not map's last GPU reader. */
int sh4der_kos_shafts_map(sh4der_kos_shafts_stage_t *out,
    const sh4der_kos_shafts_plan_t *plan, const uint16_t *grid, size_t count,
    float strength, float facing, sh4der_kos_frame_id_t source,
    const sh4der_kos_shafts_stage_t *previous);
/*108 vertices, submit image.headers[MULTIPLY] then vertices via one TR sink.
   rect={x,y,w,h}, z>0; scale[0,1] (sh4der_shafts_scale derives it from strength).
   Capacity counts vertices; out aligned32/disjoint. Return108 or -1/errno.
   This is shadow-shaft attenuation, distinct from additive glow shaft copies. */
int sh4der_kos_shafts_mesh(const sh4der_kos_shafts_plan_t *plan,
    const float rect[4], float z, float scale, pvr_vertex_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
