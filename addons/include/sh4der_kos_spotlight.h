/* KallistiOS ##version## */
#ifndef SH4DER_KOS_SPOTLIGHT_H
#define SH4DER_KOS_SPOTLIGHT_H

#include <dc/pvr_chunk_cache.h>
#include <sh4zam/shz_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sh4der_kos_spotlight {
    shz_vec3_t position;
    shz_vec3_t axis;        /* Unit vector pointing out of the cone. */
    float cutoff_cos;       /* Outer half-angle cosine in [0, 1). */
    float radius;           /* Inclusive finite range in model units. */
    float constant, linear, quadratic; /* c + l*d + q*d*d */
    uint32_t rgb;           /* Linear RGB888 light tint. */
} sh4der_kos_spotlight_t;

typedef struct sh4der_kos_spotlight_plan {
    sh4der_kos_spotlight_t light;
    float radius_squared;
    float inverse_cone_width;
    float tint[3];
} sh4der_kos_spotlight_plan_t;

/* Prepare once per light change. Constant attenuation must be positive and
   the other coefficients nonnegative. Failure preserves the previous plan. */
int sh4der_kos_spotlight_prepare(sh4der_kos_spotlight_plan_t *out,
                                 const sh4der_kos_spotlight_t *light);

/* pvr_chunk_cache_prepare_vertex_t callback. Pass the prepared plan as data.
   Resolved finite positions, unit normals and the light share a coordinate
   space before projection. Cone falloff is linear in cosine; the apex,
   back faces and points on or outside the angular cutoff receive zero.
   The spotlight multiplies authored base RGB, preserving alpha, additive
   color, UVs, command and geometry. No ambient or specular contribution. */
int sh4der_kos_spotlight_vertex(const pvr_chunk_render_state_t *state,
    uint16_t source_index, const pvr_deform_vertex_t *deformation,
    pvr_vertex_t *vertex, void *data);

#ifdef __cplusplus
}
#endif
#endif
