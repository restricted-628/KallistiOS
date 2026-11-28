/* KallistiOS ##version##
   Prepared directional lighting for an admitted two-volume Compact draw. */
#ifndef SH4DER_KOS_ROUTED_LIGHTING_H
#define SH4DER_KOS_ROUTED_LIGHTING_H

#include <dc/pvr_chunk_cache.h>
#include <sh4zam/shz_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SH4DER_KOS_ROUTED_LIGHT_MAX = 3,
    SH4DER_KOS_ROUTED_VOLUMES = 2,
    SH4DER_KOS_ROUTED_CHANNELS = 3
};
enum { SH4DER_KOS_ROUTED_BASE, SH4DER_KOS_ROUTED_OFFSET, SH4DER_KOS_ROUTED_FIELDS };

typedef struct sh4der_kos_routed_directional_light {
    shz_vec3_t direction;           /* Unit vector from the surface to the light. */
    float color[SH4DER_KOS_ROUTED_CHANNELS];   /* Linear RGB in [0, 1]. */
    float gain;                     /* Signed intensity in [-1, 1]. */
    bool thin;                      /* Light a thin surface from either side. */
} sh4der_kos_routed_directional_light_t;

typedef uint8_t sh4der_kos_routed_masks_t[SH4DER_KOS_ROUTED_VOLUMES][SH4DER_KOS_ROUTED_FIELDS];

/* Linear RGB888 for base and offset. A missing inside override inherits
   both outside values. An explicit black override is valid. */
typedef struct sh4der_kos_routed_ambient {
    uint32_t outside[SH4DER_KOS_ROUTED_FIELDS];
    uint32_t inside[SH4DER_KOS_ROUTED_FIELDS];
    bool separate_inside;
} sh4der_kos_routed_ambient_t;

/* Caller-owned snapshot. Initialize through sh4der_kos_routed_lighting_prepare(). */
typedef struct sh4der_kos_routed_lighting_plan {
    unsigned count;
    unsigned thin;
    float ambient[SH4DER_KOS_ROUTED_VOLUMES][SH4DER_KOS_ROUTED_FIELDS][SH4DER_KOS_ROUTED_CHANNELS];
    shz_vec3_t directions[SH4DER_KOS_ROUTED_LIGHT_MAX];
    float weights[SH4DER_KOS_ROUTED_VOLUMES][SH4DER_KOS_ROUTED_FIELDS][SH4DER_KOS_ROUTED_CHANNELS][SH4DER_KOS_ROUTED_LIGHT_MAX];
} sh4der_kos_routed_lighting_plan_t;

/* Prepare once per light or material change. Bit i in each base/offset mask
   selects lights[i]. Supply the admitted cache format. Color-only caches
   accept base routes only. NULL ambient starts all fields at zero. Failure
   preserves the previous plan. The result owns its copied coefficients. */
int sh4der_kos_routed_lighting_prepare(
    sh4der_kos_routed_lighting_plan_t *plan,
    const sh4der_kos_routed_directional_light_t *lights,
    unsigned count, const sh4der_kos_routed_masks_t masks,
    pvr_geometry_vertex_format_t format,
    const sh4der_kos_routed_ambient_t *ambient);

/* Matches pvr_chunk_cache_prepare_two_volume_vertex_t. Pass an immutable
   plan prepared for this draw's format as data. Supply finite unit normals
   and unit light directions in the same space. Replaces both base RGBs
   and, for textured packets, both offset RGBs. All alpha bytes, UVs,
   geometry, commands and padding are preserved. Authored RGB tint and
   specular evaluation are outside this directional diffuse policy. */
int sh4der_kos_routed_lighting_vertex(
    const pvr_chunk_render_state_t *state,
    uint16_t source_index,
    const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format,
    pvr_chunk_two_volume_vertex_t *vertex, void *data);
#ifdef __cplusplus
}
#endif
#endif
