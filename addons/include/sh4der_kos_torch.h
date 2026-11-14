/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_TORCH_H
#define KOS_SH4DER_TORCH_H
#include <sh4der_kos_overlay.h>
#include <sh4der/sh4der_torch.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_TORCH_CARVE_VERTS (SH4DER_TORCH_BANDS*2*(SH4DER_TORCH_SPOKES+1))
#define SH4DER_KOS_TORCH_BOOST_VERTS (SH4DER_TORCH_RINGS*2*(SH4DER_TORCH_SPOKES+1))
typedef struct sh4der_kos_torch_shape {
    float cx,cy,rx,ry,tint_g,tint_b,hot_u,umax;
} sh4der_kos_torch_shape_t;
typedef struct sh4der_kos_torch_plan {
    sh4der_torch_vert_t geometry[SH4DER_TORCH_VERTS];
    float carve[SH4DER_TORCH_BANDS*2],boost[SH4DER_TORCH_RINGS*2];
    float tint_g,tint_b;
} sh4der_kos_torch_plan_t;
/* Host/load or shape change. Caller scratch306 public torch vertices, disjoint
   from plan/shape. rx/ry>0, tint/hot_u[0,1], umax>=EDGE_OUT and covers desired
   screen extent (public torch_umax derives it). No per-frame geometry admission.
   Failure preserves plan; original pinned CPU mesh definitions are linked. */
int sh4der_kos_torch_prepare(sh4der_kos_torch_plan_t *out,
    const sh4der_kos_torch_shape_t *shape, sh4der_torch_vert_t *scratch, size_t capacity);
/* Return306 vertices: first136 carve, then170 boost; zero without writes when
   power==carve==0. Repack18 band-side colors, reuse fixed positions. power/carve
   in[0,1]; caller derives carve=darkness*power using public dark/gate helpers.
   z_boost>z_carve>0; out aligned32/disjoint, capacity in vertices. Submit cached
   overlay MULTIPLY header + first136, then BOOST header + next170 via one TR
   sink. Separate reciprocal depths preserve autosort order. Failure preserves
   output. Public near_at can compose the existing Compact base_prepare hook. */
int sh4der_kos_torch_mesh(const sh4der_kos_torch_plan_t *plan,
    float power, float carve, float z_carve, float z_boost,
    pvr_vertex_t *out, size_t capacity);

#define SH4DER_KOS_TORCH_FOG_ENTRIES 129
typedef struct sh4der_kos_torch_fog_plan {
    float base[SH4DER_KOS_TORCH_FOG_ENTRIES],reach[SH4DER_KOS_TORCH_FOG_ENTRIES];
    float far_depth;
} sh4der_kos_torch_fog_plan_t;
/* Host/load or baseline/far-depth change. Copies129 baseline[0,1] values and
   prepares reach at their public torch entry depths. No renderer state change. */
int sh4der_kos_torch_fog_prepare(sh4der_kos_torch_fog_plan_t *out,
    const float *base, size_t count, float far_depth);
/* Emits129 bent values for reach[0,1], no per-entry admission. Caller applies
   at a safe scene boundary with pvr_fog_far_depth(plan.far_depth), BLACK fog
   color and pvr_fog_table_custom(out), or restores its own baseline. This formula
   is not equivalent for tinted fog. Caller owns global fog and current scene;
   helper never touches registers. Outputs disjoint, failure leaves out intact. */
int sh4der_kos_torch_fog(const sh4der_kos_torch_fog_plan_t *plan,
    float reach, float *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
